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

static void sendOBDResponse(uint32_t id, const uint8_t* data, uint8_t len)
{
    if (len > 7) return; // single-frame limit

    // --- SINGLE FRAME ONLY (for now) ---
    twai_message_t tx = {};

    tx.identifier = id;
    tx.extd = 0;
    tx.rtr = 0;

    // PCI = payload length (ISO-TP single frame)
    tx.data[0] = len;

    // copy payload
    memcpy(&tx.data[1], data, len);

    tx.data_length_code = len + 1;

    CANDriver::send(tx);
}

static bool handlePID(uint8_t mode, uint8_t pid, uint8_t* out, uint8_t& len)
{
    if (mode != 0x01) return false;

    switch (pid)
    {
    case 0x00: // supported PIDs
        out[0] = 0x41;
        out[1] = 0x00;
        out[2] = 0x08; // fake support bitmap
        out[3] = 0x10;
        out[4] = 0x00;
        out[5] = 0x00;
        len = 6;
        return true;

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
    }

    return false;
}

static void handleOBDRequest(const twai_message_t& rx)
{
    if (rx.identifier != 0x7DF) return;
    if (rx.data_length_code < 3) return;

    uint8_t mode = rx.data[1];
    uint8_t pid  = rx.data[2];

    uint8_t payload[8];
    uint8_t len = 0;

    if (!handlePID(mode, pid, payload, len)) return;

    // --- ISO-TP SINGLE FRAME ---
    sendOBDResponse(0x7E8, payload, len);
    
}

void generatorLoop()
{
    if (!appState.running) return;

    uint64_t now = esp_timer_get_time();

    if (appState.mode == MODE_SLOW)
    {
        if (now - lastSlowUs < 3000000ULL) return;
        lastSlowUs = now;
    }
    else if (appState.delay_us > 0)
    {
        if (now - lastFrameUs < (uint64_t)appState.delay_us) return;
    }
    else if (appState.target_fps > 0)
    {
        uint32_t interval = 1000000ULL / appState.target_fps;
        if (now - lastFrameUs < interval) return;
    }

    twai_message_t msg = {};
    msg.extd = canFrameCfg.extended ? 1 : 0;
    msg.rtr = 0;
    msg.data_length_code = 8;

    uint32_t id = appState.locked_id >= 0 ? appState.locked_id : currentId;
    if (canFrameCfg.extended) id |= 0x100;
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

        if (appState.locked_id < 0) currentId = (currentId + 1) % 10;
    }

    if (now - lastFpsUs >= 1000000ULL)
    {
        lastFpsUs = now;
        DEBUG("FPS: %lu\n", frameCount);
        frameCount = 0;
    }
}

void ecuLoop()
{
    if (!appState.running) return;

    CANRxItem item;

    while (rxBufferPop(item))
    {
        handleOBDRequest(item.msg);
    }
}
