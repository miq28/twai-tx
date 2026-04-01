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

extern void transportTxTask(void *);
extern void canTxTask(void *);
extern void canRouterProcess();
extern void commandTask(void*);

void setup()
{
    transportSerialInit();
    initAppState();
    RS485.begin(1000000);
    CANDriver::init(500000, false);
    startCanRxTask();
    analyzerInit();

    xTaskCreatePinnedToCore(transportTxTask, "tx_out", 4096, NULL, 8, NULL, 0);
    xTaskCreatePinnedToCore(canTxTask, "can_tx", 4096, NULL, 18, NULL, 1);
    xTaskCreatePinnedToCore(commandTask, "cmd", 4096, NULL, 6, NULL, 0);
}

void loop()
{
    transportSerialProcess();   // input only
    canRouterProcess();         // NEW

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