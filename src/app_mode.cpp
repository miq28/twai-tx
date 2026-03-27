#include "app_mode.h"
#include "config.h"

AppState appState;

CANState canState;

CANFrameConfig canFrameCfg;

void initAppState()
{
    appState.running = true;
    appState.mode = MODE_ANALYZER;
    appState.target_fps = DEFAULT_FPS;
    appState.delay_us = 0;
    appState.locked_id = -1;

    canState.baud = 500000;
    canState.listenOnly = false;

    canFrameCfg.extended = false; // default = standard
}