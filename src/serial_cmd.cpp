#include <Arduino.h>
#include <stdlib.h>
#include "serial_cmd.h"
#include "app_mode.h"
#include "can_driver.h"

static char buf[32];
static uint8_t idx = 0;

void serialCmdInit()
{
    Serial.begin(115200);
}

void serialCmdProcess()
{
    while (Serial.available())
    {
        char c = Serial.read();

        if (c == '\n' || c == '\r')
        {
            buf[idx] = 0;

            if (buf[0] == 's')
                appState.running = true;
            else if (buf[0] == 'x')
                appState.running = false;

            else if (buf[0] == 'm')
            {
                if (buf[1] == '0')
                    appState.mode = MODE_GENERATOR;
                else if (buf[1] == '1')
                    appState.mode = MODE_SLOW;
                else if (buf[1] == '2')
                    appState.mode = MODE_ECU;
            }
            else if (buf[0] == 'f')
            {
                appState.target_fps = atoi(&buf[1]);
            }
            else if (buf[0] == 'd')
            {
                appState.delay_us = atoi(&buf[1]);
            }
            else if (buf[0] == 'i')
            {
                int v = atoi(&buf[1]);
                appState.locked_id = (v >= 0 && v <= 9) ? v : -1;
            }
            else if (buf[0] == 'r')
            {
                initAppState();
            }
            // baud change
            else if (buf[0] == 'b')
            {
                uint32_t baud = atoi(&buf[1]);
                if (CANDriver::reinit(baud, canState.listenOnly))
                {
                    canState.baud = baud;
                }
            }

            // listen-only toggle
            else if (buf[0] == 'l')
            {
                bool listen = atoi(&buf[1]) == 1;
                if (CANDriver::reinit(canState.baud, listen))
                {
                    canState.listenOnly = listen;
                }
            }

            idx = 0;
        }
        else if (idx < sizeof(buf) - 1)
        {
            buf[idx++] = c;
        }
    }
}