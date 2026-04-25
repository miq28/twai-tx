#include "traffic_modes.h"
#include "app_mode.h"
#include "can_bus.h"
#include <Arduino.h>
#include <esp_timer.h>
#include "debug.h"
#include "led_activity.h"

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

    static void buildFrame(twai_message_t &msg)
    {
        msg = {};
        msg.rtr = 0;

        if (canFrameCfg.pattern == 0)
        {
            // ===== SIMPLE MODE (your original behavior)
            msg.extd = canFrameCfg.extended ? 1 : 0;
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
        }
        else
        {
            // ===== REALISTIC MIXED MODE
            uint32_t r = esp_random();

            bool useExt = (r & 1);
            msg.extd = useExt ? 1 : 0;

            if (useExt)
                msg.identifier = r & 0x1FFFFFFF; // 29-bit
            else
                msg.identifier = r & 0x7FF; // 11-bit

            msg.data_length_code = r % 9;

            for (int i = 0; i < msg.data_length_code; i++)
            {
                msg.data[i] = (r >> (i * 3)) & 0xFF;
            }
        }

        // ===== BONUS SAFETY (VERY IMPORTANT)
        if (msg.data_length_code > 8)
        {
            static uint32_t lastWarn = 0;
            if (millis() - lastWarn > 1000)
            {
                lastWarn = millis();
                DEBUG("[WARN] DLC corrupted: %u\n", msg.data_length_code);
            }
            msg.data_length_code = 8;
        }
    }
}

void generatorLoop()
{
    if (!appState.canTxEnabled)
        return;

    if (CANDriver::getErrorStateRaw() == TWAI_ERROR_BUS_OFF)
    {
        return; // stop generating
    }

    uint64_t now = micros();

    // ===== MODE CHANGE HANDLING (keep your existing logic)
    static uint32_t lastTargetFps = UINT32_MAX;

    if (lastTargetFps != appState.target_fps)
    {
        lastTargetFps = appState.target_fps;
        CANDriver::flushTxQueue();
    }

    // ===== TIMING CONTROL =====
    if (appState.target_fps == 0)
    {
        // 🔥 f0 = max speed → no timing limit
    }
    else if (appState.mode == MODE_SLOW)
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
    else
    {
        uint32_t interval = 1000000ULL / appState.target_fps;
        if (now - lastFrameUs < interval)
            return;
    }

    // ===== BUILD FRAME (single source of truth)
    twai_message_t msg;
    buildFrame(msg);

    // ===== SEND (queue-aware)
    if (CANDriver::getTxQueueFree() > 0)
    {
        if (CANDriver::sendAsync(msg))
        {
            ledTxEvent();

            counter++;
            frameCount++;
            lastFrameUs = now;

            // ===== ID increment ONLY for simple pattern
            if (canFrameCfg.pattern == 0 && appState.locked_id < 0)
            {
                currentId = (currentId + 1) % 10;
            }
        }
    }
}

void ecuLoop()
{
    if (!appState.canTxEnabled)
        return;

    uint64_t now = esp_timer_get_time();
    twai_message_t msg = {};
    msg.extd = 0;
    msg.rtr = 0;

    if (now - last10ms >= 10000)
    {
        last10ms = now;
        msg.identifier = 0x100;
        msg.data_length_code = 2;

        rpm += 10;
        if (rpm > 4000)
            rpm = 800;

        msg.data[0] = rpm & 0xFF;
        msg.data[1] = (rpm >> 8) & 0xFF;
        CANDriver::sendAsync(msg);
    }

    if (now - last20ms >= 20000)
    {
        last20ms = now;
        msg.identifier = 0x200;
        msg.data_length_code = 2;

        speed += 1;
        if (speed > 120)
            speed = 0;

        msg.data[0] = speed & 0xFF;
        msg.data[1] = (speed >> 8) & 0xFF;
        CANDriver::sendAsync(msg);
    }

    if (now - last1000ms >= 1000000)
    {
        last1000ms = now;
        msg.identifier = 0x300;
        msg.data_length_code = 1;
        msg.data[0] = 0xAA;
        CANDriver::sendAsync(msg);
    }
}
