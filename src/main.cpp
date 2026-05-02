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
#include "net_manager.h"
#include <WiFi.h>

const char *resetReasonToStr(esp_reset_reason_t r)
{
    switch (r)
    {
    case ESP_RST_POWERON:
        return "POWERON"; // Power on or RST pin toggled
    case ESP_RST_EXT:
        return "EXTERNAL (EN pin)"; // External pin - not applicable for ESP32
    case ESP_RST_SW:
        return "SOFTWARE"; // Software reset via esp_restart
    case ESP_RST_PANIC:
        return "PANIC"; // Exception/panic/crash
    case ESP_RST_INT_WDT:
        return "INT WDT"; // Interrupt watchdog (software or hardware)
    case ESP_RST_TASK_WDT:
        return "TASK WDT"; // Task watchdog
    case ESP_RST_WDT:
        return "OTHER WDT"; // Other watchdog
    case ESP_RST_DEEPSLEEP:
        return "DEEPSLEEP"; // Reset after exiting deep sleep mode
    case ESP_RST_BROWNOUT:
        return "BROWNOUT"; // Brownout reset (software or hardware)
    case ESP_RST_SDIO:
        return "SDIO"; // Reset over SDIO
    case ESP_RST_USB:
        return "USB"; // Reset by USB peripheral
    case ESP_RST_JTAG:
        return "JTAG"; // Reset by JTAG
    case ESP_RST_EFUSE:
        return "EFUSE"; // Reset due to efuse error
    case ESP_RST_PWR_GLITCH:
        return "POWER_GLITCH"; // Reset due to power glitch detected
    case ESP_RST_CPU_LOCKUP:
        return "CPU_LOCKUP"; // Reset due to CPU lock up (double exception)
    default:
        return "UNKNOWN"; // Reset reason can not be determined
    }
}

void stats()
{
    static uint32_t lastRatePrint = 0;
    uint32_t now = millis();

    if (now - lastRatePrint > 1000)
    {
        lastRatePrint = now;

        uint16_t used = CANRxBuffer::getUsage();
        uint16_t cap = CANRxBuffer::getCapacity();

        uint32_t usage_pct = (used * 100UL) / cap;
        uint32_t max_pct = (CANRxBuffer::getMaxUsage() * 100UL) / cap;

        float heap_kb = esp_get_free_heap_size() / 1024.0f;
        float min_kb = heap_caps_get_minimum_free_size(MALLOC_CAP_DEFAULT) / 1024.0f;

        DEBUG("[CAN] RX:%lu/s drop:%lu/s buf:%u%% max:%u%% | TX a:%lu ok:%lu f:%lu d:%lu b:%lu h:%.1fkB (min:%.1fkB)\n",
              CANRxBuffer::getRateRx(),
              CANRxBuffer::getRateDrop(),
              usage_pct,
              max_pct,
              CANTxBuffer::getRateAttempt(),
              CANTxBuffer::getRateOk(),
              CANTxBuffer::getRateFail(),
              CANTxBuffer::getRateDrop(),
              CANTxBuffer::getRateBlock(),
              heap_kb,
              min_kb);
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
    CANRxBuffer::startTask();
    analyzerInit();
    webInit();
    ledActivityInit();
    DEBUG("Free heap after setup: %u\n", ESP.getFreeHeap());
    debug_to_serial = false;
}

void loop()
{
    debug_to_serial = (appState.mode != MODE_SAVVYCAN) || netClientConnected();

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
