#include "Arduino.h"
#include "command.h"
#include "app_mode.h"
#include "can_driver.h"
#include "command_registry.h"

extern CommandInfo commandTable[];
extern int commandCount;

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

    case CMD_SET_DELAY:
        appState.delay_us = cmd.value_int;
        break;

    case CMD_LOCK_ID:
        appState.locked_id = cmd.value_int;
        break;

    case CMD_SET_LISTEN:
        if (CANDriver::reinit(canState.baud, cmd.value_bool))
            canState.listenOnly = cmd.value_bool;
        break;

    case CMD_STATUS:
        Serial.printf("Mode:%d Baud:%lu FPS:%d\n",
                      appState.mode,
                      canState.baud,
                      appState.target_fps);
        break;

    case CMD_SET_EXTENDED:
        canFrameCfg.extended = (cmd.value_int == 1);
        // if (CANDriver::reinit(canState.baud, cmd.value_int))
        Serial.printf("[CAN] Frame mode: %s\n",
                      canFrameCfg.extended ? "EXTENDED (29-bit)" : "STANDARD (11-bit)");
        break;

    case CMD_RESET:
        initAppState();
        Serial.println("*** STATES RESET - states only ***");
        break;

    case CMD_HELP:
    {
        Serial.println("\n=== COMMANDS ===");

        for (int i = 0; i < commandCount; i++)
        {
            Serial.printf("%-15s (%s) : %s\n",
                          commandTable[i].name,
                          commandTable[i].alias,
                          commandTable[i].help);
        }

        Serial.println("=================\n");
    }
    break;

    default:
        Serial.println("COMMAND VALID BUT HANDLE NOT AVAILABLE! check source code'");
        break;
    }
}