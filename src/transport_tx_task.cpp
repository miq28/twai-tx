#include <Arduino.h>
#include "transport_tx_buffer.h"

void transportTxTask(void*)
{
    uint8_t tmp[256];

    while (1)
    {
        int n = txPop(tmp, sizeof(tmp));
        if (n > 0)
        {
            Serial.write(tmp, n);
        }
        else
        {
            vTaskDelay(1);
        }
    }
}