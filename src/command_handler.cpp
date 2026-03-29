#include "Arduino.h"
#include "command.h"
#include "app_mode.h"
#include "can_driver.h"
#include "command_registry.h"
#include "ascii_encoder.h"
#include "binary_encoder.h"
#include "analyzer_mode.h"

void analyzerSetEncoder(ICanEncoder *enc);
void analyzerSetFilter(bool enable, uint32_t id);

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
        else if (strcmp(cmd.str, "slow") == 0)
            appState.mode = MODE_SLOW;
        else if (strcmp(cmd.str, "ecu") == 0)
            appState.mode = MODE_ECU;
        else if (strcmp(cmd.str, "analyzer") == 0)
            appState.mode = MODE_ANALYZER;
        else if (strcmp(cmd.str, "savvycan") == 0)
            appState.mode = MODE_SAVVYCAN;
        break;

    case CMD_SET_BAUD:
        CANDriver::reinit(cmd.value_u32, CANDriver::isListenOnly());
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
        CANDriver::reinit(CANDriver::getCurrentBaud(), cmd.value_bool);
        break;

    case CMD_STATUS:
        Serial.printf("Mode:%d Baud:%lu, Running:%d listen-only:%s FPS:%d\n",                      
                      appState.mode,
                      CANDriver::getCurrentBaud(),
                      CANDriver::isRunning() ? "true" : "false",
                      CANDriver::isListenOnly() ? "true" : "false",
                      appState.target_fps);
        break;

    case CMD_SET_EXTENDED:
        canFrameCfg.extended = (cmd.value_int == 1);
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

    case CMD_SET_FORMAT:
        if (strcmp(cmd.str, "binary") == 0)
        {
            analyzerSetEncoder(&binaryEncoder);
            Serial.println("[FMT] binary");
        }
        else if (strcmp(cmd.str, "ascii") == 0)
        {
            analyzerSetEncoder(&asciiEncoder);
            Serial.println("[FMT] ascii");
        }
        else
        {
            Serial.println("[FMT] unknown (use: format binary | format ascii)");
        }
        break;

    case CMD_SET_FILTER:
        if (!cmd.value_bool)
        {
            analyzerSetFilter(false, 0);
            Serial.println("[FILTER] off");
        }
        else
        {
            analyzerSetFilter(true, cmd.value_u32);
            Serial.printf("[FILTER] ID:0x%lX\n", cmd.value_u32);
        }
        break;

    default:
        Serial.println("COMMAND VALID BUT HANDLE NOT AVAILABLE! check source code'");
        break;
    }
}