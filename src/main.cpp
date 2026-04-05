#include <Arduino.h>
#include "can_bus.h"
#include "transport.h"
#include "app_mode.h"
#include "traffic_modes.h"
#include "analyzer_mode.h"
#include "gvret_mode.h"
#include "rs485.h"

void setup()
{
    transportInit();
    initAppState();
    RS485.begin(2000000);
    CANDriver::init(500000, false);
    startCanRxTask();
    analyzerInit();
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
