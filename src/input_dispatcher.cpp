#include "input_dispatcher.h"
#include "command.h"
#include "app_mode.h"
#include "gvret.h"
#include <stdlib.h>
#include <string.h>
#include "rs485.h"

static uint8_t escapeCount = 0;
static char buf[64];
static uint8_t idx = 0;

void cliProcessByte(uint8_t b)
{
    char c = (char)b;

    if (c == '\n') return;

    if (c == '\r')
    {
        buf[idx] = 0;

        RS485.printf("[CLI] %s\n", buf);

        Command cmd;
        if (commandParse(buf, cmd))
        {
            cmdPush(cmd);
        }
        else
        {
            RS485.println("[ERR] unknown command");
        }

        idx = 0;
    }
    else if (idx < sizeof(buf) - 1)
    {
        buf[idx++] = c;
    }
}

void dispatchByte(InputContext&, uint8_t b)
{
    if (b == '+')
    {
        if (++escapeCount >= 3)
        {
            appState.mode = MODE_ANALYZER;
            escapeCount = 0;
            RS485.println("[ESC] exit savvy");
            return;
        }
    }
    else escapeCount = 0;

    if (b == 0xE7 || b == 0xF1)
    {
        appState.mode = MODE_SAVVYCAN;
        RS485.println("[AUTO] SAVVYCAN");
    }

    if (appState.mode == MODE_SAVVYCAN)
        gvretProcessByte(b);
    else
        cliProcessByte(b);
}