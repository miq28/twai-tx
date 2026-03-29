#include "input_dispatcher.h"
#include "command.h"
#include "command_parser.h"
#include "command_handler.h"

// forward declarations (no coupling)
void processIncomingByte(uint8_t b);   // GVRET
void handleCommand(const Command& cmd);
bool parseCommand(const char* buf, Command& cmd);

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
void dispatchByte(InputContext& ctx, uint8_t b)
{
    // detect GVRET mode switch
    if (b == 0xE7)
    {
        ctx.binaryMode = true;
        processIncomingByte(b);
        return;
    }

    // GVRET stream
    if (b == 0xF1 || ctx.binaryMode)
    {
        processIncomingByte(b);
        return;
    }

    // otherwise CLI
    cliProcessByte(b);
}