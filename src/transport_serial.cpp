#include <Arduino.h>
#include "transport_serial.h"

#include "command.h"
#include "command_parser.h"
#include "command_handler.h"

void transportSerialInit()
{
    Serial.begin(1000000);
    delay(100); // allow USB stabilize
}

void transportSerialProcess()
{
    static char buf[64];
    static uint8_t idx = 0;

    while (Serial.available())
    {
        char c = Serial.read();

        if (c == '\n' || c == '\r')
        {
            buf[idx] = 0;

            Command cmd;
            if (parseCommand(buf, cmd))
                handleCommand(cmd);

            idx = 0;
        }
        else if (idx < sizeof(buf) - 1)
        {
            buf[idx++] = c;
        }
    }
}