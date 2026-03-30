#include "gvret_mode.h"
#include "can_driver.h"
#include "app_mode.h"
#include <Arduino.h>
#include "gvret_stream.h"
#include "rs485.h"

static bool binaryMode = false;
static uint32_t lastActivityMs = 0;
static bool savvyConnected = false;

// ===== STATE MACHINE =====
enum GVRET_STATE
{
    IDLE,
    GET_COMMAND,
    SETUP_CANBUS
};

/*
enum STATE {
    IDLE,
    GET_COMMAND,
    BUILD_CAN_FRAME,
    TIME_SYNC,
    GET_DIG_INPUTS,
    GET_ANALOG_INPUTS,
    SET_DIG_OUTPUTS,
    SETUP_CANBUS,
    GET_CANBUS_PARAMS,
    GET_DEVICE_INFO,
    SET_SINGLEWIRE_MODE,
    SET_SYSTYPE,
    ECHO_CAN_FRAME,
    SETUP_EXT_BUSES
};
*/

enum GVRET_PROTOCOL
{
    PROTO_BUILD_CAN_FRAME = 0,
    PROTO_TIME_SYNC = 1,
    PROTO_DIG_INPUTS = 2,
    PROTO_ANA_INPUTS = 3,
    PROTO_SET_DIG_OUT = 4,
    PROTO_SETUP_CANBUS = 5,
    PROTO_GET_CANBUS_PARAMS = 6,
    PROTO_GET_DEV_INFO = 7,
    PROTO_SET_SW_MODE = 8,
    PROTO_KEEPALIVE = 9,
    PROTO_SET_SYSTYPE = 10,
    PROTO_ECHO_CAN_FRAME = 11,
    PROTO_GET_NUMBUSES = 12,
    PROTO_GET_EXT_BUSES = 13,
    PROTO_SET_EXT_BUSES = 14,
    PROTO_BUILD_FD_FRAME = 20,
    PROTO_SETUP_FD = 21,
    PROTO_GET_FD = 22,
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
    case PROTO_TIME_SYNC: // 1
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

    case PROTO_SETUP_CANBUS: // 5
        state = SETUP_CANBUS;
        step = 0;
        break;

    case PROTO_GET_CANBUS_PARAMS: // 6
        sendCANConfig();
        state = IDLE;
        break;

    case PROTO_GET_DEV_INFO: // 7
        sendDeviceInfo();
        state = IDLE;
        break;

    case PROTO_KEEPALIVE: // 9
        lastActivityMs = millis();
        savvyConnected = true;
        RS485.printf("SavvyCan keep-alive received, time: %lu\n", millis());
        sendKeepAlive();
        state = IDLE;
        break;

    case PROTO_GET_NUMBUSES: // 12
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
            lastActivityMs = millis();
            // enter binary mode (no response required)
            binaryMode = true;
            RS485.println("Entering binary mode");
            if (appState.mode != MODE_SAVVYCAN)
            {
                appState.mode = MODE_SAVVYCAN;
                RS485.println("Mode switched to SAVVYCAN");
            }
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

// ===== MAIN LOOP =====

void gvretLoop()
{
    // timeout = 10 seconds (adjust if needed)
    if (millis() - lastActivityMs > 10300)
    {
        savvyConnected = false;
        binaryMode = false;
        RS485.println("SavvyCan connection lost, exiting binary mode");
        appState.mode = MODE_ANALYZER;
        RS485.println("Mode switched to ANALYZER");
    }

    if (CANDriver::isRunning() && savvyConnected)
    {
        gvretStream();
    }
}