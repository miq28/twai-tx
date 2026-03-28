#include "gvret_mode.h"
#include "can_rx_buffer.h"
#include "can_driver.h"
#include "app_mode.h"
#include <Arduino.h>
#include "gvret_protocol.h"
#include "gvret_encoder.h"

static bool busEnabled = false;
static bool handshakeDone = false;

static GVRETProtocol gvret;

// ===== BYTE-BY-BYTE COMMAND PARSER =====
void processIncomingByte(uint8_t b)
{
    // TODO: implement GVRET command parsing
    // e.g:
    // - handshake detection
    // - baud change
    // - listen mode
    // - bus enable/disable
}

// ===== SERIAL RX =====
static void handleSerialInput()
{
    while (Serial.available())
    {
        gvret.handleByte(Serial.read());

        // trigger immediate response
        // gvretForceFlush = true;
    }
}

// ===== STREAM CAN → GVRET =====
static void streamCAN()
{
    if (!gvret.isHandshakeDone() || !gvret.isBusEnabled())
        return;

    CANRxItem item;

    while (rxBufferPop(item))
    {
        gvretEncoder.encode(item);
    }
}

// ===== MAIN LOOP =====
void gvretLoop()
{
    handleSerialInput();
    streamCAN();
}

void resetGvretEnum()
{
    gvret.reset();
}