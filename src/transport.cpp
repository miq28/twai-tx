#include <Arduino.h>
#include "transport.h"
#include "command.h"
#include "debug.h"

static InputContext serialCtx;

void transportInit()
{
#if defined(WEACT_STUDIO_CAN485_V1)
    Serial.begin(2000000);
#else
    Serial.begin(1000000);
#endif
    delay(100);
    DEBUG_PRINTLN("Type 'help' for commands");
}

void transportProcess()
{
    // ===== USB SERIAL =====
    while (Serial.available())
    {
        uint8_t b = Serial.read();
        dispatchByte(serialCtx, b);
    }

    // ===== RS485 =====
    while (RS485.available())
    {
        uint8_t b = RS485.read();
        dispatchByte(serialCtx, b);
    }
}



// ===== TX handing
#define TX_BUF_SIZE 2048

static uint8_t txBuf[TX_BUF_SIZE];
static volatile uint16_t txHead = 0;
static volatile uint16_t txTail = 0;

static inline bool txPush(uint8_t b)
{
    uint16_t next = (txHead + 1) % TX_BUF_SIZE;
    if (next == txTail) return false; // full → drop
    txBuf[txHead] = b;
    txHead = next;
    return true;
}

void transportWrite(const uint8_t* data, size_t len)
{
    for (size_t i = 0; i < len; i++)
    {
        txPush(data[i]); // drop if full
    }
}

void transportFlush()
{
    while (txTail != txHead)
    {
        uint8_t b = txBuf[txTail];
        txTail = (txTail + 1) % TX_BUF_SIZE;

        // current behavior: USB only
        Serial.write(b);
    }
}
