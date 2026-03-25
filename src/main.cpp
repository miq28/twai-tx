#include <Arduino.h>
#include "can_driver.h"
#include "serial_cmd.h"
#include "app_mode.h"
#include "generator_mode.h"
#include "mode_ecu.h"

void setup()
{
    serialCmdInit();
    initAppState();
    CANDriver::init(500000, false);
}

void loop()
{
    serialCmdProcess();

    switch (appState.mode)
    {
    case MODE_GENERATOR:
        generatorLoop();
        break;

    case MODE_SLOW:
        generatorLoop();
        break;

    case MODE_ECU:
        ecuLoop();
        break;
    }
}