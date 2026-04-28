#pragma once
#include <stdint.h>

struct CANFrameConfig
{
    bool extended;   // false = standard, true = extended
};

extern CANFrameConfig canFrameCfg;

enum Mode
{
    MODE_GENERATOR,
    MODE_SLOW,
    MODE_ECU,
    MODE_ANALYZER,
    MODE_SAVVYCAN
};

struct AppState
{
    bool running;
    int target_fps;
    int delay_us;
    int locked_id;
};

extern AppState appState;

void initAppState();