#include "transport_manager.h"
#include "input_dispatcher.h"

#define MAX_TRANSPORTS 4

static ITransport* list[MAX_TRANSPORTS];
static int count = 0;

static InputContext ctx;

void transportManagerInit()
{
    count = 0;
}

void transportManagerAdd(ITransport* t)
{
    if (count < MAX_TRANSPORTS)
        list[count++] = t;
}

bool transportManagerSend(const uint8_t* data, int len)
{
    bool ok = true;

    for (int i = 0; i < count; i++)
    {
        if (!list[i]->send(data, len))
            ok = false;
    }

    return ok;
}

void transportManagerProcessRx()
{
    for (int i = 0; i < count; i++)
    {
        int budget = 64;

        while (budget-- && list[i]->available())
        {
            uint8_t b = list[i]->read();
            dispatchByte(ctx, b);
        }
    }
}