#include "tx_pipe.h"
#include "transport.h"
#include <Arduino.h>
#include "net_manager.h"

namespace TxPipe
{
    constexpr uint16_t BUF_SIZE = 8192;

    static uint8_t buf[BUF_SIZE];
    static volatile uint16_t head = 0;
    static volatile uint16_t tail = 0;

    static TaskHandle_t taskHandle = nullptr;

    bool push(const uint8_t *data, uint16_t len)
    {
        if (len >= BUF_SIZE)
            return false;

        uint16_t freeSpace = (tail - head - 1 + BUF_SIZE) % BUF_SIZE;
        if (len > freeSpace)
            return false; // drop (backpressure)

        for (uint16_t i = 0; i < len; i++)
        {
            buf[head] = data[i];
            head = (head + 1) % BUF_SIZE;
        }
        return true;
    }

    void task(void *)
    {
        static uint8_t tmp[256];

        while (true)
        {
            uint16_t available = (head - tail + BUF_SIZE) % BUF_SIZE;

            if (available == 0)
            {
                vTaskDelay(1);
                continue;
            }

            // 🔥 HARD LIMIT (no burst)
            const uint16_t CHUNK = 128;

            uint16_t sendLen = available;
            if (sendLen > CHUNK)
                sendLen = CHUNK;

            // copy
            for (uint16_t i = 0; i < sendLen; i++)
            {
                tmp[i] = buf[tail];
                tail = (tail + 1) % BUF_SIZE;
            }

            // 🔥 ONLY send if TCP is ready + heap safe
            if (ESP.getFreeHeap() > 12000 && netClientConnected())
            {
                transportWrite(tmp, sendLen);
            }

            // 🔥 pacing (CRITICAL)
            vTaskDelay(2);
        }
    }

    void init()
    {
        xTaskCreatePinnedToCore(
            task,
            "tx_pipe",
            4096,
            NULL,
            12,
            &taskHandle,
            1);
    }
}

TaskHandle_t TxPipe::getTaskHandle()
{
    return taskHandle;
}