// // #include "soc/spi_reg.h"
// // #include "esp_system.h"
// // #include "esp_chip_info.h"
// #include "esp_mac.h"
// // #include "esp_flash.h"

// #include "config.h"
// #include "can_bus.h"
// #include "debug.h"
// #include <Preferences.h>
// // #include <nvs_flash.h>
// #include "esp_system.h"
// #include "spi_flash_mmap.h"

// #include "esp_flash.h"

#include "esp_mac.h"
#include "esp_flash.h"

#include "config.h"
#include "can_bus.h"
#include "debug.h"
#include "led_activity.h"
#include <Preferences.h>
#include "esp_system.h"
#include "spi_flash_mmap.h"

void printFlashID()
{
    uint32_t flash_id;

    if (esp_flash_read_id(NULL, &flash_id) == ESP_OK)
    {
        DEBUG("FLASH ID: %06X\n", flash_id);
    }
    else
    {
        DEBUG("FLASH ID: read failed\n");
    }
}

void checkESPBoard()
{
    // ===== BASIC =====
    DEBUG("SDK: %s\n", ESP.getSdkVersion());
    DEBUG("Chip: %s Rev %u\n", ESP.getChipModel(), ESP.getChipRevision());
    DEBUG("Cores: %u\n", ESP.getChipCores());

    // ===== CLOCK =====
    DEBUG("CPU: %d MHz\n", getCpuFrequencyMhz());
    DEBUG("XTAL: %d MHz\n", getXtalFrequencyMhz());
    DEBUG("APB: %.1f MHz\n", getApbFrequency() / 1000000.0);

    // ===== MAC =====
    uint64_t mac64 = ESP.getEfuseMac();
    // DEBUG("MAC (raw): %012llX\n", mac64);
    DEBUG("MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
          (uint8_t)(mac64 >> 40),
          (uint8_t)(mac64 >> 32),
          (uint8_t)(mac64 >> 24),
          (uint8_t)(mac64 >> 16),
          (uint8_t)(mac64 >> 8),
          (uint8_t)(mac64 >> 0));

    // ===== CHIP ID (derived) =====
    uint32_t chipId = 0;
    for (int i = 0; i < 17; i += 8)
    {
        chipId |= ((mac64 >> (40 - i)) & 0xFF) << i;
    }
    DEBUG("Chip ID: %u (0x%08X)\n", chipId, chipId);

    // ===== FLASH =====
    printFlashID();
    DEBUG("Flash Speed: %.1f MHz\n", ESP.getFlashChipSpeed() / 1000000.0);
    DEBUG("Flash Size: %.2f MB\n", ESP.getFlashChipSize() / (1024.0 * 1024));
    DEBUG("Flash Mode: %d (0=QIO,1=QOUT,2=DIO,3=DOUT)\n", ESP.getFlashChipMode());

    // ===== RAM =====
    DEBUG("Heap Total: %.2f KB\n", ESP.getHeapSize() / 1024.0);
    DEBUG("Heap Free: %.2f KB\n", ESP.getFreeHeap() / 1024.0);
    DEBUG("Heap Max Alloc: %.2f KB\n", ESP.getMaxAllocHeap() / 1024.0);

    // ===== PSRAM =====
    if (psramFound())
    {
        DEBUG("PSRAM Total: %.2f KB\n", ESP.getPsramSize() / 1024.0);
        DEBUG("PSRAM Free: %.2f KB\n", ESP.getFreePsram() / 1024.0);
    }
    else
    {
        DEBUG("PSRAM: not found\n");
    }
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
    if (!prefs.isKey("canLogMask"))
    {
        prefs.putUInt("canLogMask",
                      CAN_ALERT_CRITICAL | CAN_ALERT_OPERATIONAL);
    }
    if (!prefs.isKey("ledEnabled"))
    {
        prefs.putBool("ledEnabled", true); // default ON
    }

    settings.CANBaud = prefs.getUInt("CANBaud");
    DEBUG("CANBaud: %u\n", settings.CANBaud);
    settings.listenOnly = prefs.getBool("listenOnly");
    DEBUG("listenOnly: %d\n", settings.listenOnly);
    settings.canLogMask = prefs.getUInt("canLogMask");
    DEBUG("canLogMask: %u\n", settings.canLogMask);
    settings.ledEnabled = prefs.getBool("ledEnabled");
    DEBUG("ledEnabled: %d\n", settings.ledEnabled);

    prefs.end();
}

void setCANConfig(uint32_t baud, bool listenOnly)
{
    if (settings.CANBaud == baud &&
        settings.listenOnly == listenOnly)
        return;

    settings.CANBaud = baud;
    settings.listenOnly = listenOnly;

    Preferences prefs;
    prefs.begin(PREF_NAME, false);
    prefs.putUInt("CANBaud", baud);
    prefs.putBool("listenOnly", listenOnly);
    prefs.end();

    DEBUG("[CAN CFG] baud:%lu listen:%d\n", baud, listenOnly);

    CANDriver::reinit(baud, listenOnly);
}

void setWifiMode(uint8_t mode)
{
    if (settings.wifiMode == mode)
        return;

    settings.wifiMode = mode;

    Preferences prefs;
    prefs.begin(PREF_NAME, false);
    prefs.putUChar("wifiMode", mode);
    prefs.end();

    DEBUG("[WIFI] mode set to %u (rebooting)\n", mode);

    ESP.restart();   // required to re-init stack cleanly
}

void setCANLogMask(uint32_t mask)
{
    if (settings.canLogMask == mask)
        return;

    settings.canLogMask = mask;

    Preferences prefs;
    prefs.begin(PREF_NAME, false);
    prefs.putUInt("canLogMask", mask);
    prefs.end();

    DEBUG("[CANLOG] updated: %lu\n", mask);
}

void changePrefsString(const char *key, const char *str)
{
    // Persist
    Preferences prefs;
    prefs.begin(PREF_NAME, false);
    prefs.putString(key, str);
    DEBUG("%s settings updated to: %s\n", key, prefs.getString(key));
    prefs.end();
}

void setLedEnabled(bool en)
{
    if (settings.ledEnabled == en)
        return;

    settings.ledEnabled = en;

    // Persist
    Preferences prefs;
    prefs.begin(PREF_NAME, false);
    prefs.putBool("ledEnabled", en);
    prefs.end();

    // Apply immediately
    ledSetEnabled(en);

    DEBUG("[LED] %s (persisted)\n", en ? "ON" : "OFF");
}
