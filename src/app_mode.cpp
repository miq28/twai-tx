#include "app_mode.h"
#include "config.h"

AppState appState;

CANFrameConfig canFrameCfg;

void initAppState()
{
    // ===== APP STATE =====
    appState.running = true;
#if defined(WEACT_STUDIO_CAN485_V1)
    appState.mode = MODE_SAVVYCAN;
#else
    appState.mode = MODE_GENERATOR;
#endif
    appState.target_fps = DEFAULT_FPS;
    appState.delay_us = 0;
    appState.locked_id = -1;

    // ===== FRAME CONFIG =====
    canFrameCfg.extended = false;
}