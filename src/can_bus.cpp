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

    // Globals to store state
    volatile twai_error_state_t g_errorState = TWAI_ERROR_ACTIVE;
    volatile bool g_errorStateChanged = false;
    volatile bool g_driverAlive = false;
    volatile bool g_driverStateChanged = false;

    static uint32_t currentBaud = 500000;
    static bool currentListenOnly = false;
    static bool driverRunning = false;

    static twai_node_handle_t node = nullptr;

    static volatile bool recoverRequested = false;

    // ===== EVENT QUEUE =====
    enum EventType : uint8_t
    {
        EVT_RX,
        EVT_STATE,
        EVT_ERROR
    };

    struct Event
    {
        EventType type;

        union
        {
            struct
            {
                twai_message_t msg;
                uint32_t ts;
            } rx;

            struct
            {
                twai_error_state_t old_sta;
                twai_error_state_t new_sta;
            } state;

            struct
            {
                // uint32_t err_code;
                // uint8_t tx_err;
                // uint8_t rx_err;
                uint32_t flags; // from err_flags.val
            } error;
        };
    };

    static constexpr uint32_t EVT_SIZE = 128;
    static Event evtBuf[EVT_SIZE];
    static volatile uint32_t evtHead = 0;
    static volatile uint32_t evtTail = 0;

    static inline bool IRAM_ATTR pushEvent(const Event &e)
    {
        uint32_t h = evtHead;
        uint32_t next = (h + 1) & (EVT_SIZE - 1);

        if (next == evtTail)
            return false;

        evtBuf[h] = e;
        evtHead = next;
        return true;
    }

    bool popEvent(Event &e)
    {
        uint32_t t = evtTail;

        if (t == evtHead)
            return false;

        e = evtBuf[t];
        evtTail = (t + 1) & (EVT_SIZE - 1);
        return true;
    }

    const char *stateToStr(twai_error_state_t s)
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
            return "UNKNOWN";
        }
    }



    void requestRecover()
    {
        recoverRequested = true;
    }

    void loop()
    {
        // ===== PROCESS CAN EVENTS =====
        CANDriver::Event ev;

        while (CANDriver::popEvent(ev))
        {
            switch (ev.type)
            {
            case CANDriver::EVT_RX:
                // move ISR work here
                CANRxBuffer::pushFromISR(ev.rx.msg); // now safe
                ledRxEvent();                        // now safe
                break;

            case CANDriver::EVT_STATE:
            {
                auto s = ev.state.new_sta;

                CANDriver::g_errorState = s;
                CANDriver::g_errorStateChanged = true;

                if (s == TWAI_ERROR_BUS_OFF)
                {
                    CANDriver::g_driverAlive = false;
                    CANDriver::g_driverStateChanged = true;

                    CANDriver::requestRecover(); // safe trigger

                    // SAFE here (not ISR anymore)
                    // IMPORTANT: node is private → see note below
                }

                if (ev.state.old_sta == TWAI_ERROR_BUS_OFF &&
                    s == TWAI_ERROR_ACTIVE)
                {
                    CANDriver::g_driverAlive = true;
                    CANDriver::g_driverStateChanged = true;
                }
                break;
            }

            case CANDriver::EVT_ERROR:
            {
                uint32_t f = ev.error.flags;

                // ===== decode flags =====
                if (f & (1 << 0))
                    DEBUG("[CAN] ARB LOST\n");
                if (f & (1 << 1))
                    DEBUG("[CAN] BIT ERROR\n");
                if (f & (1 << 2))
                    DEBUG("[CAN] FORM ERROR\n");
                if (f & (1 << 3))
                    DEBUG("[CAN] STUFF ERROR\n");
                if (f & (1 << 4))
                    DEBUG("[CAN] ACK ERROR\n");

                // ===== get counters =====
                twai_node_status_t s;
                if (CANDriver::getStatus(s))
                {
                    static uint32_t lastPrint = 0;

                    if (millis() - lastPrint > 200)
                    {
                        lastPrint = millis();

                        DEBUG("[CAN] state=%d tx_err=%u rx_err=%u\n",
                              stateToStr(s.state),
                              s.tx_error_count,
                              s.rx_error_count);
                    }
                }

                break;
            }
            }
        }

        // recover if requested (event-driven, safe context)
        if (recoverRequested)
        {
            recoverRequested = false;
            twai_node_recover(node);
        }
    }

    bool getStatus(twai_node_status_t &out)
    {
        if (!node)
            return false;

        if (twai_node_get_info(node, &out, nullptr) != ESP_OK)
            return false;

        return true;
    }

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

            // event-driven state update
            Event e{};
            e.type = EVT_RX;
            e.rx.msg = msg;
            e.rx.ts = esp_timer_get_time();

            pushEvent(e);
        }

        return false;
    }

    static bool on_error(twai_node_handle_t,
                         const twai_error_event_data_t *edata,
                         void *)
    {
        Event e{};
        e.type = EVT_ERROR;
        e.error.flags = edata->err_flags.val;

        pushEvent(e);
        return false;
    }

    static bool on_state_change(twai_node_handle_t,
                                const twai_state_change_event_data_t *edata,
                                void *)
    {
        // event-driven state update
        Event e{};
        e.type = EVT_STATE;
        e.state.old_sta = edata->old_sta;
        e.state.new_sta = edata->new_sta;

        pushEvent(e);

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
            .tx_queue_depth = 64,
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
            g_driverAlive = false;
            g_driverStateChanged = true;
            CAN_LOG("[CAN] start failed\n");
            return false;
        }

        currentBaud = baud;
        currentListenOnly = listenOnly;
        driverRunning = true;

        g_driverAlive = true;
        g_driverStateChanged = true;

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
    }

    bool sendAsync(const twai_message_t &msg)
    {
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

        if (node)
        {
            twai_node_disable(node);
            twai_node_delete(node);
            node = nullptr;
        }

        bool ok = startDriver(baud, listenOnly);

        // CANRxBuffer::clear();
        // CANRxBuffer::startTask();

        return ok;
    }

    bool send(const twai_message_t &msg)
    {
        if (!node)
            return false;

        uint8_t dlc = msg.data_length_code;

        if (dlc > 8)
        {
            static uint32_t lastWarn = 0;
            if (millis() - lastWarn > 1000)
            {
                lastWarn = millis();
                DEBUG("[WARN] DLC corrupted: %u\n", dlc);
            }
            dlc = 8;
        }

        uint8_t buf[8];

        twai_frame_t frame = {};
        frame.header.id = msg.identifier;
        frame.header.ide = msg.extd;
        frame.header.rtr = msg.rtr;
        frame.buffer = buf;
        frame.buffer_len = dlc;

        if (!msg.rtr && dlc > 0)
        {
            memcpy(buf, msg.data, dlc);
        }

        esp_err_t err = twai_node_transmit(node, &frame, 0);

        if (err != ESP_OK)
        {
            // 🔥 OPTIONAL: mark driver issue (event-driven LED will react)
            CANDriver::g_driverAlive = false;
            CANDriver::g_driverStateChanged = true;
            return false;
        }

        return true;
    }

    uint32_t getCurrentBaud() { return currentBaud; }
    bool isListenOnly() { return currentListenOnly; }
}

namespace CANRxBuffer
{
    static TaskHandle_t rxTaskHandle = nullptr;
    static volatile bool rxRunning = false;

    static StaticEventGroup_t rxEventGroupBuf;
    static EventGroupHandle_t rxEventGroup = nullptr;

#define RX_TASK_STOPPED_BIT BIT0

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

    // void task(void *)
    // {
    //     rxRunning = true;

    //     while (rxRunning)
    //     {
    //         vTaskDelay(pdMS_TO_TICKS(50));
    //     }

    //     xEventGroupSetBits(rxEventGroup, RX_TASK_STOPPED_BIT);
    //     rxTaskHandle = nullptr;
    //     vTaskDelete(NULL);
    // }

    // void startTask()
    // {
    //     if (!rxEventGroup)
    //         rxEventGroup = xEventGroupCreateStatic(&rxEventGroupBuf);

    //     if (rxTaskHandle)
    //         stopTask();

    //     xEventGroupClearBits(rxEventGroup, RX_TASK_STOPPED_BIT);
    //     rxRunning = true;

    //     xTaskCreatePinnedToCore(task, "can_rx", 4096, NULL, 16, &rxTaskHandle, 1);
    // }

    // void stopTask()
    // {
    //     if (rxTaskHandle)
    //     {
    //         rxRunning = false;

    //         xEventGroupWaitBits(
    //             rxEventGroup,
    //             RX_TASK_STOPPED_BIT,
    //             pdTRUE,
    //             pdTRUE,
    //             pdMS_TO_TICKS(200));
    //     }
    // }

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