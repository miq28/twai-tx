#include "can_pipeline.h"
#include "transport.h"
#include "transport_manager.h"

#define Q_SIZE 512
static CANRxItem q[Q_SIZE];
static uint16_t h=0,t=0;

static uint32_t analyzerDrop = 0;

void analyzerPush(const CANRxItem& item)
{
    uint16_t n=(h+1)%Q_SIZE;
    if (n == t)
    {
        analyzerDrop++;
        return;
    }
    q[h]=item;
    h=n;
}

void analyzerProcess()
{
    CANRxItem item;
    int budget = 64;

    while (budget-- && t != h)
    {
        item = q[t];
        t = (t + 1) % Q_SIZE;

        uint8_t out[32];
        int idx = 0;

        uint32_t id = item.msg.identifier;
        out[idx++] = id;
        out[idx++] = id >> 8;
        out[idx++] = id >> 16;
        out[idx++] = id >> 24;

        uint8_t dlc = item.msg.data_length_code;
        out[idx++] = dlc;

        for (int i=0;i<dlc;i++)
            out[idx++] = item.msg.data[i];

        transportManagerSend(out, idx);
    }
}