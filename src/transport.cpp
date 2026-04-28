#include <Arduino.h>
#include "transport.h"
#include "command.h"
#include "debug.h"
#include "net_manager.h"
#include "app_mode.h"
#include "gvret_mode.h"
#include "config.h"

static InputContext serialCtx;

void transportInit()
{
    // Serial.begin(1000000); -> moved to top of main setup()

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
    if (settings.mode == MODE_SAVVYCAN)
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
        size_t totalSent = 0;

        while (totalSent < len)
        {
            size_t sent = netWrite(data + totalSent, len - totalSent);

            if (sent == 0)
            {
                // 🔥 STOP — jangan kirim sisa (biar tidak corrupt)
                return;
            }

            totalSent += sent;
        }
    }
    else
    {
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
