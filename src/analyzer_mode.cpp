#include "analyzer_mode.h"
#include "can_rx_buffer.h"
#include "can_encoder.h"
#include "app_mode.h"
#include "ascii_encoder.h" // ✅ allowed (same module group)

static AnalyzerConfig cfg;

#define ANALYZER_Q_SIZE 512
static CANRxItem q[ANALYZER_Q_SIZE];
static uint16_t h = 0, t = 0;

CANRxItem item;
int budget = 64;

void analyzerInit()
{
    cfg.encoder = &asciiEncoder; // ✅ default here
    cfg.filterEnabled = false;
    cfg.filterId = 0;
}

void analyzerSetEncoder(ICanEncoder *enc)
{
    cfg.encoder = enc;
}

void analyzerSetFilter(bool enable, uint32_t id)
{
    cfg.filterEnabled = enable;
    cfg.filterId = id;
}

void analyzerLoop()
{
    if (!cfg.encoder)
        return;

    CANRxItem item;

    int budget = 64;

    while (budget-- && t != h)
    {
        item = q[t];
        t = (t + 1) % ANALYZER_Q_SIZE;

        if (cfg.filterEnabled && item.msg.identifier != cfg.filterId)
            continue;

        cfg.encoder->encode(item);
    }
}

void analyzerPush(const CANRxItem &item)
{
    uint16_t n = (h + 1) % ANALYZER_Q_SIZE;
    if (n == t)
        return;
    q[h] = item;
    h = n;
}