#include "can_bus.h"
#include "config.h"
#include <Arduino.h>

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
        CAN_LOG("[CAN] Reinit requested -> baud:%lu listen:%d\n", baud, listenOnly);
        twai_stop();
        driverRunning = false;
        twai_driver_uninstall();

        bool ok = startDriver(baud, listenOnly);

        // 🔥 clear stale frames from previous driver instance
        CANRxBuffer::clear();

        // 🔥 CRITICAL FIX: restart RX task
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
}

namespace CANRxBuffer
{
    TaskHandle_t rxTaskHandle = nullptr;
    constexpr uint16_t RX_BUF_SIZE = 1024;

    CANRxItem rxBuffer[RX_BUF_SIZE];
    volatile uint16_t rxHead = 0;
    volatile uint16_t rxTail = 0;

    volatile uint32_t dropCount = 0; // overflow counter
    volatile uint32_t totalFrames = 0;
    volatile uint16_t maxUsage = 0;

    bool push(const twai_message_t &msg, uint32_t ts)
    {
        uint16_t next = (rxHead + 1) % RX_BUF_SIZE;

        // hitung usage sebelum insert
        uint16_t used = (rxHead - rxTail + RX_BUF_SIZE) % RX_BUF_SIZE;
        if (used > maxUsage)
            maxUsage = used;

        if (next == rxTail) // silently drop frame,
        {
            dropCount++; // 🔥 overflow counter
            return false;
        }

        rxBuffer[rxHead].msg = msg;
        rxBuffer[rxHead].timestamp = ts;
        rxHead = next;

        totalFrames++;
        return true;
    }

    void task(void *)
    {
        twai_message_t msg;

        while (1)
        {
            if (twai_receive(&msg, portMAX_DELAY) == ESP_OK)
            {
                (void)push(msg, micros());
            }
        }
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

    void startTask()
    {
        if (rxTaskHandle)
        {
            vTaskDelete(rxTaskHandle);
            rxTaskHandle = nullptr;
        }

        xTaskCreatePinnedToCore(task, "can_rx", 4096, NULL, 16, NULL, 1);
    }

    void clear()
    {
        rxTail = rxHead;
    }
}

uint32_t CANRxBuffer::getDropCount() { return dropCount; }
uint32_t CANRxBuffer::getTotalFrames() { return totalFrames; }
uint16_t CANRxBuffer::getMaxUsage() { return maxUsage; }

void CANRxBuffer::resetStats()
{
    dropCount = 0;
    totalFrames = 0;
    maxUsage = 0;
}
