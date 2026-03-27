#include "generator_mode.h"
#include "app_mode.h"
#include "can_driver.h"
#include <esp_timer.h>

static uint8_t current_id = 0;
static uint32_t counter = 0;

static uint64_t last_frame_us = 0;
static uint64_t last_slow_us = 0;

static uint32_t frame_count = 0;
static uint64_t last_fps_us = 0;

void generatorLoop()
{
    if (!appState.running)
        return;

    uint64_t now = esp_timer_get_time();

    // slow mode
    if (appState.mode == MODE_SLOW)
    {
        if (now - last_slow_us < 3000000ULL)
            return;
        last_slow_us = now;
    }
    else
    {
        if (appState.delay_us > 0)
        {
            if (now - last_frame_us < (uint64_t)appState.delay_us)
                return;
        }
        else if (appState.target_fps > 0)
        {
            uint32_t interval = 1000000ULL / appState.target_fps;
            if (now - last_frame_us < interval)
                return;
        }
    }

    twai_message_t msg;
    msg.extd = canFrameCfg.extended ? 1 : 0;
    msg.rtr = 0;
    msg.data_length_code = 8;

    uint32_t id;

    if (appState.locked_id >= 0)
    {
        id = appState.locked_id;
    }
    else
    {
        id = current_id;
    }

    // Extend ID range when in extended mode
    if (canFrameCfg.extended)
    {
        id |= 0x100; // simple expansion (keeps pattern but avoids tiny IDs)
    }

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
        frame_count++;
        last_frame_us = now;

        if (appState.locked_id < 0)
            current_id = (current_id + 1) % 10;
    }

    // FPS debug
    if (now - last_fps_us >= 1000000ULL)
    {
        last_fps_us = now;
        // Serial.printf("FPS: %lu\n", frame_count);
        frame_count = 0;
    }
}