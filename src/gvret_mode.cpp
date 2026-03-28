#include "gvret_mode.h"
#include "can_rx_buffer.h"
#include "can_driver.h"
#include "app_mode.h"
#include <Arduino.h>
#include "gvret_encoder.h"

enum GVRET_CMD
{
    CMD_CONNECT      = 0x01,
    CMD_ENABLE_BUS   = 0x02,
    CMD_DISABLE_BUS  = 0x03,
    CMD_SET_BAUD     = 0x04,
    CMD_LISTEN       = 0x05
};

static ICanEncoder* encoder = &gvretEncoder;

static bool busEnabled = false;
static bool handshakeDone = false;

enum ParseState {
    WAIT_SYNC,
    READ_LEN,
    READ_DATA
};

static ParseState state = WAIT_SYNC;
static uint8_t buf[32];
static uint8_t idx = 0;
static uint8_t expectedLen = 0;

static void handleGVRETCommand(uint8_t* data, uint8_t len)
{
    uint8_t cmd = data[0];

    switch (cmd)
    {
    case CMD_CONNECT:
        handshakeDone = true;
        Serial.write((uint8_t*)"\xF1\x02\x01\x01", 4); // simple ACK
        break;

    case CMD_ENABLE_BUS:
        busEnabled = true;
        break;

    case CMD_DISABLE_BUS:
        busEnabled = false;
        break;

    case CMD_SET_BAUD:
    {
        if (len >= 5)
        {
            uint32_t baud =
                (data[1]) |
                (data[2] << 8) |
                (data[3] << 16) |
                (data[4] << 24);

            CANDriver::reinit(baud, false);
        }
        break;
    }

    case CMD_LISTEN:
    {
        if (len >= 2)
        {
            bool listen = data[1];
            CANDriver::reinit(canState.baud, listen);
        }
        break;
    }
    }
}

// ===== BYTE-BY-BYTE COMMAND PARSER =====
void processIncomingByte(uint8_t b)
{
    switch (state)
    {
    case WAIT_SYNC:
        if (b == 0xF1)
        {
            idx = 0;
            state = READ_LEN;
        }
        break;

    case READ_LEN:
        expectedLen = b;
        idx = 0;
        state = READ_DATA;
        break;

    case READ_DATA:
        buf[idx++] = b;

        if (idx >= expectedLen)
        {
            handleGVRETCommand(buf, expectedLen);
            state = WAIT_SYNC;
        }
        break;
    }
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
        encoder->encode(item);
    }
}

// ===== MAIN LOOP =====
void gvretLoop()
{
    handleSerialInput();   // 🔴 command handling
    streamCAN();           // 🔴 data streaming
}