#include "config.h"
#include "can_bus.h"
#include <Preferences.h>
// #include <nvs_flash.h>

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