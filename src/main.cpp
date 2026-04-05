#include <Arduino.h>
#include "can_bus.h"
#include "transport.h"
#include "app_mode.h"
#include "traffic_modes.h"
#include "analyzer_mode.h"
#include "gvret_mode.h"
#include "debug.h"

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

void setup()
{
    RS485.begin(2000000);
    DEBUG("\n=== BOOT ===\n");
    esp_reset_reason_t r = esp_reset_reason();
    DEBUG("Reset reason: %s (%d)\n",
          resetReasonToStr(r), r);
    DEBUG("Free heap before setup: %u\n", ESP.getFreeHeap());
    transportInit();
    initAppState();
    CANDriver::init(500000, false);
    startCanRxTask();
    analyzerInit();
    DEBUG("Free heap after setup: %u\n", ESP.getFreeHeap());
}

void loop()
{
    transportProcess();

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
        gvretLoop();
        break;
    }
}
