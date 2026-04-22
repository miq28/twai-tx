#include "app_mode.h"
#include "config.h"
#include "Preferences.h"

AppState appState;

CANFrameConfig canFrameCfg;

void initAppState()
{
    Preferences prefs;

    prefs.begin(PREF_NAME, false);
    // prefs.clear();

    if (!prefs.isKey("CAN_RX_ENABLED"))
        prefs.putBool("CAN_RX_ENABLED", true);
    if (!prefs.isKey("CAN_TX_ENABLED"))
        prefs.putBool("CAN_TX_ENABLED", true);

    appState.canRxEnabled = prefs.getBool("CAN_RX_ENABLED");
    DEBUG("canRxEnabled: %d\n", appState.canRxEnabled);
    appState.canTxEnabled = prefs.getBool("CAN_TX_ENABLED");
    DEBUG("canTxEnabled: %d\n", appState.canTxEnabled);

    prefs.end();

#if defined(WEACT_STUDIO_CAN485_V1)
    appState.mode = MODE_GENERATOR;
#else
    appState.mode = MODE_GENERATOR;
#endif
    appState.target_fps = DEFAULT_FPS;
    appState.delay_us = 0;
    appState.locked_id = -1;

    // ===== FRAME CONFIG =====

    canFrameCfg.extended = false;
    canFrameCfg.pattern = 0; // default = current behavior (0–9)
}