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
// optional filter (application layer)
static bool filterEnabled = false;
static uint32_t filterId = 0;

void analyzerSetFilter(bool enable, uint32_t id)
{
    filterEnabled = enable;
    filterId = id;
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