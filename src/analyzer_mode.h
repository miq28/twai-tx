#pragma once
#include <stdint.h>

enum AnalyzerFormat
{
    ANALYZER_FORMAT_ASCII,
    ANALYZER_FORMAT_BINARY
};

void analyzerInit();
void analyzerLoop();
void analyzerSetFormat(AnalyzerFormat format);
void analyzerSetFilter(bool enable, uint32_t id);
