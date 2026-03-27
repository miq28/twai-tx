#include "can_rx_task.h"
#include "can_rx_buffer.h"

#include <driver/twai.h>
#include <Arduino.h>

static void canRxTask(void *arg)
{
    twai_message_t msg;

    while (1)
    {
        if (twai_receive(&msg, portMAX_DELAY) == ESP_OK)
        {
            uint32_t ts = micros();

            if (!rxBufferPush(msg, ts))
            {
                // optional: overflow counter
            }
        }
    }
}

void startCanRxTask()
{
    xTaskCreatePinnedToCore(
        canRxTask,
        "can_rx",
        4096,
        NULL,
        20,     // high priority
        NULL,
        1       // core 1
    );
}