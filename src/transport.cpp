#include <Arduino.h>
#include "input_dispatcher.h"

#if defined(WEACT_STUDIO_CAN485_V1)
#define TX_SIZE 1024  
#else
#define TX_SIZE 4096
#endif

static uint8_t buf[TX_SIZE];
static volatile uint16_t head = 0, tail = 0;

static InputContext ctx;

// ================= TX =================
bool transportSend(const uint8_t *data, int len)
{
    for (int i = 0; i < len; i++)
    {
        uint16_t next = (head + 1) % TX_SIZE;
        if (next == tail)
            return false;
        buf[head] = data[i];
        head = next;
    }
    return true;
}

// ================= TX TASK =================
static void transportTask(void *)
{
    uint8_t tmp[256];

    while (1)
    {
        int n = 0;

        while (tail != head && n < 256)
        {
            tmp[n++] = buf[tail];
            tail = (tail + 1) % TX_SIZE;
        }

        if (n > 0)
            Serial.write(tmp, n);
        else
            vTaskDelay(1);
    }
}

// ================= INIT =================
void transportInit()
{
    Serial.begin(1000000);
    xTaskCreatePinnedToCore(transportTask, "tx", 4096, NULL, 8, NULL, 0);
}

// ================= RX =================
void transportProcess()
{
    int budget = 64;

    while (budget-- && Serial.available())
    {
        dispatchByte(ctx, Serial.read());
    }
}