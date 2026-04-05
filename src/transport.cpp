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
