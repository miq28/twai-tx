#include "can_bus.h"
#include "config.h"
#include <Arduino.h>
#include "led_activity.h"

namespace CANDriver
{
    struct CANStatus
    {
        twai_state_t state;
        uint32_t tec;
        uint32_t rec;
    };

    static uint32_t currentBaud = 500000;
    static bool currentListenOnly = false;
    static bool driverRunning = false;

    // ===== auto-recovery =====
    static bool recoveryActive = false;
    static uint32_t recoveryStartTime = 0;
    static uint32_t degradedSince = 0;
    constexpr uint32_t RECOVERY_TIMEOUT_MS = 1000;
    constexpr uint32_t DEGRADED_TIMEOUT_MS = 2000;

    bool getTiming(uint32_t baud, twai_timing_config_t &timing)
    {
        switch (baud)
        {
        case 1000000:
            timing = TWAI_TIMING_CONFIG_1MBITS();
            return true;
        case 800000:
            timing = TWAI_TIMING_CONFIG_800KBITS();
            return true;
#if defined(WEACT_STUDIO_CAN485_V1)
        case 500000:
            // timing.brp = 16;
            // timing.tseg_1 = 7;
            // timing.tseg_2 = 2;
            // timing.sjw = 1;
            // timing.triple_sampling = false;
            // return true;
            timing = TWAI_TIMING_CONFIG_500KBITS();
            return true;
#else
        case 500000:
            timing = TWAI_TIMING_CONFIG_500KBITS();
            return true;
#endif
        case 250000:
            timing = TWAI_TIMING_CONFIG_250KBITS();
            return true;
        case 125000:
            timing = TWAI_TIMING_CONFIG_125KBITS();
            return true;
        case 100000:
            timing = TWAI_TIMING_CONFIG_100KBITS();
            return true;
        case 50000:
            timing = TWAI_TIMING_CONFIG_50KBITS();
            return true;
        case 25000:
            timing = TWAI_TIMING_CONFIG_25KBITS();
            return true;
        case 83333:
            timing.brp = 48;
            timing.tseg_1 = 15;
            timing.tseg_2 = 4;
            timing.sjw = 3;
            timing.triple_sampling = false;
            return true;
        case 33333:
            timing.brp = 120;
            timing.tseg_1 = 15;
            timing.tseg_2 = 4;
            timing.sjw = 3;
            timing.triple_sampling = false;
            return true;
        default:
            return false;
        }
    }

    static bool startDriver(uint32_t baud, bool listenOnly)
    {
        twai_general_config_t general = TWAI_GENERAL_CONFIG_DEFAULT(
            CAN_TX, CAN_RX,
            listenOnly ? TWAI_MODE_LISTEN_ONLY : TWAI_MODE_NORMAL);

        general.tx_queue_len = 32;
        general.rx_queue_len = 64;

        general.alerts_enabled = TWAI_ALERT_ALL;

        twai_timing_config_t timing;
        if (!getTiming(baud, timing))
        {
            CAN_LOG("[CAN] Unsupported baud %lu -> fallback 500k\n", baud);
            timing = TWAI_TIMING_CONFIG_500KBITS();
            baud = 500000;
        }
        else
        {
            CAN_LOG("[CAN] Init OK -> baud: %lu\n", baud);
        }

        twai_filter_config_t filter = TWAI_FILTER_CONFIG_ACCEPT_ALL();

        if (twai_driver_install(&general, &timing, &filter) != ESP_OK)
        {
            CAN_LOG("[CAN] Install failed\n");
            driverRunning = false;
            return false;
        }

        if (twai_start() != ESP_OK)
        {
            CAN_LOG("[CAN] Start failed\n");
            return false;
        }

        currentBaud = baud;
        currentListenOnly = listenOnly;
        driverRunning = true;
        CAN_LOG("[CAN] Started (%s)\n", listenOnly ? "LISTEN ONLY" : "NORMAL");
        return true;
    }

    void init(uint32_t baud, bool listenOnly)
    {
        startDriver(baud, listenOnly);
        CANEvents::startTask();
    }

    bool reinit(uint32_t baud, bool listenOnly)
    {
        CAN_LOG("[CAN] Reinit requested -> baud:%lu listen:%d\n", baud, listenOnly);

        // 1. STOP TASKS FIRST (critical)
        CANEvents::stopTask(); // <-- new
        CANRxBuffer::stopTask();

        // 2. STOP DRIVER
        twai_stop();
        driverRunning = false;

        // 3. UNINSTALL DRIVER
        twai_driver_uninstall();

        // 4. START DRIVER (fresh)
        bool ok = startDriver(baud, listenOnly);

        // 5. CLEAR BUFFERS
        CANRxBuffer::clear();

        // 6. RESTART TASKS
        CANEvents::startTask(); // <-- ADD THIS
        CANRxBuffer::startTask();

        return ok;
    }

    bool send(const twai_message_t &msg)
    {
        return twai_transmit((twai_message_t *)&msg, 0) == ESP_OK;
    }

    bool isRunning()
    {
        return driverRunning;
    }

    uint32_t getCurrentBaud()
    {
        return currentBaud;
    }

    bool isListenOnly()
    {
        return currentListenOnly;
    }

    void handleRecovery(const CANStatus &s)
    {
        static bool recoveryActive = false;

        // Step 2: wait until recovery finished
        if (recoveryActive && s.state == TWAI_STATE_STOPPED)
        {
            CAN_LOG("[CAN] Recovery complete → restart\n");
            twai_start();
            recoveryActive = false;
        }

        // Step 3: clear flag when running again (safety)
        if (s.state == TWAI_STATE_RUNNING)
        {
            recoveryActive = false;
        }
    }

    CANHealthState currentHealth = CAN_HEALTH_OK;

    CANHealthState getCANHealth()
    {
        twai_status_info_t s;

        if (twai_get_status_info(&s) != ESP_OK)
            return CAN_HEALTH_ERROR;

        return getHealthFromState(
            s.state,
            s.tx_error_counter,
            s.rx_error_counter);
    }

    twai_state_t getStateRaw()
    {
        twai_status_info_t s;
        if (twai_get_status_info(&s) == ESP_OK)
            return s.state;

        return TWAI_STATE_STOPPED; // safe fallback
    }

    const char *getStateStr(twai_state_t s)
    {
        switch (s)
        {
        case TWAI_STATE_STOPPED:
            return "STOPPED";
        case TWAI_STATE_RUNNING:
            return "RUNNING";
        case TWAI_STATE_BUS_OFF:
            return "BUS_OFF";
        case TWAI_STATE_RECOVERING:
            return "RECOVERING";
        default:
            return "UNKNOWN";
        }
    }

    const char *getStateStr()
    {
        return getStateStr(getStateRaw());
    }

    CANHealthState getHealthFromState(twai_state_t s,
                                      uint32_t tec,
                                      uint32_t rec)
    {
        if (s == TWAI_STATE_BUS_OFF)
            return CAN_HEALTH_BUS_OFF;

        if (tec > 127 || rec > 127)
            return CAN_HEALTH_ERROR;

        if (tec > 96 || rec > 96)
            return CAN_HEALTH_DEGRADED;

        return CAN_HEALTH_OK;
    }

    bool getStatus(CANStatus &out)
    {
        twai_status_info_t s;
        if (twai_get_status_info(&s) != ESP_OK)
            return false;

        out.state = s.state;
        out.tec = s.tx_error_counter;
        out.rec = s.rx_error_counter;
        return true;
    }

    CANHealthState getHealth(const CANStatus &s)
    {
        return getHealthFromState(s.state, s.tec, s.rec);
    }

    void logStatus(const CANStatus &s)
    {
        const char *state_str = getStateStr(s.state);

        CAN_LOG("[CAN] %s TEC=%d REC=%d\n",
                state_str,
                s.tec,
                s.rec);
    }
}

namespace CANRxBuffer
{
    // ===== CONTROL =====
    static TaskHandle_t rxTaskHandle = nullptr;
    static volatile bool rxRunning = false;

    static StaticEventGroup_t rxEventGroupBuf;
    static EventGroupHandle_t rxEventGroup = nullptr;

#define RX_TASK_STOPPED_BIT BIT0

    // ===== BUFFER =====
    constexpr uint16_t RX_BUF_SIZE = 1024;

    CANRxItem rxBuffer[RX_BUF_SIZE];
    volatile uint16_t rxHead = 0;
    volatile uint16_t rxTail = 0;

    volatile uint32_t dropCount = 0;
    volatile uint32_t totalFrames = 0;
    volatile uint16_t maxUsage = 0;

    // FPS
    static uint32_t lastTotalFrames = 0;
    static uint32_t lastFpsTime = 0;
    static uint32_t rxFps = 0;

    // ===== PUSH =====
    bool push(const twai_message_t &msg, uint32_t ts)
    {
        uint16_t next = (rxHead + 1) % RX_BUF_SIZE;

        uint16_t used = (rxHead - rxTail + RX_BUF_SIZE) % RX_BUF_SIZE;
        if (used > maxUsage)
            maxUsage = used;

        if (next == rxTail)
        {
            rxTail = (rxTail + 1) % RX_BUF_SIZE;
            dropCount = dropCount + 1;
        }

        rxBuffer[rxHead].msg = msg;
        rxBuffer[rxHead].timestamp = ts;
        rxHead = next;

        totalFrames = totalFrames + 1;
        return true;
    }

    // ===== TASK =====
    void task(void *)
    {
        twai_message_t msg;

        rxRunning = true;

        while (rxRunning)
        {
            // ---- RECEIVE ONLY (critical path) ----
            if (twai_receive(&msg, pdMS_TO_TICKS(10)) == ESP_OK)
            {
                (void)push(msg, micros());
                ledRxEvent();
            }
        }

        // =========================================================
        // CLEAN SHUTDOWN
        // =========================================================
        xEventGroupSetBits(rxEventGroup, RX_TASK_STOPPED_BIT);

        rxTaskHandle = nullptr;
        vTaskDelete(NULL);
    }

    // ===== POP =====
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

    // ===== START =====
    void startTask()
    {
        if (!rxEventGroup)
        {
            rxEventGroup = xEventGroupCreateStatic(&rxEventGroupBuf);
        }

        if (rxTaskHandle)
        {
            stopTask();
        }

        xEventGroupClearBits(rxEventGroup, RX_TASK_STOPPED_BIT);
        rxRunning = true;

        xTaskCreatePinnedToCore(
            task,
            "can_rx",
            4096,
            NULL,
            16,
            &rxTaskHandle,
            1);
    }

    // ===== STOP (SAFE) =====
    void stopTask()
    {
        if (rxTaskHandle)
        {
            rxRunning = false;

            // wait until task fully exits
            xEventGroupWaitBits(
                rxEventGroup,
                RX_TASK_STOPPED_BIT,
                pdTRUE,
                pdTRUE,
                pdMS_TO_TICKS(200));
        }
    }

    // ===== UTIL =====
    void clear()
    {
        rxTail = rxHead;
    }

    uint32_t getDropCount() { return dropCount; }
    uint32_t getTotalFrames() { return totalFrames; }
    uint16_t getMaxUsage() { return maxUsage; }

    uint32_t getRxFps()
    {
        uint32_t now = millis();

        if (now - lastFpsTime >= 200)
        {
            uint32_t current = totalFrames;
            rxFps = current - lastTotalFrames;

            lastTotalFrames = current;
            lastFpsTime = now;
        }

        return rxFps;
    }

    void resetStats()
    {
        dropCount = 0;
        totalFrames = 0;
        maxUsage = 0;
    }
}

// namespace
// {
//     CANHealthState currentHealth = CAN_HEALTH_OK;
// }

namespace CANEvents
{
    static twai_state_t last_state = TWAI_STATE_STOPPED;

    static TaskHandle_t evtTaskHandle = nullptr;
    static volatile bool evtRunning = false;

    void process()
    {
        uint32_t now = millis();

        // -------------------------
        // ALERT EVENTS (non-blocking)
        // -------------------------
        uint32_t alerts;
        if (twai_read_alerts(&alerts, 0) == ESP_OK && alerts)
        {
            if (alerts & TWAI_ALERT_BUS_OFF)
            {
                CAN_LOG("[CAN EVT] BUS OFF → start recovery\n");
                if (twai_initiate_recovery() == ESP_OK)
                {
                    // mark active via static inside handleRecovery
                }
            }

            if (alerts & TWAI_ALERT_ERR_PASS)
            {
                CAN_LOG("[CAN EVT] ERROR PASSIVE\n");
            }
        }

        // -------------------------
        // SINGLE STATUS FETCH
        // -------------------------
        CANDriver::CANStatus st;
        if (!CANDriver::getStatus(st))
            return;

        // -------------------------
        // RECOVERY (state-based)
        // -------------------------
        CANDriver::handleRecovery(st);

        // -------------------------
        // LED (health)
        // -------------------------
        CANHealthState h = CANDriver::getHealth(st);
        ledSetCANHealth(h);

        // -------------------------
        // LOG (throttled)
        // -------------------------
        static uint32_t lastPrint = 0;
        if (now - lastPrint > 200)
        {
            lastPrint = now;
            CANDriver::logStatus(st);
        }
    }

    void canEventTask(void *)
    {
        evtRunning = true;

        while (evtRunning)
        {
            CANEvents::process();
            vTaskDelay(1);
        }

        evtTaskHandle = nullptr;
        vTaskDelete(NULL);
    }

    void startTask()
    {
        if (evtTaskHandle)
            return;

        xTaskCreatePinnedToCore(
            canEventTask,
            "can_evt",
            3072,
            NULL,
            10,
            &evtTaskHandle, // <-- important
            1);
    }

    void stopTask()
    {
        if (evtTaskHandle)
        {
            evtRunning = false;
            vTaskDelay(pdMS_TO_TICKS(10)); // allow exit
        }
    }
}