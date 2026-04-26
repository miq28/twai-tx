#include <Arduino.h>
#include "can_bus.h"
#include "transport.h"
#include "app_mode.h"
#include "traffic_modes.h"
#include "analyzer_mode.h"
#include "gvret_mode.h"
#include "debug.h"
#include <Preferences.h>
#include "config.h"
#include "web_server.h"
#include "led_activity.h"
#include <WiFi.h>



void stats()
{
    static uint32_t lastDrops = 0;
    static uint32_t lastFrames = 0;
    static uint32_t lastPrint = 0;

    static uint32_t accumOverwrite = 0;

    uint32_t nowDrops = CANRxBuffer::getDropCount();
    uint32_t deltaDrops = nowDrops - lastDrops;

    // 🔥 selalu update baseline
    lastDrops = nowDrops;

    // akumulasi overwrite
    if (deltaDrops > 0)
    {
        accumOverwrite += deltaDrops;
    }

    if (millis() - lastPrint >= 1000)
    {
        lastPrint = millis();

        // =========== RX stats
        uint32_t frames = CANRxBuffer::getTotalFrames();
        uint16_t maxBuf = CANRxBuffer::getMaxUsage();
        uint16_t curBuf = CANRxBuffer::count();

        uint32_t deltaFrames = frames - lastFrames;
        lastFrames = frames;

        if (deltaFrames > 0 || accumOverwrite > 0)
        {
            float overwriteRate = (deltaFrames > 0)
                                      ? (100.0f * accumOverwrite / deltaFrames)
                                      : 0.0f;

            DEBUG("[CAN] RX: fps=%lu overwrite=%lu (%.2f%%) buf=%u max=%u\n",
                  deltaFrames,
                  accumOverwrite,
                  overwriteRate,
                  curBuf,
                  maxBuf);

            accumOverwrite = 0;
        }

        // =========== TX stats
        if (!appState.canTxEnabled)
        {
            DEBUG("[CAN] TX: DISABLED\n");
            return;
        }

        static uint32_t lastTxAttempt = 0;
        static uint32_t lastTxOk = 0;
        static uint32_t lastTxDrop = 0;

        uint32_t txAttempt = CANDriver::getTxAttempt();
        uint32_t txSuccess = CANDriver::getTxSuccess();
        uint32_t txDrop = CANDriver::getTxDrop();

        uint32_t dTxAttempt = txAttempt - lastTxAttempt;
        uint32_t dTxOk = txSuccess - lastTxOk;
        uint32_t dTxDrop = txDrop - lastTxDrop;

        lastTxAttempt = txAttempt;
        lastTxOk = txSuccess;
        lastTxDrop = txDrop;

        uint32_t qUsed = CANDriver::getTxQueueUsed();
        uint32_t qFree = CANDriver::getTxQueueFree();
        DEBUG("[CAN] TX: Target FPS=%lu attempt=%lu ok=%lu drop=%lu q=%lu/%lu\n",
              appState.target_fps,
              dTxAttempt,
              dTxOk,
              dTxDrop,
              qUsed,
              qUsed + qFree);
    }
}

void setup()
{
    Serial.begin(1000000);
    RS485.begin(1000000);
    debug_to_serial = true;
    DEBUG("\n=== BOOT ===\n");
    esp_reset_reason_t r = esp_reset_reason();
    DEBUG("Reset reason: %s (%d)\n",
          resetReasonToStr(r), r);
    DEBUG("Free heap before setup: %u\n", ESP.getFreeHeap());

    checkESPBoard();
    loadSettings();

    transportInit();
    initAppState();
    CANDriver::init(settings.CANBaud, settings.listenOnly);
    // CANMonitor::startTask();
    analyzerInit();
    webInit();
    ledActivityInit();
    DEBUG("Free heap after setup: %u\n", ESP.getFreeHeap());
    debug_to_serial = false;
}

void loop()
{
    debug_to_serial = !(appState.mode == MODE_SAVVYCAN);

    transportProcess();

    gvretLoop();

    switch (appState.mode)
    {
    case MODE_GENERATOR:

    case MODE_SLOW:
        generatorLoop();
        break;

    case MODE_ECU:
        ecuLoop();
        break;

    case MODE_ANALYZER:
        analyzerLoop();
        break;

    case MODE_SAVVYCAN:
        break;
    }

    // 🔥 ADD HERE
    streamFlush();

    // ===== TX handing
    transportFlush();

    stats();
    // ledActivityUpdate();
    // ledWifiConnected(WiFi.status() == WL_CONNECTED);
}
