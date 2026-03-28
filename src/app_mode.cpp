#include "app_mode.h"
#include "config.h"

AppState appState;

CANState canState;

CANFrameConfig canFrameCfg;

void initAppState()
{
    // ===== APP STATE =====
    appState.running = true;
    appState.mode = MODE_SAVVYCAN;
    appState.target_fps = DEFAULT_FPS;
    appState.delay_us = 0;
    appState.locked_id = -1;

    // ===== CAN STATE =====
    canState.baud = 500000;
    canState.listenOnly = false;
    canFrameCfg.extended = false; // default = standard

    // ===== FRAME CONFIG =====
    canFrameCfg.extended = false;
}