#include "can_bus.h"
#include "config.h"
#include <Arduino.h>
#include "led_activity.h"

namespace CANDriver
{
    static uint32_t currentBaud = 500000;
    static bool currentListenOnly = false;
    static bool driverRunning = false;

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
        case 500000:
            timing = TWAI_TIMING_CONFIG_500KBITS();
            return true;
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
    }

    bool reinit(uint32_t baud, bool listenOnly)
    {
        // CAN_LOG("[CAN] Reinit requested -> baud:%lu listen:%d\n", baud, listenOnly);
        // twai_stop();
        // driverRunning = false;
        // twai_driver_uninstall();
        // bool ok = startDriver(baud, listenOnly);
        // // 🔥 clear stale frames from previous driver instance
        // CANRxBuffer::clear();
        // // 🔥 CRITICAL FIX: restart RX task
        // CANRxBuffer::startTask();

        CAN_LOG("[CAN] Reinit requested -> baud:%lu listen:%d\n", baud, listenOnly);
        CANRxBuffer::stopTask();
        twai_stop();
        driverRunning = false;
        twai_driver_uninstall();
        bool ok = startDriver(baud, listenOnly);
        CANRxBuffer::clear();
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

    CANHealthState currentHealth = CAN_HEALTH_OK;

    CANHealthState getCANHealth()
    {
        twai_status_info_t s;

        if (twai_get_status_info(&s) != ESP_OK)
            return CAN_HEALTH_ERROR;

        // Priority order (highest severity first)

        if (s.state == TWAI_STATE_BUS_OFF)
        {
            currentHealth = CAN_HEALTH_BUS_OFF;
        }
        else if (s.tx_error_counter > 255)
        {
            // shouldn't normally happen unless unstable
            currentHealth = CAN_HEALTH_ERROR;
        }
        else if (s.tx_error_counter > 127 || s.rx_error_counter > 127)
        {
            currentHealth = CAN_HEALTH_ERROR;
        }
        else if (s.tx_error_counter > 0 || s.rx_error_counter > 0)
        {
            currentHealth = CAN_HEALTH_DEGRADED;
        }
        else
        {
            currentHealth = CAN_HEALTH_OK;
        }

        return currentHealth;
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
            dropCount++;
        }

        rxBuffer[rxHead].msg = msg;
        rxBuffer[rxHead].timestamp = ts;
        rxHead = next;

        totalFrames++;
        return true;
    }

    // ===== TASK =====
    void task(void *)
    {
        twai_message_t msg;

        rxRunning = true;

        CANHealthState lastHealth = CAN_HEALTH_OK;

        uint32_t lastHealthPrint = 0;

        while (rxRunning)
        {
            uint32_t now = millis();

            // =========================================================
            // CAN HEALTH (clean, stable)
            // =========================================================
            CANHealthState h = CANDriver::getCANHealth();

            // update LED (always safe, very cheap)
            ledSetCANHealth(h);

            // debug print (throttled)
            if (now - lastHealthPrint > 200)
            {
                lastHealthPrint = now;

                switch (h)
                {
                case CAN_HEALTH_OK:
                    DEBUG_PRINTLN("[CAN] OK");
                    break;

                case CAN_HEALTH_DEGRADED:
                    DEBUG_PRINTLN("[CAN] DEGRADED");
                    break;

                case CAN_HEALTH_ERROR:
                    DEBUG_PRINTLN("[CAN] ERROR");
                    break;

                case CAN_HEALTH_BUS_OFF:
                    DEBUG_PRINTLN("[CAN] BUS OFF");
                    break;
                }
            }

            // =========================================================
            // RECEIVE (short timeout = responsive + safe)
            // =========================================================
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

    /*
    // ===== TASK =====
    void task(void *)
    {
        twai_message_t msg;

        rxRunning = true;

        while (rxRunning)
        {
            static uint32_t lastHealthPrint = 0;

            if (millis() - lastHealthPrint > 200)
            {
                lastHealthPrint = millis();

                CANHealthState h = CANDriver::getCANHealth();

                switch (h)
                {
                case CAN_HEALTH_OK:
                    DEBUG_PRINTLN("[CAN] OK");
                    break;

                case CAN_HEALTH_DEGRADED:
                    DEBUG_PRINTLN("[CAN] DEGRADED");
                    break;

                case CAN_HEALTH_ERROR:
                    DEBUG_PRINTLN("[CAN] ERROR");
                    break;

                case CAN_HEALTH_BUS_OFF:
                    DEBUG_PRINTLN("[CAN] BUS OFF");
                    break;
                }
            }

            // --- alerts (non-blocking)
            uint32_t alerts = 0;
            if (twai_read_alerts(&alerts, 0) == ESP_OK && alerts)
            {
                // if (alerts & (TWAI_ALERT_ERR_PASS |
                // TWAI_ALERT_BUS_ERROR |
                // TWAI_ALERT_RX_QUEUE_FULL |
                // TWAI_ALERT_TX_FAILED |
                // TWAI_ALERT_BUS_OFF))
                // {
                // ledCanErrorEvent();
                // }

                // if (alerts & TWAI_ALERT_ERR_PASS)
                //     DEBUG_PRINTLN("[CAN] Alert: Error Passive");

                // if (alerts & TWAI_ALERT_BUS_ERROR)
                //     DEBUG_PRINTLN("[CAN] Alert: Bus Error");

                // if (alerts & TWAI_ALERT_BUS_OFF)
                //     DEBUG_PRINTLN("[CAN] Alert: Bus Off");

                // if (alerts & TWAI_ALERT_RX_QUEUE_FULL)
                //     DEBUG_PRINTLN("[CAN] Alert: RX Queue Full");

                // if (alerts & TWAI_ALERT_TX_FAILED)
                //     DEBUG_PRINTLN("[CAN] Alert: TX Failed");

                // if (alerts & TWAI_ALERT_ARB_LOST)
                //     DEBUG_PRINTLN("[CAN] Alert: Arbitration Lost");

                ledCanErrorEvent();
            }

            // --- receive (short timeout = safe shutdown)
            if (twai_receive(&msg, pdMS_TO_TICKS(10)) == ESP_OK)
            {
                (void)push(msg, micros());
                ledRxEvent();
            }
        }

        // signal fully stopped
        xEventGroupSetBits(rxEventGroup, RX_TASK_STOPPED_BIT);

        rxTaskHandle = nullptr;
        vTaskDelete(NULL);
    }
        */

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
