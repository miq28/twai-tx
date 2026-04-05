#include <Arduino.h>
#include "can_driver.h"
#include "transport_serial.h"
#include "app_mode.h"
#include "generator_mode.h"
#include "mode_ecu.h"
#include "can_rx_task.h"
#include "analyzer_mode.h"
#include "gvret_mode.h"
#include "rs485.h"

void setup()
{
    transportSerialInit();
    initAppState();
    RS485.begin(2000000);
    CANDriver::init(500000, false);
    startCanRxTask();
    analyzerInit();
}

void loop()
{
    transportSerialProcess();   // always run

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