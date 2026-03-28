#include "gvret_mode.h"
#include "can_driver.h"
#include "app_mode.h"
#include <Arduino.h>
#include "gvret_stream.h"

static bool binaryMode = false;

// ===== STATE MACHINE =====
enum GVRET_STATE
{
    IDLE,
    GET_COMMAND,
    SETUP_CANBUS
};

static GVRET_STATE state = IDLE;
static uint8_t step = 0;
static uint32_t build_int = 0;

// ===== RESPONSES =====

static void sendKeepAlive()
{
    uint8_t resp[] = {0xF1, 9, 0xDE, 0xAD};
    Serial.write(resp, sizeof(resp));
}

static void sendDeviceInfo()
{
    uint8_t resp[] = {
        0xF1,
        7,
        0x34, 0x12, // build num
        0x20,       // EEPROM ver (important)
        0,          // file output type
        0,          // auto log
        0           // single wire
    };
    Serial.write(resp, sizeof(resp));
}

static void sendCANConfig()
{
    uint8_t resp[12] = {0};

    resp[0] = 0xF1;
    resp[1] = 6;

    resp[2] = 0;

    if (CANDriver::isRunning())
        resp[2] |= (1 << 0);

    if (CANDriver::isListenOnly())
        resp[2] |= (1 << 4);

    uint32_t baud = CANDriver::getCurrentBaud();

    resp[3] = baud & 0xFF;
    resp[4] = baud >> 8;
    resp[5] = baud >> 16;
    resp[6] = baud >> 24;

    // CAN1 (unused)
    resp[7] = 0;
    resp[8] = 0;
    resp[9] = 0;
    resp[10] = 0;
    resp[11] = 0;

    Serial.write(resp, 12);
}

// ===== COMMAND HANDLER =====

static void handleCommand(uint8_t cmd)
{
    switch (cmd)
    {
    case 1: // PROTO_TIME_SYNC
    {
        uint32_t now = micros();

        uint8_t resp[] = {
            0xF1,
            1,
            (uint8_t)(now & 0xFF),
            (uint8_t)(now >> 8),
            (uint8_t)(now >> 16),
            (uint8_t)(now >> 24)};

        Serial.write(resp, sizeof(resp));
        state = IDLE;
        break;
    }

    case 9: // KEEPALIVE
        sendKeepAlive();
        state = IDLE;
        break;

    case 7: // GET DEVICE INFO
        sendDeviceInfo();
        state = IDLE;
        break;

    case 6: // GET CAN CONFIG
        sendCANConfig();
        state = IDLE;
        break;

    case 5: // SETUP CAN BUS
        state = SETUP_CANBUS;
        step = 0;
        break;

    case 12: // PROTO_GET_NUMBUSES
    {
        uint8_t resp[] = {
            0xF1,
            12,
            1 // number of CAN buses (you have 1)
        };
        Serial.write(resp, sizeof(resp));
        state = IDLE;
        break;
    }

    default:
        state = IDLE;
        break;
    }
}

// ===== SETUP CAN BUS =====

static void handleSetupCAN(uint8_t b)
{
    switch (step)
    {
    case 0:
        build_int = b;
        break;
    case 1:
        build_int |= b << 8;
        break;
    case 2:
        build_int |= b << 16;
        break;

    case 3:
    {
        build_int |= b << 24;

        uint32_t baud = build_int & 0xFFFFF;

        bool enable = build_int & 0x40000000UL;
        bool listen = build_int & 0x20000000UL;

        if (enable)
        {
            CANDriver::reinit(baud, listen);
        }
        break;
    }

    case 7:
        state = IDLE;
        break;

    case 13: // PROTO_GET_EXT_BUSES
    {
        uint8_t resp[1 + 1 + 15] = {0};

        resp[0] = 0xF1;
        resp[1] = 13;

        // 15 bytes of zeros (no extra buses)
        for (int i = 0; i < 15; i++)
            resp[2 + i] = 0;

        Serial.write(resp, sizeof(resp));

        state = IDLE;
        break;
    }
    }

    step++;
}

// ===== BYTE PARSER =====

void processIncomingByte(uint8_t b)
{
    switch (state)
    {
    case IDLE:
        if (b == 0xE7)
        {
            // enter binary mode (no response required)
            binaryMode = true;
            return;
        }
        if (b == 0xF1)
        {
            state = GET_COMMAND;
        }
        break;

    case GET_COMMAND:
        handleCommand(b);
        break;

    case SETUP_CANBUS:
        handleSetupCAN(b);
        break;

    default:
        state = IDLE;
        break;
    }
}

// ===== SERIAL RX =====

void handleSerialInput()
{
    while (Serial.available())
    {
        processIncomingByte(Serial.read());
    }
}

// ===== STREAM (DISABLED FOR NOW) =====

void streamCAN()
{
    // NOT YET
}

// ===== MAIN LOOP =====

void gvretLoop()
{
    handleSerialInput();
    if (CANDriver::isRunning() && binaryMode)
    {
        gvretStream();
    }
}