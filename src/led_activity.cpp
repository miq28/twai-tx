#include "led_activity.h"
#include <Arduino.h>

#if defined(WEACT_STUDIO_CAN485_V1)

#include <Adafruit_NeoPixel.h>

#define LED_PIN GPIO_NUM_4
#define LED_COUNT 1

static Adafruit_NeoPixel pixel(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);

// ===== CONFIG =====
static const uint32_t PULSE_MS = 20;
static const uint32_t ERROR_MS = 120; // 🔴 error flash
static const uint32_t PEAK_MS = 40;

static const float DECAY = 0.90f;
static const float SCALE = 0.02f;

static const uint16_t PEAK_THRESHOLD = 50;

// WiFi (TOP PRIORITY)
static const uint32_t WIFI_PERIOD_MS = 1000;
static const uint32_t WIFI_FLASH_MS = 25;

// ===== STATE =====
static volatile uint16_t rxEvents = 0;
static volatile uint16_t txEvents = 0;
static volatile bool errorFlag = false;

static float rxLevel = 0;
static float txLevel = 0;

static uint32_t rxUntil = 0;
static uint32_t txUntil = 0;
static uint32_t errorUntil = 0;
static uint32_t peakUntil = 0;

// WiFi
static bool wifiConnected = true;
static uint32_t wifiNextFlash = 0;
static uint32_t wifiFlashUntil = 0;

// ===== INIT =====
void ledActivityInit()
{
    pixel.begin();
    pixel.clear();
    pixel.show();
}

// ===== EVENTS =====
void ledRxEvent() { rxEvents++; }
void ledTxEvent() { txEvents++; }

void ledCanErrorEvent()
{
    errorFlag = true;
}

void ledWifiConnected(bool connected)
{
    wifiConnected = connected;
}

// ===== UPDATE =====
void ledActivityUpdate()
{
    uint32_t now = millis();

    // ===== WIFI TOP PRIORITY (NON-DESTRUCTIVE) =====
    bool wifiFlash = false;

    if (!wifiConnected)
    {
        if (now >= wifiNextFlash)
        {
            wifiNextFlash = now + WIFI_PERIOD_MS;
            wifiFlashUntil = now + WIFI_FLASH_MS;
        }

        wifiFlash = (now < wifiFlashUntil);
    }

    // =========================================================
    // CAN EVENTS
    // =========================================================
    uint16_t rx = rxEvents;
    uint16_t tx = txEvents;
    rxEvents = 0;
    txEvents = 0;

    // density
    rxLevel = rxLevel * DECAY + rx;
    txLevel = txLevel * DECAY + tx;

    // pulses
    if (rx > 0)
        rxUntil = now + PULSE_MS;
    if (tx > 0)
        txUntil = now + PULSE_MS;

    // error
    if (errorFlag)
    {
        errorFlag = false;
        errorUntil = now + ERROR_MS;
    }

    // peak
    if ((rx + tx) > PEAK_THRESHOLD)
    {
        peakUntil = now + PEAK_MS;
    }

    bool rxActive = now < rxUntil;
    bool txActive = now < txUntil;
    bool error = now < errorUntil;
    bool peak = now < peakUntil;

    // brightness
    float level = rxLevel + txLevel;
    float brightness = level * SCALE;
    if (brightness > 1.0f)
        brightness = 1.0f;

    uint8_t base = (uint8_t)(brightness * 80);

    uint8_t r = 0, g = 0, b = 0;

    // =========================================================
    // PRIORITY
    // =========================================================
    if (error)
    {
        r = 150; // 🔴 CAN error
    }
    else if (peak)
    {
        r = g = b = 100; // ⚪ burst
    }
    else if (rxActive || txActive)
    {
        if (rxActive && txActive)
        {
            g = base;
            b = base; // 🔷
        }
        else if (rxActive)
        {
            b = base; // 🔵
        }
        else
        {
            g = base; // 🟢
        }
    }
    else
    {
        // idle glow
        g = base / 4;
        b = base / 4;
    }

    // pixel.setPixelColor(0, pixel.Color(r, g, b));
    // pixel.show();

    // ===== FINAL COMPOSITION =====

    // if CAN error active → NEVER hide it
    if (error)
    {
        r = 150;
        g = 0;
        b = 0;
    }
    else if (wifiFlash)
    {
        // WiFi visible ONLY if no error
        r = 120;
        g = 120;
        b = 0;
    }

    pixel.setPixelColor(0, pixel.Color(r, g, b));
    pixel.show();
}

#else

void ledActivityInit() {}
void ledActivityUpdate() {}
void ledRxEvent() {}
void ledTxEvent() {}
void ledCanErrorEvent() {}
void ledWifiConnected(bool) {}

#endif