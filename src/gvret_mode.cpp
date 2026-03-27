#include "gvret_mode.h"
#include "can_rx_buffer.h"
#include "can_driver.h"
#include "app_mode.h"
#include <Arduino.h>

static bool busEnabled = false;
static bool handshakeDone = false;

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
void handleSerialInput()
{
    while (Serial.available())
    {
        uint8_t b = Serial.read();
        processIncomingByte(b);
    }
}

// ===== STREAM CAN → GVRET =====
void streamCAN()
{
    if (!busEnabled || !handshakeDone)
        return;

    CANRxItem item;

    while (rxBufferPop(item))
    {
        // encode GVRET frame
        // Serial.write(...)
    }
}

// ===== MAIN LOOP =====
void gvretLoop()
{
    handleSerialInput();   // 🔴 command handling
    streamCAN();           // 🔴 data streaming
}