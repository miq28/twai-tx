#include "can_pipeline.h"
#include "transport.h"
#include "can_driver.h"
#include "analyzer.h"
#include "gvret.h"
#include "command.h"
#include "app_mode.h"
#include "rs485.h"
#include "generator_mode.h"
#include "mode_ecu.h"
#include "transport_manager.h"
#include "transport_serial.h"

void setup()
{
    transportInit();
    transportManagerInit();
    transportManagerAdd(&serialTransport);
    RS485.begin(2000000);
    RS485.println("BOOT");

    initAppState();

    CANDriver::init(500000, false);
    canPipelineInit();

    RS485.println("INIT DONE");
}

void loop()
{
    transportManagerProcessRx();

    CANRxItem item;
    int budget = 128;

    while (budget-- && canRxPop(item))
    {
        if (appState.mode == MODE_ANALYZER)
            analyzerPush(item);
        else if (appState.mode == MODE_SAVVYCAN)
            gvretPush(item);
    }

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
        analyzerProcess();
        break;

    case MODE_SAVVYCAN:
        gvretProcess();
        break;
    }

    commandProcess();

    static uint32_t last = 0;

    if (millis() - last > 1000)
    {
        last = millis();
        RS485.printf("%lu mode=%d overflow=%lu\n",
                     micros(),
                     appState.mode,
                     canGetRxOverflow());
    }
}