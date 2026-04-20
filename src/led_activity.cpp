#include "led_activity.h"
#include <Arduino.h>

#if defined(WEACT_STUDIO_CAN485_V1)

#include <Adafruit_NeoPixel.h>

#define LED_PIN GPIO_NUM_4
#define LED_COUNT 1

static Adafruit_NeoPixel pixel(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);

// ===== CONFIG =====
static const uint32_t PULSE_MS    = 20;
static const uint32_t PEAK_MS     = 40;

static const float DECAY          = 0.90f;
static const float SCALE          = 0.02f;

static const uint16_t PEAK_THRESHOLD = 50;

// WiFi
static const uint32_t WIFI_PERIOD_MS = 1000;
static const uint32_t WIFI_FLASH_MS  = 25;

// LED refresh
static const uint32_t LED_TASK_MS = 10;

// ===== STATE =====
static volatile uint16_t rxEvents = 0;
static volatile uint16_t txEvents = 0;
static volatile bool wifiConnected = true;

static volatile CANHealthState canHealth = CAN_HEALTH_OK;

static float rxLevel = 0;
static float txLevel = 0;

static uint32_t rxUntil = 0;
static uint32_t txUntil = 0;
static uint32_t peakUntil = 0;

static uint32_t wifiNextFlash = 0;
static uint32_t wifiFlashUntil = 0;

// ===== API =====
void ledRxEvent() { rxEvents++; }
void ledTxEvent() { txEvents++; }

void ledSetCANHealth(CANHealthState state)
{
    canHealth = state;
}

void ledWifiConnected(bool connected)
{
    wifiConnected = connected;
}

// ===== TASK =====
void ledTask(void *)
{
    pixel.begin();
    pixel.clear();
    pixel.show();

    bool errorPulseState = false;
    uint32_t errorPulseTimer = 0;

    while (1)
    {
        uint32_t now = millis();

        // =========================================================
        // 🟡 WIFI FLASH (TOP PRIORITY)
        // =========================================================
        if (!wifiConnected)
        {
            if (now >= wifiNextFlash)
            {
                wifiNextFlash = now + WIFI_PERIOD_MS;
                wifiFlashUntil = now + WIFI_FLASH_MS;
            }

            if (now < wifiFlashUntil)
            {
                pixel.setPixelColor(0, pixel.Color(150, 150, 0));
                pixel.show();
                vTaskDelay(pdMS_TO_TICKS(LED_TASK_MS));
                continue;
            }
        }

        // =========================================================
        // CAN HEALTH PRIORITY
        // =========================================================
        if (canHealth == CAN_HEALTH_BUS_OFF)
        {
            // 🔴 solid red
            pixel.setPixelColor(0, pixel.Color(150, 0, 0));
            pixel.show();
            vTaskDelay(pdMS_TO_TICKS(LED_TASK_MS));
            continue;
        }

        if (canHealth == CAN_HEALTH_ERROR)
        {
            // 🔴 pulsing red
            if (now - errorPulseTimer > 200)
            {
                errorPulseTimer = now;
                errorPulseState = !errorPulseState;
            }

            uint8_t r = errorPulseState ? 120 : 10;

            pixel.setPixelColor(0, pixel.Color(r, 0, 0));
            pixel.show();
            vTaskDelay(pdMS_TO_TICKS(LED_TASK_MS));
            continue;
        }

        if (canHealth == CAN_HEALTH_DEGRADED)
        {
            // 🟠 dim orange
            pixel.setPixelColor(0, pixel.Color(80, 30, 0));
            pixel.show();
            vTaskDelay(pdMS_TO_TICKS(LED_TASK_MS));
            continue;
        }

        // =========================================================
        // NORMAL CAN VISUALIZATION
        // =========================================================
        uint16_t rx = rxEvents;
        uint16_t tx = txEvents;
        rxEvents = 0;
        txEvents = 0;

        rxLevel = rxLevel * DECAY + rx;
        txLevel = txLevel * DECAY + tx;

        if (rx > 0) rxUntil = now + PULSE_MS;
        if (tx > 0) txUntil = now + PULSE_MS;

        if ((rx + tx) > PEAK_THRESHOLD)
        {
            peakUntil = now + PEAK_MS;
        }

        bool rxActive = now < rxUntil;
        bool txActive = now < txUntil;
        bool peak     = now < peakUntil;

        float level = rxLevel + txLevel;
        float brightness = level * SCALE;
        if (brightness > 1.0f) brightness = 1.0f;

        uint8_t base = (uint8_t)(brightness * 80);

        uint8_t r = 0, g = 0, b = 0;

        if (peak)
        {
            r = g = b = 100;
        }
        else if (rxActive || txActive)
        {
            if (rxActive && txActive)
            {
                g = base;
                b = base;
            }
            else if (rxActive)
            {
                b = base;
            }
            else
            {
                g = base;
            }
        }
        else
        {
            g = base / 4;
            b = base / 4;
        }

        pixel.setPixelColor(0, pixel.Color(r, g, b));
        pixel.show();

        vTaskDelay(pdMS_TO_TICKS(LED_TASK_MS));
    }
}

// ===== INIT =====
void ledActivityInit()
{
    xTaskCreatePinnedToCore(
        ledTask,
        "ledTask",
        2048,
        NULL,
        1,
        NULL,
        0
    );
}

#else

void ledActivityInit() {}
void ledRxEvent() {}
void ledTxEvent() {}
void ledSetCANHealth(CANHealthState) {}
void ledWifiConnected(bool) {}

#endif