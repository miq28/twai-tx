#include <Arduino.h>
#include "can_driver.h"
#include "transport_serial.h"
#include "app_mode.h"
#include "generator_mode.h"
#include "mode_ecu.h"
#include "can_rx_task.h"
#include "analyzer_mode.h"

void setup()
{
    transportSerialInit(); // ✅ FIX
    initAppState();
    CANDriver::init(500000, false);
    startCanRxTask(); // ← ADD THIS LINE
}

void loop()
{
    transportSerialProcess(); // ✅ FIX

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
    }
}