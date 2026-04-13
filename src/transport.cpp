#include <Arduino.h>
#include "transport.h"
#include "command.h"
#include "debug.h"
#include "net_manager.h"
#include "app_mode.h"
#include "gvret_mode.h"

static InputContext serialCtx;

void transportInit()
{
#if defined(WEACT_STUDIO_CAN485_V1)
    Serial.begin(2000000);
#else
    Serial.begin(1000000);
#endif
    delay(100);

    netInit();

    DEBUG_PRINTLN("Type 'help' for commands");
}

void transportDispatchByte(uint8_t b)
{
    dispatchByte(serialCtx, b);
}

void transportDispatchBuffer(const uint8_t *data, size_t len)
{
    // 🔥 fast path for GVRET (binary mode)
    if (appState.mode == MODE_SAVVYCAN)
    {
        for (size_t i = 0; i < len; i++)
        {
            processIncomingByte(data[i]);
        }
        return;
    }

    // fallback (CLI / analyzer)
    for (size_t i = 0; i < len; i++)
    {
        dispatchByte(serialCtx, data[i]);
    }
}

void transportProcess()
{
    uint8_t buf[64];

    // ===== USB SERIAL =====
    while (Serial.available())
    {
        uint8_t b = Serial.read();
        transportDispatchByte(b);
    }

    // ===== RS485 =====
    while (RS485.available())
    {
        uint8_t b = RS485.read();
        transportDispatchByte(b);
    }

    netLoop();
}

// void transportWrite(const uint8_t *data, size_t len)
// {
//     for (size_t i = 0; i < len; i++)
//     {
//         txPush(data[i]); // drop if full
//     }
// }

void transportWrite(const uint8_t *data, size_t len)
{
    if (netClientConnected())
    {
        // best effort send
        size_t sent = netWrite(data, len);

        // 🔥 if TCP buffer is full → drop frame (do NOT block)
        
        // if (sent != len)
        // {
        //     // 🔥 DROP WHOLE FRAME (never retry partial)
        //     return;
        // }

        if (sent == 0)
        {
            return; // only drop if nothing sent
        }
    }
    else
    {
        // ===== SERIAL =====
        Serial.write(data, len);
    }
}

void transportWritePriority(const uint8_t *data, size_t len)
{
    if (netClientConnected())
    {
        netWrite(data, len); // 🔥 always send (no drop)
    }
    else
    {
        Serial.write(data, len);
    }
}

void transportFlush()
{
    // no-op (kept for compatibility)
}
