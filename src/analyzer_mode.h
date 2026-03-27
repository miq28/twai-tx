#pragma once
#include "can_encoder.h"

struct AnalyzerConfig
{
    ICanEncoder* encoder;
    bool filterEnabled;
    uint32_t filterId;
};

void analyzerInit();
void analyzerLoop();
void analyzerSetEncoder(ICanEncoder* enc);
void analyzerSetFilter(bool enable, uint32_t id);