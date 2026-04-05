#include <Arduino.h>
#include "transport.h"
#include "command.h"

static InputContext serialCtx;

void transportInit()
{
#if defined(WEACT_STUDIO_CAN485_V1)
    Serial.begin(2000000);
#else
    Serial.begin(1000000);
#endif
    delay(100);
    Serial.println("Type 'help' for commands");
}

void transportProcess()
{
    while (Serial.available())
    {
        uint8_t b = Serial.read();
        dispatchByte(serialCtx, b);
    }
}
