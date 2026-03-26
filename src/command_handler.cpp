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

    case CMD_SET_EXTENDED:
        canFrameCfg.extended = (cmd.value_int == 1);

        Serial.printf("[CAN] Frame mode: %s\n",
                      canFrameCfg.extended ? "EXTENDED (29-bit)" : "STANDARD (11-bit)");
        break;

    case CMD_HELP:
    {
        Serial.println("\n=== COMMANDS ===");

        Serial.println("start / stop");
        Serial.println("mode generator | mode ecu | mode slow");

        Serial.println("fps <num>       (e.g. fps 1000)");
        Serial.println("delay <us>      (e.g. delay 100)");
        Serial.println("id <n>          (0-9, -1 = auto)");

        Serial.println("baud <num>      (e.g. baud 500000)");
        Serial.println("listen 0|1");

        Serial.println("ext 0|1         (standard / extended)");

        Serial.println("status");

        Serial.println("=================\n");
    }
    break;

    default:
        break;
    }
}