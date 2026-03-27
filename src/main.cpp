#include <Arduino.h>
#include "can_driver.h"
#include "transport_serial.h"
#include "app_mode.h"
#include "generator_mode.h"
#include "mode_ecu.h"
#include "can_rx.h"

#define TYPE_CAN_FRAME 0x01
#define TYPE_STATUS 0x02

struct __attribute__((packed)) StatusMsg
{
    uint32_t tx_drop;
    uint32_t rx_drop;
};

void sendStatus()
{
    static uint32_t last = 0;
    uint32_t now = millis();

    if (now - last >= 1000)
    {
        last = now;

        StatusMsg s;
        s.tx_drop = CANDriver::tx_drop;
        s.rx_drop = canRxGetDropCount();

        uint8_t header[2] = {0xAA, 0x55};
        uint8_t type = TYPE_STATUS;

        Serial.write(header, 2);
        Serial.write(&type, 1);
        Serial.write((uint8_t *)&s, sizeof(s));
    }
}

void setup()
{
    transportSerialInit(); // ✅ FIX
    initAppState();
    CANDriver::init(500000, false);

    canRxInit(); // 🔥 ADD
}

void loop()
{
    transportSerialProcess(); // ✅ FIX

    canRxProcess(); // 🔥 ADD HERE (HIGH PRIORITY)

    switch (appState.mode)
    {
    case MODE_GENERATOR:
    case MODE_SLOW:
        generatorLoop();
        break;

    case MODE_ECU:
        ecuLoop();
        break;
    }

    // 🔥 ADD: send RX frames to serial
    CANRxFrame f;

    uint8_t header[2] = {0xAA, 0x55};
    uint8_t type = TYPE_CAN_FRAME;

    while (canRxPop(f))
    {
        Serial.write(header, 2);
        Serial.write(&type, 1);
        Serial.write((uint8_t *)&f, sizeof(f));
    }

    sendStatus();
}