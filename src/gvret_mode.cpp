#include "gvret_mode.h"
#include "app_mode.h"
#include "can_bus.h"
#include <Arduino.h>
#include "debug.h"
#include "transport.h"

static bool binaryMode = false;
static uint32_t lastActivityMs = 0;
static bool savvyConnected = false;
static twai_message_t txMsg;

// ===== STATE MACHINE =====

enum GVRET_STATE {
    IDLE,
    GET_COMMAND,
    BUILD_CAN_FRAME,
    // TIME_SYNC,
    // GET_DIG_INPUTS,
    // GET_ANALOG_INPUTS,
    // SET_DIG_OUTPUTS,
    SETUP_CANBUS,
    // GET_CANBUS_PARAMS,
    // GET_DEVICE_INFO,
    // SET_SINGLEWIRE_MODE,
    // SET_SYSTYPE,
    // ECHO_CAN_FRAME,
    // SETUP_EXT_BUSES
};

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
    transportWrite(resp, sizeof(resp));
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
    transportWrite(resp, sizeof(resp));
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

    transportWrite(resp, 12);
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

        transportWrite(resp, sizeof(resp));
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
        DEBUG_PRINTLN("SavvyCan keep-alive received");
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
        transportWrite(resp, sizeof(resp));
        state = IDLE;
        break;
    }

    case PROTO_BUILD_CAN_FRAME: // 0
        state = BUILD_CAN_FRAME;
        step = 0;
        break;

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

        transportWrite(resp, sizeof(resp));

        state = IDLE;
        break;
    }
    }

    step++;
}

// ===== BUILD CAN FRAME =====

static uint8_t out_bus = 0;

static void handleBuildCAN(uint8_t b)
{
    switch (step)
    {
    case 0: txMsg.identifier = b; break;
    case 1: txMsg.identifier |= b << 8; break;
    case 2: txMsg.identifier |= b << 16; break;

    case 3:
        txMsg.identifier |= b << 24;

        if (txMsg.identifier & (1UL << 31))
        {
            txMsg.identifier &= 0x7FFFFFFF;
            txMsg.extd = 1;
        }
        else
        {
            txMsg.extd = 0;
        }
        break;

    case 4:
        out_bus = b & 0x3;   // not used, but required to consume byte
        break;

    case 5:
    {
        uint8_t dlc = b & 0xF;
        if (dlc > 8) dlc = 8;

        txMsg.data_length_code = dlc;
        txMsg.rtr = 0;
        break;
    }

    default:
        if (step < (6 + txMsg.data_length_code))
        {
            txMsg.data[step - 6] = b;
        }
        else
        {
            // last byte = checksum → ignore
            CANDriver::send(txMsg);   // ✅ REAL FIX
            state = IDLE;
        }
        break;
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
            DEBUG_PRINTLN("Entering binary mode");
            if (appState.mode != MODE_SAVVYCAN)
            {
                appState.mode = MODE_SAVVYCAN;
                DEBUG_PRINTLN("Mode switched to SAVVYCAN");
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

    case BUILD_CAN_FRAME:
        handleBuildCAN(b);
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
        DEBUG_PRINTLN("SavvyCan connection lost, exiting binary mode");
        CANRxBuffer::clear();   // 🔥 important
        appState.mode = MODE_ANALYZER;
        DEBUG_PRINTLN("Mode switched to ANALYZER");
    }

    if (CANDriver::isRunning() && savvyConnected)
    {
        CANRxItem item;

        while (CANRxBuffer::pop(item))
        {
            const twai_message_t &m = item.msg;
            uint8_t buf[32];
            int idx = 0;

            buf[idx++] = 0xF1;
            buf[idx++] = 0x00;

            uint32_t ts = item.timestamp;
            buf[idx++] = ts & 0xFF;
            buf[idx++] = ts >> 8;
            buf[idx++] = ts >> 16;
            buf[idx++] = ts >> 24;

            uint32_t id = m.identifier;
            if (m.extd) id |= (1UL << 31);

            buf[idx++] = id & 0xFF;
            buf[idx++] = id >> 8;
            buf[idx++] = id >> 16;
            buf[idx++] = id >> 24;

            uint8_t dlc = m.data_length_code & 0xF;
            buf[idx++] = dlc;

            for (int i = 0; i < dlc; i++) buf[idx++] = m.data[i];

            buf[idx++] = 0;
            transportWrite(buf, idx);
        }
    }
}
