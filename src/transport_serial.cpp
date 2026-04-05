#include <Arduino.h>
#include "transport_serial.h"
#include "input_dispatcher.h"

static InputContext serialCtx;

void transportSerialInit()
{
#if defined(WEACT_STUDIO_CAN485_V1)
    Serial.begin(2000000);
#else
    Serial.begin(1000000);
#endif
    delay(100);
    Serial.println("Type 'help' for commands");
}

void transportSerialProcess()
{
    while (Serial.available())
    {
        uint8_t b = Serial.read();
        dispatchByte(serialCtx, b);
    }
}