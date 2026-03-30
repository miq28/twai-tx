#include "input_dispatcher.h"
#include "command.h"
#include "command_parser.h"
#include "command_handler.h"
#include "app_mode.h"
#include "rs485.h"

// forward declarations (no coupling)
void processIncomingByte(uint8_t b); // GVRET
void handleCommand(const Command &cmd);
bool parseCommand(const char *buf, Command &cmd);

static uint8_t escapeCount = 0;

// ===== CLI =====
static char buf[64];
static uint8_t idx = 0;

void cliProcessByte(uint8_t b)
{
    char c = (char)b;

    if (c == '\n' || c == '\r')
    {
        buf[idx] = 0;

        Command cmd;
        if (parseCommand(buf, cmd))
        {
            handleCommand(cmd);
        }

        idx = 0;
    }
    else if (idx < sizeof(buf) - 1)
    {
        buf[idx++] = c;
    }
}

// ===== DISPATCHER =====
void dispatchByte(InputContext &ctx, uint8_t b)
{
    // ===== ESCAPE =====
    if (b == '+')
    {
        escapeCount++;
        if (escapeCount >= 3)
        {
            appState.mode = MODE_ANALYZER;
            escapeCount = 0;
            RS485.println("[ESC] Exit SAVVYCAN mode");
            return;
        }
    }
    else
    {
        escapeCount = 0;
    }

    // ===== AUTO-SWITCH TO SAVVYCAN =====
    if (appState.mode != MODE_SAVVYCAN)
    {
        if (b == 0xE7 || b == 0xF1)
        {
            appState.mode = MODE_SAVVYCAN;
            RS485.println("[AUTO] Enter SAVVYCAN mode");
        }
    }

    // ===== ROUTING =====
    if (appState.mode == MODE_SAVVYCAN)
    {
        processIncomingByte(b);
    }
    else
    {
        cliProcessByte(b);
    }
}