#include "can_bus.h"
#include "config.h"
#include <Arduino.h>
#include "led_activity.h"

#include "esp_twai.h"
#include "esp_twai_onchip.h"

namespace CANDriver
{
    static uint32_t currentBaud = 500000;
    static bool currentListenOnly = false;
    static bool driverRunning = false;

    static twai_node_handle_t node = nullptr;

    // =========================================================
    // RX ISR CALLBACK
    // =========================================================
    // static bool on_rx_done(twai_node_handle_t n,
    //                        const twai_rx_done_event_data_t *edata,
    //                        void *)
    // {
    //     twai_frame_t frame;

    //     if (twai_node_receive_from_isr(n, &frame) == ESP_OK)
    //     {
    //         twai_message_t msg = {};
    //         msg.identifier = frame.header.id;
    //         msg.extd = frame.header.ide;
    //         msg.rtr = frame.header.rtr;
    //         msg.data_length_code = frame.buffer_len;

    //         memcpy(msg.data, frame.buffer, frame.buffer_len);

    //         CANRxBuffer::pushFromISR(msg);
    //     }

    //     return false;
    // }

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
        }

        return false;
    }

    static bool on_error(twai_node_handle_t,
                         const twai_error_event_data_t *,
                         void *)
    {
        return false;
    }

    static bool on_state_change(twai_node_handle_t,
                                const twai_state_change_event_data_t *edata,
                                void *)
    {
        if (edata->new_sta == TWAI_ERROR_BUS_OFF)
        {
            twai_node_recover(node);
        }
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
            .tx_queue_depth = 32,
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

    void init(uint32_t baud, bool listenOnly)
    {
        startDriver(baud, listenOnly);
    }

    bool reinit(uint32_t baud, bool listenOnly)
    {
        CANRxBuffer::stopTask();

        if (node)
        {
            twai_node_disable(node);
            twai_node_delete(node);
            node = nullptr;
        }

        bool ok = startDriver(baud, listenOnly);

        CANRxBuffer::clear();
        CANRxBuffer::startTask();

        return ok;
    }

    bool send(const twai_message_t &msg)
    {
        if (!CANDriver::isRunning())
            return false;

        if (!node)
            return false;

        if (msg.data_length_code > 8)
            return false;

        if (msg.data_length_code > 0 && msg.data == nullptr)
            return false;

        twai_frame_t frame = {};
        frame.header.id = msg.identifier;
        frame.header.ide = msg.extd;
        frame.header.rtr = msg.rtr;
        frame.buffer = (uint8_t *)msg.data;
        frame.buffer_len = msg.data_length_code;

        return twai_node_transmit(node, &frame, 0) == ESP_OK;
    }

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
        rxBuffer[rxHead].timestamp = 0;

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

    void task(void *)
    {
        rxRunning = true;

        while (rxRunning)
        {
            CANHealthState h = CANDriver::getCANHealth();
            ledSetCANHealth(h);

            vTaskDelay(pdMS_TO_TICKS(50));
        }

        xEventGroupSetBits(rxEventGroup, RX_TASK_STOPPED_BIT);
        rxTaskHandle = nullptr;
        vTaskDelete(NULL);
    }

    void startTask()
    {
        if (!rxEventGroup)
            rxEventGroup = xEventGroupCreateStatic(&rxEventGroupBuf);

        if (rxTaskHandle)
            stopTask();

        xEventGroupClearBits(rxEventGroup, RX_TASK_STOPPED_BIT);
        rxRunning = true;

        xTaskCreatePinnedToCore(task, "can_rx", 4096, NULL, 16, &rxTaskHandle, 1);
    }

    void stopTask()
    {
        if (rxTaskHandle)
        {
            rxRunning = false;

            xEventGroupWaitBits(
                rxEventGroup,
                RX_TASK_STOPPED_BIT,
                pdTRUE,
                pdTRUE,
                pdMS_TO_TICKS(200));
        }
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