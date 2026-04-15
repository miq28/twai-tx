// #include "soc/spi_reg.h"
// #include "esp_system.h"
// #include "esp_chip_info.h"
#include "esp_mac.h"
// #include "esp_flash.h"

#include "config.h"
#include "can_bus.h"
#include "debug.h"
#include <Preferences.h>
// #include <nvs_flash.h>
#include "esp_system.h"
#include "spi_flash_mmap.h"

void checkESPBoard()
{
    // Flash size
    Serial.print("Flash size: ");
    Serial.println(ESP.getFlashChipSize() / (1024 * 1024));
    DEBUG("Flash size: %d MB\n", ESP.getFlashChipSize() / (1024 * 1024));

    // PSRAM
    if (psramFound())
    {
        Serial.print("PSRAM size: ");
        Serial.println(ESP.getPsramSize() / (1024 * 1024));
        DEBUG("PSRAM size: %d MB\n ", ESP.getPsramSize() / (1024 * 1024));
    }
    else
    {
        Serial.println("PSRAM not found");
        DEBUG_PRINTLN("PSRAM not found");
    }

    // DEBUGPORT.print("ESP32 SDK: "); DEBUGPORT.println(ESP.getSdkVersion());
    // DEBUGPORT.print("ESP32 DEVICE: "); DEBUGPORT.println(GetDeviceHardwareRevision());
    // DEBUGPORT.print("ESP32 CHIP ID: "); DEBUGPORT.println((uint32_t)ESP.getEfuseMac(), HEX);
    // DEBUGPORT.print("ESP32 CPU CORES: "); DEBUGPORT.println(ESP.getChipCores());
    // DEBUGPORT.print("ESP32 CPU FREQ: "); DEBUGPORT.print(getCpuFrequencyMhz()); DEBUGPORT.println(" MHz");
    // DEBUGPORT.print("ESP32 XTAL FREQ: "); DEBUGPORT.print(getXtalFrequencyMhz()); DEBUGPORT.println(" MHz");
    // DEBUGPORT.print("ESP32 APB FREQ: "); DEBUGPORT.print(getApbFrequency() / 1000000.0, 1); DEBUGPORT.println(" MHz");
    // DEBUGPORT.print("ESP32 FLASH CHIP ID: "); DEBUGPORT.println(ESP_get_FlashChipId());
    // DEBUGPORT.print("ESP32 FLASH CHIP FREQ: "); DEBUGPORT.print(ESP_getFlashChipSpeed() / 1000000.0, 1); DEBUGPORT.println(" MHz");
    // DEBUGPORT.print("ESP32 FLASH REAL SIZE: "); DEBUGPORT.print(ESP_getFlashChipRealSize() / (1024.0 * 1024), 2); DEBUGPORT.println(" MB");
    // //DEBUGPORT.print("ESP32 FLASH SIZE (MAGIC BYTE): "); DEBUGPORT.print(ESP.getFlashChipSize() / (1024.0 * 1024), 2); DEBUGPORT.println(" MB");
    // DEBUGPORT.print("ESP32 FLASH REAL MODE: "); DEBUGPORT.println(ESP_getFlashChipMode());
    // DEBUGPORT.print("ESP32 FLASH MODE (MAGIC BYTE): "); DEBUGPORT.print(ESP.getFlashChipMode()); DEBUGPORT.println(", 0=QIO, 1=QOUT, 2=DIO, 3=DOUT");
    // DEBUGPORT.print("ESP32 RAM SIZE: "); DEBUGPORT.print(ESP.getHeapSize() / 1024.0, 2); DEBUGPORT.println(" KB");
    // DEBUGPORT.print("ESP32 FREE RAM: "); DEBUGPORT.print(ESP.getFreeHeap() / 1024.0, 2); DEBUGPORT.println(" KB");
    // DEBUGPORT.print("ESP32 MAX RAM ALLOC: "); DEBUGPORT.print(ESP.getMaxAllocHeap() / 1024.0, 2); DEBUGPORT.println(" KB");
    // DEBUGPORT.print("ESP32 FREE PSRAM: "); DEBUGPORT.print(ESP.getFreePsram() / 1024.0, 2); DEBUGPORT.println(" KB");
    // DEBUGPORT.print("ESP32 TOTAL PSRAM: "); DEBUGPORT.print(ESP.getPsramSize() / 1024.0, 2); DEBUGPORT.println(" KB");

    // // Print MAC address
    // uint8_t mac[6];
    // esp_read_mac(mac, ESP_MAC_WIFI_STA);
    // DEBUGPORT.print("ESP32 MAC: ");
    // for (int i = 0; i < 6; i++) {
    //   if (i > 0) DEBUGPORT.print(":");
    //   DEBUGPORT.print(mac[i], HEX);
    // }
    // DEBUGPORT.println();
}

Settings settings;

void loadSettings()
{
    // nvs_flash_erase(); // erase the NVS partition and...
    // nvs_flash_init();  // initialize the NVS partition.
    Preferences prefs;

    // prefs.begin(PREF_NAME, false);
    // settings.CANBaud = prefs.getUInt("CANBaud", 500000);
    // settings.listenOnly = prefs.getBool("listenOnly", false);

    prefs.begin(PREF_NAME, false);
    // prefs.clear();

    if (!prefs.isKey("CANBaud") || prefs.getUInt("CANBaud") == 0)
    {
        prefs.putUInt("CANBaud", 500000);
    }
    if (!prefs.isKey("listenOnly"))
    {
        prefs.putBool("listenOnly", false);
    }

    settings.CANBaud = prefs.getUInt("CANBaud");
    DEBUG("CANBaud: %u\n", settings.CANBaud);
    settings.listenOnly = prefs.getBool("listenOnly");
    DEBUG("listenOnly: %d\n", settings.listenOnly);

    prefs.end();
}

void applyCANConfig(uint32_t baud, bool listenOnly)
{
    uint32_t oldBaud = CANDriver::getCurrentBaud();
    bool oldListen = CANDriver::isListenOnly();

    if (oldBaud == baud && oldListen == listenOnly)
        return;

    // Apply to driver
    CANDriver::reinit(baud, listenOnly);

    // Update runtime copy
    settings.CANBaud = baud;
    settings.listenOnly = listenOnly;

    // Persist
    Preferences prefs;
    prefs.begin(PREF_NAME, false);
    prefs.putUInt("CANBaud", baud);
    prefs.putBool("listenOnly", listenOnly);
    prefs.end();
}