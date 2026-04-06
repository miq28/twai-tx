#include "traffic_modes.h"
#include "app_mode.h"
#include "can_bus.h"
#include <Arduino.h>
#include <esp_timer.h>
#include "debug.h"

namespace
{
    uint8_t currentId = 0;
    uint32_t counter = 0;
    uint64_t lastFrameUs = 0;
    uint64_t lastSlowUs = 0;
    uint32_t frameCount = 0;
    uint64_t lastFpsUs = 0;

    uint64_t last10ms = 0;
    uint64_t last20ms = 0;
    uint64_t last1000ms = 0;
    uint16_t rpm = 800;
    uint16_t speed = 0;
}

static const char VIN[] = "WP0ZZZ99ZTS392124"; // 17 chars

static uint32_t buildPidBitmap01_20()
{
    uint32_t bm = 0;

// helper macro: set bit for PID
#define SET_PID(pid) bm |= (1UL << (0x20 - (pid)))

    SET_PID(0x0C); // RPM
    SET_PID(0x0D); // Speed

#undef SET_PID

    return bm;
}

static uint16_t encodeDTC(char type, uint16_t code)
{
    uint16_t t = 0;

    switch (type)
    {
    case 'P':
        t = 0;
        break;
    case 'C':
        t = 1;
        break;
    case 'B':
        t = 2;
        break;
    case 'U':
        t = 3;
        break;
    }

    return (t << 14) | (code & 0x3FFF);
}

bool fcReceived;

struct IsoTpTxState
{
    bool active;
    uint32_t id;

    const uint8_t *data;
    uint16_t len;

    uint16_t offset;
    uint8_t seq;

    uint32_t lastSendUs;

    // ===== FLOW CONTROL =====
    bool fcReceived;
    uint8_t blockSize; // BS
    uint8_t stMin;     // STmin (raw)
    uint8_t bsCounter; // frames sent in current block
};

static IsoTpTxState isoTx = {};

struct IsoTpRxState
{
    bool active;

    uint32_t id;       // sender (tester 0x7E0)
    uint16_t expected; // total length
    uint16_t len;

    uint8_t nextSeq;
    uint8_t buf[256];
};

static IsoTpRxState isoRx = {};

static void sendFlowControl(uint32_t id)
{
    twai_message_t fc = {};
    fc.identifier = id;
    fc.extd = 0;
    fc.rtr = 0;
    fc.data_length_code = 8;

    fc.data[0] = 0x30; // CTS
    fc.data[1] = 0x00; // BS (0 = unlimited)
    fc.data[2] = 0x00; // STmin

    CANDriver::send(fc);
}

static void sendOBDResponse(uint32_t id, const uint8_t *data, uint8_t len)
{
    // --- SINGLE FRAME ---
    if (len <= 7)
    {
        twai_message_t tx = {};

        tx.identifier = id;
        tx.extd = 0;
        tx.rtr = 0;

        tx.data[0] = len;
        memcpy(&tx.data[1], data, len);

        tx.data_length_code = len + 1;

        CANDriver::send(tx);
        return;
    }

    // --- MULTI FRAME (start) ---
    isoTx.active = true;
    isoTx.fcReceived = false;

    isoTx.blockSize = 0;
    isoTx.stMin = 0;
    isoTx.bsCounter = 0;

    isoTx.lastSendUs = 0;

    isoTx.id = id;
    isoTx.data = data;
    isoTx.len = len;
    isoTx.offset = 6;
    isoTx.seq = 1;

    // First Frame
    twai_message_t tx = {};

    tx.identifier = id;
    tx.extd = 0;
    tx.rtr = 0;

    tx.data[0] = 0x10 | ((len >> 8) & 0x0F); // FF
    tx.data[1] = len & 0xFF;

    memcpy(&tx.data[2], data, 6);

    tx.data_length_code = 8;

    CANDriver::send(tx);
}

static bool handlePID(uint8_t mode, uint8_t pid, uint8_t *out, uint8_t &len)
{
    if (mode == 0x01)
    {
        switch (pid)
        {
        case 0x00:
        {
            uint32_t bm = buildPidBitmap01_20();

            out[0] = 0x41;
            out[1] = 0x00;
            out[2] = (bm >> 24) & 0xFF;
            out[3] = (bm >> 16) & 0xFF;
            out[4] = (bm >> 8) & 0xFF;
            out[5] = bm & 0xFF;

            len = 6;
            return true;
        }

        case 0x0C: // RPM
        {
            uint16_t rpm = 3000;
            uint16_t v = rpm * 4;

            out[0] = 0x41;
            out[1] = 0x0C;
            out[2] = v >> 8;
            out[3] = v & 0xFF;
            len = 4;
            return true;
        }

        case 0x0D: // speed
            out[0] = 0x41;
            out[1] = 0x0D;
            out[2] = 88;
            len = 3;
            return true;

        case 0x20:
        {
            // no support beyond 0x20
            out[0] = 0x41;
            out[1] = 0x20;
            out[2] = 0x00;
            out[3] = 0x00;
            out[4] = 0x00;
            out[5] = 0x00;

            len = 6;
            return true;
        }
        }
    }

    // ===== MODE 09 =====
    else if (mode == 0x09)
    {
        switch (pid)
        {
        case 0x02: // VIN
        {
            // response: 49 02 01 + VIN (17 bytes)

            out[0] = 0x49;
            out[1] = 0x02;
            out[2] = 0x01;

            memcpy(&out[3], VIN, 17);

            len = 20; // 3 + 17 → forces multi-frame
            return true;
        }
        }
    }

    // ===== MODE 03 (Stored DTCs) =====
    if (mode == 0x03)
    {
        // Example: 2 DTCs
        uint16_t dtc1 = encodeDTC('P', 0x0300); // P0300 (random misfire)
        uint16_t dtc2 = encodeDTC('P', 0x0133); // P0133 (O2 slow response)

        out[0] = 0x43; // response for Mode 03

        out[1] = dtc1 >> 8;
        out[2] = dtc1 & 0xFF;

        out[3] = dtc2 >> 8;
        out[4] = dtc2 & 0xFF;

        len = 5; // 1 + 2*2 bytes

        return true;
    }

    return false;
}

static void handleOBDRequest(const twai_message_t &rx)
{
    if (rx.identifier != 0x7DF)
        return;
    if (rx.data_length_code < 3)
        return;

    uint8_t mode = rx.data[1];
    uint8_t pid = rx.data[2];

    static uint8_t payload[64];
    uint8_t len = 0;

    if (!handlePID(mode, pid, payload, len))
        return;

    // --- ISO-TP SINGLE FRAME ---
    sendOBDResponse(0x7E8, payload, len);
}

static inline uint32_t decodeSTminUs(uint8_t st)
{
    if (st <= 0x7F)
        return st * 1000; // ms → us

    if (st >= 0xF1 && st <= 0xF9)
        return (st - 0xF0) * 100; // 100–900 us

    return 0; // reserved → no delay
}

static void processIsoTpTx()
{
    if (!isoTx.active)
        return;

    // wait FC
    if (!isoTx.fcReceived)
        return;

    uint32_t nowUs = micros();

    // ===== STmin timing =====
    uint32_t stMinUs = decodeSTminUs(isoTx.stMin);

    if (stMinUs > 0 && (nowUs - isoTx.lastSendUs) < stMinUs)
        return;

    // ===== Block Size handling =====
    if (isoTx.blockSize != 0 && isoTx.bsCounter >= isoTx.blockSize)
    {
        // wait next FC
        isoTx.fcReceived = false;
        isoTx.bsCounter = 0;
        DEBUG("[ISO-TP] Waiting next FC\n");
        return;
    }

    // ===== Done =====
    if (isoTx.offset >= isoTx.len)
    {
        isoTx.active = false;
        DEBUG("[ISO-TP] TX complete\n");
        return;
    }

    // ===== Send CF =====
    twai_message_t tx = {};
    tx.identifier = isoTx.id;

    tx.data[0] = 0x20 | (isoTx.seq & 0x0F);

    uint8_t remaining = isoTx.len - isoTx.offset;
    uint8_t chunk = remaining > 7 ? 7 : remaining;

    memcpy(&tx.data[1], isoTx.data + isoTx.offset, chunk);
    tx.data_length_code = chunk + 1;

    CANDriver::send(tx);

    isoTx.offset += chunk;
    isoTx.seq++;
    if (isoTx.seq > 0x0F)
        isoTx.seq = 1;

    isoTx.lastSendUs = nowUs;
    isoTx.bsCounter++;
}

void generatorLoop()
{
    if (!appState.running)
        return;

    uint64_t now = esp_timer_get_time();

    if (appState.mode == MODE_SLOW)
    {
        if (now - lastSlowUs < 3000000ULL)
            return;
        lastSlowUs = now;
    }
    else if (appState.delay_us > 0)
    {
        if (now - lastFrameUs < (uint64_t)appState.delay_us)
            return;
    }
    else if (appState.target_fps > 0)
    {
        uint32_t interval = 1000000ULL / appState.target_fps;
        if (now - lastFrameUs < interval)
            return;
    }

    twai_message_t msg = {};
    msg.extd = canFrameCfg.extended ? 1 : 0;
    msg.rtr = 0;
    msg.data_length_code = 8;

    uint32_t id = appState.locked_id >= 0 ? appState.locked_id : currentId;
    if (canFrameCfg.extended)
        id |= 0x100;
    msg.identifier = id;

    msg.data[0] = (counter >> 0) & 0xFF;
    msg.data[1] = (counter >> 8) & 0xFF;
    msg.data[2] = (counter >> 16) & 0xFF;
    msg.data[3] = (counter >> 24) & 0xFF;
    msg.data[4] = 0x44;
    msg.data[5] = 0x55;
    msg.data[6] = 0x66;
    msg.data[7] = 0x77;

    if (CANDriver::send(msg))
    {
        counter++;
        frameCount++;
        lastFrameUs = now;

        if (appState.locked_id < 0)
            currentId = (currentId + 1) % 10;
    }

    if (now - lastFpsUs >= 1000000ULL)
    {
        lastFpsUs = now;
        DEBUG("FPS: %lu\n", frameCount);
        frameCount = 0;
    }
}

static void handleIsoTpRx(const twai_message_t &m)
{
    if (m.identifier != 0x7DF && m.identifier != 0x7E0)
        return;

    uint8_t pci = m.data[0] & 0xF0;

    // ===== SINGLE FRAME =====
    if (pci == 0x00)
    {
        uint8_t len = m.data[0];

        handleOBDRequest(m); // reuse existing
        return;
    }

    // ===== FIRST FRAME =====
    if (pci == 0x10)
    {
        isoRx.active = true;
        isoRx.id = m.identifier;

        isoRx.expected = ((m.data[0] & 0x0F) << 8) | m.data[1];

        memcpy(isoRx.buf, &m.data[2], 6);
        isoRx.len = 6;
        isoRx.nextSeq = 1;

        // send FC immediately
        sendFlowControl(0x7E8); // ECU → tester

        return;
    }

    // ===== CONSECUTIVE FRAME =====
    if (pci == 0x20 && isoRx.active)
    {
        uint8_t seq = m.data[0] & 0x0F;

        if (seq != isoRx.nextSeq)
        {
            // sequence error → abort
            isoRx.active = false;
            DEBUG("[ISO-TP RX] seq error\n");
            return;
        }

        uint8_t chunk = m.data_length_code - 1;

        memcpy(isoRx.buf + isoRx.len, &m.data[1], chunk);
        isoRx.len += chunk;

        isoRx.nextSeq++;
        if (isoRx.nextSeq > 0x0F)
            isoRx.nextSeq = 0;

        // complete
        if (isoRx.len >= isoRx.expected)
        {
            isoRx.active = false;

            // build fake CAN msg for existing handler
            twai_message_t fake = {};
            fake.identifier = 0x7DF;
            fake.data_length_code = isoRx.len + 1;

            fake.data[0] = isoRx.len;
            memcpy(&fake.data[1], isoRx.buf, isoRx.len);

            handleOBDRequest(fake);
        }

        return;
    }
}

void ecuLoop()
{
    if (!appState.running)
        return;

    CANRxItem item;

    while (rxBufferPop(item))
    {
        const auto &m = item.msg;

        // --- detect Flow Control (from tester) ---
        if (m.identifier == 0x7E0 && (m.data[0] & 0xF0) == 0x30)
        {
            isoTx.fcReceived = true;

            isoTx.blockSize = m.data[1]; // BS
            isoTx.stMin = m.data[2];     // STmin
            isoTx.bsCounter = 0;

            DEBUG("[ISO-TP] FC: BS=%u STmin=0x%02X\n",
                  isoTx.blockSize, isoTx.stMin);

            continue;
        }

        // handleOBDRequest(m);
        handleIsoTpRx(m);
    }

    // NEW
    processIsoTpTx();
}
