#pragma once
#include <stdint.h>

struct CANFrameConfig
{
    bool extended; // false = standard, true = extended
    // 🔥 ADD THIS
    uint8_t pattern; // 0=original, 1=realistic
    /*
        Pattern 0 (your original)
        IDs: 0–9
        EXT: manual (ext 0/1)
        DLC: fixed 8

        Pattern 1 (realistic)
        IDs: random
        EXT: mixed automatically
        DLC: 0–8 random
        payload: random
    */
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
    Mode mode;
    int target_fps;
    int delay_us;
    int locked_id;
};

extern AppState appState;

void initAppState();