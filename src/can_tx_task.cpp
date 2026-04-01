#include <Arduino.h>
#include <driver/twai.h>
#include "can_tx_buffer.h"

void canTxTask(void*)
{
    twai_message_t msg;

    while (1)
    {
        if (canTxPop(msg))
        {
            twai_transmit(&msg, portMAX_DELAY);
        }
        else
        {
            vTaskDelay(1);
        }
    }
}