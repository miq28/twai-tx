#include "can_bus.h"
#include "config.h"
#include <Arduino.h>
#include "led_activity.h"

#include "esp_twai.h"
#include "esp_twai_onchip.h"

// === TX queue & monitoring
static QueueHandle_t canTxQueue;
static volatile uint32_t txSuccess = 0;
static volatile uint32_t txDrop = 0;
static volatile uint32_t txAttempt = 0;

typedef struct
{
    twai_message_t msg;
    uint8_t data[8]; // 🔥 persistent buffer
} can_tx_item_t;
#define TX_POOL_SIZE 64

static uint8_t txPool[TX_POOL_SIZE][8];
static volatile uint32_t txPoolIndex = 0;
// === TX queue

namespace CANDriver
{
    static uint32_t currentBaud = 500000;
    static bool currentListenOnly = false;
    static bool driverRunning = false;

    static twai_node_handle_t node = nullptr;

    // >>> ADD THIS <<<
    static volatile bool canTxAllowed = true;

    // ================= EVENT BUFFER =================

    typedef enum
    {
        CAN_EVT_ERROR,
        CAN_EVT_STATE_CHANGE
    } can_evt_type_t;

    typedef struct
    {
        can_evt_type_t type;

        union
        {
            uint32_t errFlags;

            struct
            {
                uint8_t oldState;
                uint8_t newState;
            } state;
        };
    } can_evt_t;

#define CAN_EVT_BUF_SIZE 32

    static can_evt_t evtBuf[CAN_EVT_BUF_SIZE];
    static volatile uint8_t evtHead = 0;
    static volatile uint8_t evtTail = 0;

    // ===== ERROR COUNTERS =====
    static volatile uint32_t err_ack = 0;
    static volatile uint32_t err_bit = 0;
    static volatile uint32_t err_form = 0;
    static volatile uint32_t err_stuff = 0;
    static volatile uint32_t err_arb = 0;
    static volatile uint32_t err_other = 0;

    // ISR-safe push
    static inline void pushEventFromISR(const can_evt_t &e)
    {
        uint8_t next = (evtHead + 1) % CAN_EVT_BUF_SIZE;

        if (next == evtTail)
        {
            evtTail = (evtTail + 1) % CAN_EVT_BUF_SIZE; // overwrite oldest
        }

        evtBuf[evtHead] = e;
        evtHead = next;
    }

    // task-side pop
    static bool popEvent(can_evt_t &out)
    {
        if (evtTail == evtHead)
            return false;

        out = evtBuf[evtTail];
        evtTail = (evtTail + 1) % CAN_EVT_BUF_SIZE;
        return true;
    }

    // =========================================================
    // RX ISR CALLBACK
    // =========================================================

    static bool on_rx_done(twai_node_handle_t n,
                           const twai_rx_done_event_data_t *edata,
                           void *)
    {
        uint8_t buf[64]; // enough for CAN FD (safe even if you use classic)

        twai_frame_t frame = {};
        frame.buffer = buf;             // 🔥 REQUIRED
        frame.buffer_len = sizeof(buf); // 🔥 REQUIRED

        if (twai_node_receive_from_isr(n, &frame) == ESP_OK)
        {
            twai_message_t msg = {};
            msg.identifier = frame.header.id;
            msg.extd = frame.header.ide;
            msg.rtr = frame.header.rtr;

            // 🔥 clamp here
            uint8_t dlc = frame.buffer_len;
            if (dlc > 8)
                dlc = 8;

            msg.data_length_code = dlc;

            if (!msg.rtr && dlc > 0)
            {
                memcpy(msg.data, frame.buffer, dlc);
            }

            CANRxBuffer::pushFromISR(msg);

            // Activity LED
            // ✅ ADD THIS LINE HERE
            ledRxEvent();
        }

        return false;
    }

    static const char *errFlagToStr(uint32_t f)
    {
        if (f & (1 << 4))
            return "ACK";
        if (f & (1 << 1))
            return "BIT";
        if (f & (1 << 2))
            return "FORM";
        if (f & (1 << 3))
            return "STUFF";
        if (f & (1 << 0))
            return "ARB_LOST";
        return "UNKNOWN";
    }

    static const char *stateToStr(uint8_t s)
    {
        switch (s)
        {
        case TWAI_ERROR_ACTIVE:
            return "ACTIVE";
        case TWAI_ERROR_WARNING:
            return "WARNING";
        case TWAI_ERROR_PASSIVE:
            return "PASSIVE";
        case TWAI_ERROR_BUS_OFF:
            return "BUS_OFF";
        default:
            return "?";
        }
    }

    void processEvents()
    {
        can_evt_t e;

        while (popEvent(e))
        {
            if (e.type == CAN_EVT_ERROR)
            {
                // ❌ no ERROR logging here anymore
                // counting is already done in ISR (on_error)
                // printing is in task and now rate-limited and only prints the error type and count
                // no more state info (can be added back if needed)
            }
            else if (e.type == CAN_EVT_STATE_CHANGE)
            {
                DEBUG("[CAN][STATE] %s(%d) -> %s(%d)\n",
                      stateToStr(e.state.oldState),
                      e.state.oldState,
                      stateToStr(e.state.newState),
                      e.state.newState);
            }
        }
    }

    void processError()
    {
        static uint32_t lastErrReport = 0;

        uint32_t now = millis();
        if (now - lastErrReport < 1000)
            return;

        lastErrReport = now;

        // ===== snapshot + reset =====
        uint32_t ack = err_ack;
        err_ack = 0;
        uint32_t bit = err_bit;
        err_bit = 0;
        uint32_t form = err_form;
        err_form = 0;
        uint32_t stuff = err_stuff;
        err_stuff = 0;
        uint32_t arb = err_arb;
        err_arb = 0;
        uint32_t other = err_other;
        err_other = 0;

        // ===== print only if any error =====
        if (!(ack || bit || form || stuff || arb || other))
            return;

        // ===== build single-line log =====
        char line[128];
        int len = 0;

        len += snprintf(line + len, sizeof(line) - len, "[CAN][ERR/s]");

        if (stuff)
            len += snprintf(line + len, sizeof(line) - len, " STUFF=%lu", stuff);
        if (bit)
            len += snprintf(line + len, sizeof(line) - len, " BIT=%lu", bit);
        if (form)
            len += snprintf(line + len, sizeof(line) - len, " FORM=%lu", form);
        if (ack)
            len += snprintf(line + len, sizeof(line) - len, " ACK=%lu", ack);
        if (arb)
            len += snprintf(line + len, sizeof(line) - len, " ARB=%lu", arb);
        if (other)
            len += snprintf(line + len, sizeof(line) - len, " OTHER=%lu", other);

        len += snprintf(line + len, sizeof(line) - len, "\n");

        // ===== single DEBUG call =====
        DEBUG("%s", line);
    }

    static bool on_error(twai_node_handle_t,
                         const twai_error_event_data_t *edata,
                         void *)
    {
        uint32_t f = edata->err_flags.val;

        // ===== COUNT (VERY FAST, ISR SAFE) =====
        if (f & (1 << 4))
            err_ack = err_ack + 1;
        else if (f & (1 << 1))
            err_bit = err_bit + 1;
        else if (f & (1 << 2))
            err_form = err_form + 1;
        else if (f & (1 << 3))
            err_stuff = err_stuff + 1;
        else if (f & (1 << 0))
            err_arb = err_arb + 1;
        else
            err_other = err_other + 1;

        // still push event if you want state logs
        can_evt_t e = {};
        e.type = CAN_EVT_ERROR;
        e.errFlags = f;
        pushEventFromISR(e);

        return false;
    }

    static bool on_state_change(twai_node_handle_t,
                                const twai_state_change_event_data_t *edata,
                                void *)
    {
        // if (edata->old_sta == edata->new_sta)
        // {
        //     return false; // 🚀 drop useless event
        // }

        can_evt_t e = {};
        e.type = CAN_EVT_STATE_CHANGE;
        e.state.oldState = edata->old_sta;
        e.state.newState = edata->new_sta;

        pushEventFromISR(e);

        return false;
    }

    // =========================================================
    // DRIVER START
    // =========================================================
    static bool startDriver(uint32_t baud, bool listenOnly)
    {
        twai_onchip_node_config_t cfg = {
            .io_cfg = {
                .tx = CAN_TX,
                .rx = CAN_RX,
                .quanta_clk_out = GPIO_NUM_NC,
                .bus_off_indicator = GPIO_NUM_NC,
            },
            .bit_timing = {
                .bitrate = baud,
            },
            .tx_queue_depth = 8, // initially was 64
            .flags = {
                .enable_listen_only = listenOnly,
            }};

        if (twai_new_node_onchip(&cfg, &node) != ESP_OK)
        {
            CAN_LOG("[CAN] install failed\n");
            return false;
        }

        twai_event_callbacks_t cbs = {};
        cbs.on_rx_done = on_rx_done;
        cbs.on_error = on_error;
        cbs.on_state_change = on_state_change;

        twai_node_register_event_callbacks(node, &cbs, nullptr);

        if (twai_node_enable(node) != ESP_OK)
        {
            CAN_LOG("[CAN] start failed\n");
            return false;
        }

        currentBaud = baud;
        currentListenOnly = listenOnly;
        driverRunning = true;

        CAN_LOG("[CAN] started\n");
        return true;
    }

    static void canTxTask(void *)
    {
        can_tx_item_t item;

        while (true)
        {
            if (xQueueReceive(canTxQueue, &item, portMAX_DELAY))
            {
                // ===== SANITIZE DLC =====
                uint8_t dlc = item.msg.data_length_code;
                if (dlc > 8)
                    dlc = 8;

                // ===== BUILD FRAME =====
                twai_frame_t frame = {};

                frame.header.id = item.msg.identifier;
                frame.header.ide = item.msg.extd;
                frame.header.rtr = item.msg.rtr;

                // ===== IMPORTANT: USE item.data DIRECTLY =====
                // This is safe now because we BLOCK until driver accepts it
                frame.buffer = item.data;
                frame.buffer_len = dlc;

                // ===== BLOCKING TRANSMIT (CRITICAL FIX) =====
                esp_err_t err = twai_node_transmit(node, &frame, portMAX_DELAY);

                if (err == ESP_OK)
                {
                    txSuccess = txSuccess + 1;
                }
                else
                {
                    txDrop = txDrop + 1;

                    // Rate-limited error log (real errors only)
                    static uint32_t lastPrint = 0;
                    if (millis() - lastPrint > 1000)
                    {
                        lastPrint = millis();
                        DEBUG("[CAN] TX error: %d\n", err);
                    }
                }
            }
        }
    }

    void flushTxQueue()
    {
        if (canTxQueue)
        {
            xQueueReset(canTxQueue);
        }
    }

    uint32_t getTxAttempt() { return txAttempt; }
    uint32_t getTxSuccess() { return txSuccess; }
    uint32_t getTxDrop() { return txDrop; }
    uint32_t getTxQueueFree()
    {
        return uxQueueSpacesAvailable(canTxQueue);
    }
    uint32_t getTxQueueUsed()
    {
        return uxQueueMessagesWaiting(canTxQueue);
    }

    void init(uint32_t baud, bool listenOnly)
    {
        startDriver(baud, listenOnly);

        // === TX queue
        // canTxQueue = xQueueCreate(256, sizeof(can_tx_item_t));
        canTxQueue = xQueueCreate(512, sizeof(can_tx_item_t));

        xTaskCreatePinnedToCore(
            canTxTask,
            "can_tx",
            4096,
            NULL,
            14,
            NULL,
            1);

        // CANRxBuffer::clear();
        // CANRxBuffer::startTask(); // if you still use it
        CANMonitor::startTask(); // ✅ new
    }

    bool sendAsync(const twai_message_t &msg)
    {
        if (!canTxAllowed)
        {
            return false; // 🚫 block all TX while recovering
        }

        if (!canTxQueue)
            return false;

        can_tx_item_t item;

        // ===== COPY STRUCT =====
        item.msg = msg;

        // ===== SANITIZE DLC =====
        uint8_t dlc = item.msg.data_length_code;

        if (dlc > 8)
            dlc = 8;

        if (item.msg.rtr)
            dlc = 0;

        item.msg.data_length_code = dlc;

        // ===== COPY DATA INTO PERSISTENT BUFFER =====
        if (!item.msg.rtr && dlc > 0)
        {
            memcpy(item.data, msg.data, dlc);
        }

        txAttempt = txAttempt + 1;

        // ===== QUEUE =====
        if (uxQueueSpacesAvailable(canTxQueue) == 0) // Prevent useless enqueue when queue is full
        {
            txDrop = txDrop + 1;
            return false;
        }

        if (xQueueSend(canTxQueue, &item, 0) != pdTRUE)
        {
            txDrop = txDrop + 1;

            static uint32_t lastPrint = 0;
            if (millis() - lastPrint > 1000)
            {
                lastPrint = millis();
                DEBUG("[CAN] TX drop: %lu\n", txDrop);
            }

            return false;
        }

        return true;
    }

    bool reinit(uint32_t baud, bool listenOnly)
    {
        // CANRxBuffer::stopTask();
        CANMonitor::stopTask();

        if (node)
        {
            twai_node_disable(node);
            twai_node_delete(node);
            node = nullptr;
        }

        bool ok = startDriver(baud, listenOnly);

        if (ok)
        {
            txAttempt = 0;
            txSuccess = 0;
            txDrop = 0;

            // CANRxBuffer::clear();
            // CANRxBuffer::startTask();
            CANMonitor::startTask();
        }

        return ok;
    }

    // bool send(const twai_message_t &msg)
    // {
    //     if (!CANDriver::isRunning())
    //         return false;

    //     if (!node)
    //         return false;

    //     // 🔥 DEBUG + CLAMP HERE
    //     uint8_t dlc = msg.data_length_code;

    //     if (dlc > 8)
    //     {
    //         static uint32_t lastWarn = 0;
    //         if (millis() - lastWarn > 1000) // rate limit
    //         {
    //             lastWarn = millis();
    //             DEBUG("[WARN] DLC corrupted: %u\n", dlc);
    //         }
    //         dlc = 8;
    //     }

    //     uint8_t buf[8]; // 🔥 FIX

    //     twai_frame_t frame = {};
    //     frame.header.id = msg.identifier;
    //     frame.header.ide = msg.extd;
    //     frame.header.rtr = msg.rtr;
    //     frame.buffer = buf;
    //     frame.buffer_len = dlc;

    //     if (!msg.rtr && dlc > 0)
    //     {
    //         memcpy(buf, msg.data, dlc);
    //     }

    //     return twai_node_transmit(node, &frame, 0) == ESP_OK;
    // }

    bool isRunning() { return driverRunning; }
    uint32_t getCurrentBaud() { return currentBaud; }
    bool isListenOnly() { return currentListenOnly; }

    CANHealthState getCANHealth()
    {
        if (!node)
            return CAN_HEALTH_ERROR;

        twai_node_status_t s;
        twai_node_get_info(node, &s, nullptr);

        switch (s.state)
        {
        case TWAI_ERROR_ACTIVE:
            return CAN_HEALTH_OK;
        case TWAI_ERROR_WARNING:
            return CAN_HEALTH_DEGRADED;
        case TWAI_ERROR_PASSIVE:
            return CAN_HEALTH_ERROR;
        case TWAI_ERROR_BUS_OFF:
            return CAN_HEALTH_BUS_OFF;
        default:
            return CAN_HEALTH_ERROR;
        }
    }

    twai_error_state_t getErrorStateRaw()
    {
        if (!node)
            return TWAI_ERROR_BUS_OFF; // safest fallback

        twai_node_status_t s;
        twai_node_get_info(node, &s, nullptr);

        return s.state;
        // 0 = ACTIVE
        // 1 = WARNING
        // 2 = PASSIVE
        // 3 = BUS_OFF
    }

    const char *getErrorStateStr()
    {
        switch (getErrorStateRaw())
        {
        case TWAI_ERROR_ACTIVE:
            return "ACTIVE";
        case TWAI_ERROR_WARNING:
            return "WARNING";
        case TWAI_ERROR_PASSIVE:
            return "PASSIVE";
        case TWAI_ERROR_BUS_OFF:
            return "BUS_OFF";
        default:
            return "UNKNOWN";
        }
    }

    bool getStatus(twai_node_status_t &out)
    {
        if (!node)
            return false;

        return twai_node_get_info(node, &out, nullptr) == ESP_OK;
    }

    bool recover()
    {
        if (!node)
            return false;

        return twai_node_recover(node);
    }
}

namespace CANRxBuffer
{
    constexpr uint16_t RX_BUF_SIZE = 1024;

    CANRxItem rxBuffer[RX_BUF_SIZE];
    volatile uint16_t rxHead = 0;
    volatile uint16_t rxTail = 0;

    volatile uint32_t dropCount = 0;
    volatile uint32_t totalFrames = 0;

    bool pushFromISR(const twai_message_t &msg)
    {
        uint16_t next = (rxHead + 1) % RX_BUF_SIZE;

        if (next == rxTail)
        {
            rxTail = (rxTail + 1) % RX_BUF_SIZE;
            dropCount = dropCount + 1;
        }

        rxBuffer[rxHead].msg = msg;
        rxBuffer[rxHead].timestamp = esp_timer_get_time();

        rxHead = next;
        totalFrames = totalFrames + 1;

        return true;
    }

    bool pop(CANRxItem &out)
    {
        if (rxTail == rxHead)
            return false;

        out = rxBuffer[rxTail];
        rxTail = (rxTail + 1) % RX_BUF_SIZE;
        return true;
    }

    int count()
    {
        return (rxHead - rxTail + RX_BUF_SIZE) % RX_BUF_SIZE;
    }

    void clear()
    {
        rxTail = rxHead;
    }

    uint32_t getDropCount() { return dropCount; }
    uint32_t getTotalFrames() { return totalFrames; }
    uint16_t getMaxUsage() { return 0; }
    void resetStats()
    {
        dropCount = 0;
        totalFrames = 0;
    }
}

namespace CANMonitor
{
    static TaskHandle_t taskHandle = nullptr;
    static volatile bool running = false;

    static StaticEventGroup_t eventGroupBuf;
    static EventGroupHandle_t eventGroup = nullptr;

#define MONITOR_TASK_STOPPED_BIT BIT0

    void task(void *)
    {
        running = true;

        static uint32_t lastPrint = 0;
        static CANHealthState lastState = CAN_HEALTH_ERROR;

        while (running)
        {
            CANHealthState h = CANDriver::getCANHealth();
            ledSetCANHealth(h);

            // 🔥 PROCESS EVENTS HERE
            CANDriver::processEvents();

            // 🔥 PROCESS ERRORS HERE
            CANDriver::processError();

            // ===== BUS OFF RECOVERY =====
            static uint32_t lastRecover = 0;

            twai_error_state_t state = CANDriver::getErrorStateRaw();

            if (state == TWAI_ERROR_BUS_OFF)
            {
                CANDriver::canTxAllowed = false; // 🚫 stop TX immediately
                CANDriver::flushTxQueue();
                if (millis() - lastRecover > 3000)
                {
                    lastRecover = millis();

                    esp_err_t result = CANDriver::recover();

                    switch (result)
                    {
                    case ESP_OK:
                        DEBUG("[CAN] RECOVERY SUCCESSFUL\n");
                        break;

                    case (ESP_ERR_INVALID_STATE):
                        DEBUG("[CAN] RECOVERY FAILED - Invalid State\n");
                        break;

                    default:
                        DEBUG("[CAN] RECOVERY FAILED - Unknown Error\n");
                        break;
                    }
                }
            }
            else if (state == TWAI_ERROR_ACTIVE)
            {
                CANDriver::canTxAllowed = true; // ✅ only re-enable here
            }
            else
            {
                lastRecover = 0; // reset timer when not bus off
            }

            // ✅ print only when state changes OR every 1s
            if (h != lastState || millis() - lastPrint > 1000)
            {
                lastPrint = millis();
                lastState = h;

                twai_node_status_t s;

                if (CANDriver::getStatus(s))
                {
                    DEBUG("[CAN] state=%s(%d) TEC=%u REC=%u TXQ=%u\n",
                          CANDriver::getErrorStateStr(),
                          s.state,
                          s.tx_error_count,
                          s.rx_error_count,
                          s.tx_queue_remaining);
                }
            }

            vTaskDelay(pdMS_TO_TICKS(50));
        }

        xEventGroupSetBits(eventGroup, MONITOR_TASK_STOPPED_BIT);
        taskHandle = nullptr;
        vTaskDelete(NULL);
    }

    void startTask()
    {
        if (!eventGroup)
            eventGroup = xEventGroupCreateStatic(&eventGroupBuf);

        if (taskHandle)
            stopTask();

        xEventGroupClearBits(eventGroup, MONITOR_TASK_STOPPED_BIT);
        running = true;

        xTaskCreatePinnedToCore(
            task,
            "can_monitor",
            4096,
            NULL,
            3, // priority
            &taskHandle,
            1);
    }

    void stopTask()
    {
        if (taskHandle)
        {
            running = false;

            xEventGroupWaitBits(
                eventGroup,
                MONITOR_TASK_STOPPED_BIT,
                pdTRUE,
                pdTRUE,
                pdMS_TO_TICKS(200));
        }
    }
}