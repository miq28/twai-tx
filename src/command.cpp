#include "command.h"
#include <Arduino.h>
#include <stdlib.h>
#include <string.h>
#include "analyzer_mode.h"
#include "app_mode.h"
#include "can_bus.h"
#include "gvret_mode.h"
#include "debug.h"
#include "config.h"

namespace
{
struct CommandInfo
{
    const char *name;
    const char *alias;
    const char *help;
};

const CommandInfo commandTable[] = {
    {"start", "s", "start streaming"},
    {"stop", "x", "stop streaming"},
    {"fps", "f", "set FPS (e.g. fps 1000)"},
    {"help", "", "show this help"},
    {"baud", "b", "set CAN baud (e.g. b500000)"},
    {"ext", "e", "ext 0/1  : standard / extended frame"},
    {"delay", "d", "d<number>: delay us (e.g. d100)"},
    {"lock ID", "i", "i<number>: lock ID (0-9), i-1 = cycle"},
    {"listen only", "l", "l0 / l1  : listen OFF / ON"},
    {"mode generator", "m0", "generator mode"},
    {"mode slow", "m1", "slow mode"},
    {"mode ecu", "m2", "ECU mode"},
    {"mode analyzer", "m3", "analyzer mode"},
    {"mode savvycan", "m4", "savvycan mode"},
    {"status", "", "show status"},
    {"reset", "r", "reset defaults"},
};

uint8_t escapeCount = 0;
char lineBuffer[64];
uint8_t lineIndex = 0;

bool parseCommand(char *buf, Command &cmd)
{
    if (strcmp(buf, "start") == 0 || strcmp(buf, "s") == 0)
    {
        cmd.type = CMD_START;
        return true;
    }

    if (strcmp(buf, "stop") == 0 || strcmp(buf, "x") == 0)
    {
        cmd.type = CMD_STOP;
        return true;
    }

    if (strncmp(buf, "baud ", 5) == 0)
    {
        cmd.type = CMD_SET_BAUD;
        cmd.value_u32 = atoi(buf + 5);
        return true;
    }

    if (buf[0] == 'b')
    {
        cmd.type = CMD_SET_BAUD;
        cmd.value_u32 = atoi(buf + 1);
        return true;
    }

    if (buf[0] == 'l')
    {
        cmd.type = CMD_SET_LISTEN;
        cmd.value_bool = atoi(buf + 1);
        return true;
    }

    if (buf[0] == 'd')
    {
        cmd.type = CMD_SET_DELAY;
        cmd.value_int = atoi(buf + 1);
        return true;
    }

    if (buf[0] == 'i')
    {
        cmd.type = CMD_LOCK_ID;
        int value = atoi(&buf[1]);
        cmd.value_int = (value >= 0 && value <= 9) ? value : -1;
        return true;
    }

    if (buf[0] == 'r')
    {
        cmd.type = CMD_RESET;
        return true;
    }

    if (strncmp(buf, "fps ", 4) == 0)
    {
        cmd.type = CMD_SET_FPS;
        cmd.value_int = atoi(buf + 4);
        return true;
    }

    if (buf[0] == 'f' && isdigit(buf[1]))
    {
        cmd.type = CMD_SET_FPS;
        cmd.value_int = atoi(buf + 1);
        return true;
    }

    if (strcmp(buf, "mode generator") == 0 || strcmp(buf, "m0") == 0)
    {
        cmd.type = CMD_SET_MODE;
        strcpy(cmd.str, "generator");
        return true;
    }

    if (strcmp(buf, "mode slow") == 0 || strcmp(buf, "m1") == 0)
    {
        cmd.type = CMD_SET_MODE;
        strcpy(cmd.str, "slow");
        return true;
    }

    if (strcmp(buf, "mode ecu") == 0 || strcmp(buf, "m2") == 0)
    {
        cmd.type = CMD_SET_MODE;
        strcpy(cmd.str, "ecu");
        return true;
    }

    if (strcmp(buf, "mode analyzer") == 0 || strcmp(buf, "m3") == 0)
    {
        cmd.type = CMD_SET_MODE;
        strcpy(cmd.str, "analyzer");
        return true;
    }

    if (strcmp(buf, "mode savvycan") == 0 || strcmp(buf, "m4") == 0)
    {
        cmd.type = CMD_SET_MODE;
        strcpy(cmd.str, "savvycan");
        return true;
    }

    if (strcmp(buf, "status") == 0)
    {
        cmd.type = CMD_STATUS;
        return true;
    }

    if (strcmp(buf, "ext 1") == 0)
    {
        cmd.type = CMD_SET_EXTENDED;
        cmd.value_int = 1;
        return true;
    }

    if (strcmp(buf, "ext 0") == 0)
    {
        cmd.type = CMD_SET_EXTENDED;
        cmd.value_int = 0;
        return true;
    }

    if (strcmp(buf, "help") == 0)
    {
        cmd.type = CMD_HELP;
        return true;
    }

    if (strncmp(buf, "format ", 7) == 0)
    {
        cmd.type = CMD_SET_FORMAT;
        strcpy(cmd.str, buf + 7);
        return true;
    }

    if (strncmp(buf, "filter ", 7) == 0)
    {
        cmd.type = CMD_SET_FILTER;
        if (strcmp(buf + 7, "off") == 0)
        {
            cmd.value_bool = false;
        }
        else
        {
            cmd.value_bool = true;
            cmd.value_u32 = strtoul(buf + 7, NULL, 0);
        }
        return true;
    }

    if (strncmp(buf, "wifimode ", 9) == 0)
    {
        cmd.type = CMD_SET_WIFIMODE;
        cmd.value_u8 = atoi(buf + 9);
        return true;
    }

    return false;
}

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
    {
        Mode newMode = appState.mode;

        if (strcmp(cmd.str, "generator") == 0) newMode = MODE_GENERATOR;
        else if (strcmp(cmd.str, "slow") == 0) newMode = MODE_SLOW;
        else if (strcmp(cmd.str, "ecu") == 0) newMode = MODE_ECU;
        else if (strcmp(cmd.str, "analyzer") == 0) newMode = MODE_ANALYZER;
        else if (strcmp(cmd.str, "savvycan") == 0) newMode = MODE_SAVVYCAN;

        if (newMode != appState.mode)
        {
            CANRxBuffer::clear();   // 🔥 CRITICAL: drop old frames
            appState.mode = newMode;
        }
        break;
    }
    case CMD_SET_BAUD:
        applyCANConfig(cmd.value_u32, CANDriver::isListenOnly());
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
        applyCANConfig(CANDriver::getCurrentBaud(), cmd.value_bool);
        break;
    case CMD_STATUS:
        DEBUG("Mode:%d Baud:%lu, Running:%s listen-only:%s FPS:%d\n",
                      appState.mode,
                      CANDriver::getCurrentBaud(),
                      CANDriver::isRunning() ? "true" : "false",
                      CANDriver::isListenOnly() ? "true" : "false",
                      appState.target_fps);
        break;
    case CMD_SET_EXTENDED:
        canFrameCfg.extended = (cmd.value_int == 1);
        DEBUG("[CAN] Frame mode: %s\n",
                      canFrameCfg.extended ? "EXTENDED (29-bit)" : "STANDARD (11-bit)");
        break;
    case CMD_RESET:
        initAppState();
        DEBUG_PRINTLN("*** STATES RESET - states only ***");
        break;
    case CMD_HELP:
        DEBUG_PRINTLN("\n=== COMMANDS ===");
        for (size_t i = 0; i < sizeof(commandTable) / sizeof(commandTable[0]); i++)
        {
            DEBUG("%-15s (%s) : %s\n",
                          commandTable[i].name,
                          commandTable[i].alias,
                          commandTable[i].help);
        }
        DEBUG_PRINTLN("=================\n");
        break;
    case CMD_SET_FORMAT:
        if (strcmp(cmd.str, "binary") == 0)
        {
            analyzerSetFormat(ANALYZER_FORMAT_BINARY);
            DEBUG_PRINTLN("[FMT] binary");
        }
        else if (strcmp(cmd.str, "ascii") == 0)
        {
            analyzerSetFormat(ANALYZER_FORMAT_ASCII);
            DEBUG_PRINTLN("[FMT] ascii");
        }
        else
        {
            DEBUG_PRINTLN("[FMT] unknown (use: format binary | format ascii)");
        }
        break;
    case CMD_SET_FILTER:
        if (!cmd.value_bool)
        {
            analyzerSetFilter(false, 0);
            DEBUG_PRINTLN("[FILTER] off");
        }
        else
        {
            analyzerSetFilter(true, cmd.value_u32);
            DEBUG("[FILTER] ID:0x%lX\n", cmd.value_u32);
        }
        break;
    case CMD_SET_WIFIMODE:
        changeWifiMode(cmd.value_u8);
        break;
    default:
        DEBUG_PRINTLN("COMMAND VALID BUT HANDLE NOT AVAILABLE! check source code'");
        break;
    }
}
}

void cliProcessByte(uint8_t b)
{
    char c = (char)b;

    if (c == '\n' || c == '\r')
    {
        lineBuffer[lineIndex] = 0;

        Command cmd = {};
        if (parseCommand(lineBuffer, cmd)) handleCommand(cmd);
        lineIndex = 0;
    }
    else if (lineIndex < sizeof(lineBuffer) - 1)
    {
        lineBuffer[lineIndex++] = c;
    }
}

void dispatchByte(InputContext &, uint8_t b)
{
    if (b == '+')
    {
        escapeCount++;
        if (escapeCount >= 3)
        {
            CANRxBuffer::clear();   // 🔥
            appState.mode = MODE_ANALYZER;
            escapeCount = 0;
            DEBUG_PRINTLN("[ESC] Exit SAVVYCAN mode");
            return;
        }
    }
    else
    {
        escapeCount = 0;
    }

    if (appState.mode != MODE_SAVVYCAN && (b == 0xE7 || b == 0xF1))
    {
        CANRxBuffer::clear();   // 🔥 prevent ghost frames
        appState.mode = MODE_SAVVYCAN;
        DEBUG_PRINTLN("[AUTO] Enter SAVVYCAN mode");
    }

    if (appState.mode == MODE_SAVVYCAN) processIncomingByte(b);
    else cliProcessByte(b);
}
