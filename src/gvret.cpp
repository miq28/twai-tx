#include "gvret.h"
#include "can_pipeline.h"
#include "transport.h"
#include "can_driver.h"
#include "rs485.h"

#define Q_SIZE 512

static CANRxItem q[Q_SIZE];
static uint16_t h=0,t=0;

// ================= RX QUEUE =================
void gvretPush(const CANRxItem& item)
{
    uint16_t n=(h+1)%Q_SIZE;
    if(n==t) return;
    q[h]=item;
    h=n;
}

// ================= TX STREAM =================
void gvretProcess()
{
    CANRxItem item;
    int budget = 32;

    while (budget-- && t != h)
    {
        item = q[t];
        t = (t + 1) % Q_SIZE;

        uint8_t buf[32];
        int idx = 0;

        buf[idx++] = 0xF1;
        buf[idx++] = 0;

        uint32_t ts = item.timestamp;
        buf[idx++] = ts;
        buf[idx++] = ts>>8;
        buf[idx++] = ts>>16;
        buf[idx++] = ts>>24;

        uint32_t id = item.msg.identifier;
        if (item.msg.extd) id |= (1UL << 31);

        buf[idx++] = id;
        buf[idx++] = id>>8;
        buf[idx++] = id>>16;
        buf[idx++] = id>>24;

        uint8_t dlc = item.msg.data_length_code;
        buf[idx++] = dlc;

        for (int i=0;i<dlc;i++)
            buf[idx++] = item.msg.data[i];

        buf[idx++] = 0;

        transportSend(buf, idx);
    }
}

// ================= STATE MACHINE =================

enum {
    IDLE,
    GET_CMD,
    BUILD_FRAME,
    SETUP_CAN
};

static uint8_t state = IDLE;
static uint8_t cmd = 0;
static uint8_t step = 0;

static uint8_t frameBuf[16];
static uint8_t frameIdx = 0;
static uint8_t frameLen = 0;

static uint32_t build_int = 0;

// ================= RESPONSES =================

static void sendKeepAlive()
{
    uint8_t resp[] = {0xF1, 9, 0xDE, 0xAD};
    transportSend(resp, sizeof(resp));
}

static void sendDeviceInfo()
{
    uint8_t resp[] = {
        0xF1,
        7,
        0x34, 0x12,
        0x20,
        0,0,0
    };
    transportSend(resp, sizeof(resp));
}

static void sendCANConfig()
{
    uint8_t resp[12] = {0};

    resp[0] = 0xF1;
    resp[1] = 6;

    if (CANDriver::isRunning())
        resp[2] |= (1 << 0);

    if (CANDriver::isListenOnly())
        resp[2] |= (1 << 4);

    uint32_t baud = CANDriver::getCurrentBaud();

    resp[3] = baud;
    resp[4] = baud >> 8;
    resp[5] = baud >> 16;
    resp[6] = baud >> 24;

    transportSend(resp, 12);
}

// ================= INPUT =================

void gvretProcessByte(uint8_t b)
{
    switch (state)
    {
    case IDLE:
        if (b == 0xE7)
        {
            RS485.println("[GVRET] binary mode");
            return;
        }
        if (b == 0xF1)
        {
            state = GET_CMD;
        }
        break;

    case GET_CMD:
        cmd = b;

        switch (cmd)
        {
        case 0: // BUILD FRAME
            state = BUILD_FRAME;
            frameIdx = 0;
            break;

        case 5: // SETUP CAN
            state = SETUP_CAN;
            step = 0;
            build_int = 0;
            break;

        case 6:
            sendCANConfig();
            state = IDLE;
            break;

        case 7:
            sendDeviceInfo();
            state = IDLE;
            break;

        case 9:
            sendKeepAlive();
            state = IDLE;
            break;

        default:
            state = IDLE;
            break;
        }
        break;

    case SETUP_CAN:
        build_int |= ((uint32_t)b << (step * 8));
        step++;

        if (step == 4)
        {
            uint32_t baud = build_int & 0xFFFFF;
            bool enable = build_int & 0x40000000UL;
            bool listen = build_int & 0x20000000UL;

            if (enable)
                CANDriver::reinit(baud, listen);

            state = IDLE;
        }
        break;

    case BUILD_FRAME:
        frameBuf[frameIdx++] = b;

        if (frameIdx == 5)
            frameLen = frameBuf[4] & 0xF;

        if (frameIdx >= 5 + frameLen)
        {
            twai_message_t msg = {};

            uint32_t id =
                frameBuf[0] |
                (frameBuf[1] << 8) |
                (frameBuf[2] << 16) |
                (frameBuf[3] << 24);

            msg.extd = (id >> 31) & 1;
            msg.identifier = id & 0x1FFFFFFF;
            msg.data_length_code = frameLen;

            for (int i=0;i<frameLen;i++)
                msg.data[i] = frameBuf[5+i];

            canTxPush(msg);
            state = IDLE;
        }
        break;
    }
}