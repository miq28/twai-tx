#include "Arduino.h"
#include "command.h"
#include "app_mode.h"
#include "can_driver.h"

void handleCommand(const Command &cmd)
{
    switch (cmd.type)
    {
    case CMD_START:
        appState.running = true;
        break;

    case CMD_STOP:
        appState.running = false;
        break;

    case CMD_SET_MODE:
        if (strcmp(cmd.str, "generator") == 0)
            appState.mode = MODE_GENERATOR;
        else if (strcmp(cmd.str, "ecu") == 0)
            appState.mode = MODE_ECU;
        break;

    case CMD_SET_BAUD:
        if (CANDriver::reinit(cmd.value_u32, canState.listenOnly))
            canState.baud = cmd.value_u32;
        break;

    case CMD_SET_FPS:
        appState.target_fps = cmd.value_int;
        break;

    case CMD_SET_LISTEN:
        if (CANDriver::reinit(canState.baud, cmd.value_int))
            canState.listenOnly = cmd.value_int;
        break;

    case CMD_STATUS:
        Serial.printf("Mode:%d Baud:%lu FPS:%d\n",
                      appState.mode,
                      canState.baud,
                      appState.target_fps);
        break;

    default:
        break;
    }
}