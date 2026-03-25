#include <Arduino.h>
#include <driver/twai.h>
#include <esp_timer.h>

// ================= GPIO =================
#define TX_GPIO GPIO_NUM_7
#define RX_GPIO GPIO_NUM_6

// ================= MODES =================
enum Mode
{
    MODE_MAX,   // max speed (bus saturation)
    MODE_SLOW   // 1 frame every 3 seconds
};

// ================= STATE =================
static volatile bool running = true;   // default: streaming ON
static Mode mode = MODE_MAX;

static int target_fps = 1000;             // 0 = unlimited
static int delay_us = 0;               // manual delay (overrides FPS if >0)
static int locked_id = -1;             // -1 = normal cycle, 0–9 = fixed ID

static uint8_t current_id = 0;
static uint32_t counter = 0;

static uint64_t last_frame_us = 0;
static uint64_t last_slow_us = 0;

// FPS monitor
static uint32_t fps = 0;
static uint32_t frame_count = 0;
static uint64_t last_fps_us = 0;

// ================= SERIAL COMMAND PARSER =================
void handleSerial()
{
    static char buf[32];
    static uint8_t idx = 0;

    while (Serial.available())
    {
        char c = Serial.read();

        if (c == '\n' || c == '\r')
        {
            buf[idx] = 0;

            // ---- BASIC CONTROL ----
            if (buf[0] == 's') running = true;       // start
            else if (buf[0] == 'x') running = false; // stop

            // ---- MODE ----
            else if (buf[0] == 'm')
            {
                if (buf[1] == '0') mode = MODE_MAX;
                else if (buf[1] == '1') mode = MODE_SLOW;
            }

            // ---- FPS LIMIT ----
            else if (buf[0] == 'f')
            {
                target_fps = atoi(&buf[1]);
            }

            // ---- FIXED DELAY ----
            else if (buf[0] == 'd')
            {
                delay_us = atoi(&buf[1]);
            }

            // ---- LOCK ID ----
            else if (buf[0] == 'i')
            {
                locked_id = atoi(&buf[1]);
                if (locked_id < 0 || locked_id > 9)
                    locked_id = -1; // fallback to cycling
            }

            idx = 0;
        }
        else if (idx < sizeof(buf) - 1)
        {
            buf[idx++] = c;
        }
    }
}

// ================= SETUP =================
void setup()
{
    Serial.begin(1000000);

    twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(
        TX_GPIO, RX_GPIO, TWAI_MODE_NORMAL);

    // increase queue for better throughput
    g_config.tx_queue_len = 32;

    twai_timing_config_t t_config = TWAI_TIMING_CONFIG_500KBITS();
    twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    twai_driver_install(&g_config, &t_config, &f_config);
    twai_start();

    last_fps_us = esp_timer_get_time();
}

// ================= LOOP =================
void loop()
{
    handleSerial();

    if (!running)
    {
        taskYIELD();
        return;
    }

    twai_message_t msg;
    msg.extd = 0; // standard frame
    msg.rtr = 0;
    msg.data_length_code = 8;

    uint64_t now = esp_timer_get_time();

    // ================= SLOW MODE =================
    if (mode == MODE_SLOW)
    {
        if (now - last_slow_us < 3000000ULL) // 3 seconds
            return;

        last_slow_us = now;
    }
    else
    {
        // ================= RATE CONTROL =================

        // delay mode (highest priority)
        if (delay_us > 0)
        {
            if (now - last_frame_us < (uint64_t)delay_us)
                return;
        }
        // fps limiter
        else if (target_fps > 0)
        {
            uint32_t interval = 1000000ULL / target_fps;
            if (now - last_frame_us < interval)
                return;
        }
    }

    // ================= ID SELECTION =================
    uint8_t id_to_use = (locked_id >= 0) ? locked_id : current_id;
    msg.identifier = id_to_use;

    // ================= PAYLOAD =================
    msg.data[0] = (counter >> 0) & 0xFF;
    msg.data[1] = (counter >> 8) & 0xFF;
    msg.data[2] = (counter >> 16) & 0xFF;
    msg.data[3] = (counter >> 24) & 0xFF;
    msg.data[4] = 0x44;
    msg.data[5] = 0x55;
    msg.data[6] = 0x66;
    msg.data[7] = 0x77;

    // ================= TRANSMIT =================
    if (twai_transmit(&msg, 0) == ESP_OK)
    {
        counter++;
        frame_count++;
        last_frame_us = now;

        if (locked_id < 0)
            current_id = (current_id + 1) % 10;
    }
    else
    {
        taskYIELD(); // queue full
    }

    // ================= FPS MONITOR =================
    if (now - last_fps_us >= 1000000ULL)
    {
        fps = frame_count;
        frame_count = 0;
        last_fps_us = now;

        Serial.printf("FPS: %lu\n", fps);
    }
}