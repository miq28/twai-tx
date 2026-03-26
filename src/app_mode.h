#pragma once
#include <stdint.h>

struct CANFrameConfig
{
    bool extended;   // false = standard, true = extended
};

extern CANFrameConfig canFrameCfg;

enum Mode
{
    MODE_GENERATOR,  // previous MODE_MAX
    MODE_SLOW,
    MODE_ECU         // new
};

struct AppState
{
    bool running;
    Mode mode;
    int target_fps;
    int delay_us;
    int locked_id;
};

struct CANState
{
    uint32_t baud;
    bool listenOnly;
};

extern CANState canState;

extern AppState appState;

void initAppState();