#include "analyzer_mode.h"
#include "can_rx_buffer.h"
#include "can_encoder.h"
#include "app_mode.h"
#include "ascii_encoder.h"   // ✅ allowed (same module group)

static AnalyzerConfig cfg;

void analyzerInit()
{
    cfg.encoder = &asciiEncoder;   // ✅ default here
    cfg.filterEnabled = false;
    cfg.filterId = 0;
}

void analyzerSetEncoder(ICanEncoder* enc)
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
    if (!cfg.encoder) return;

    CANRxItem item;

    while (rxBufferPop(item))
    {
        if (cfg.filterEnabled &&
            item.msg.identifier != cfg.filterId)
            continue;

        cfg.encoder->encode(item);
    }
}