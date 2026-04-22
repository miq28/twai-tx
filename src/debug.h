#pragma once
#include "rs485.h"
#include <Arduino.h>
#include "web_server.h"

// ===== CONFIG =====
#define DEBUGPORT RS485
// #define RELEASE

#ifndef RELEASE

// ===== RUNTIME CONTROL =====
inline bool debug_to_serial;   // kontrol Serial output

// ===== DELTA TIMESTAMP =====
static inline uint32_t dbg_delta_us()
{
    static uint32_t last = 0;
    uint32_t now = micros();
    uint32_t delta = now - last;
    last = now;
    return delta;
}

// ===== WEB SERIAL RATE LIMIT =====
static inline bool webSerialReady()
{
    static uint32_t lastWs = 0;

    // ✔ use your WebSocket as indicator
    if (!wsHasClient())
        return false;

    uint32_t now = millis();

    // ✔ rate limit (100 Hz)
    if (now - lastWs < 10)
        return false;

    lastWs = now;
    return true;
}


// ===== MAIN LOG (RS485 + optional Serial) =====
#define DEBUG(fmt, ...) \
    do { \
        uint32_t _t = dbg_delta_us(); \
        char _buf[256]; \
        int _len = snprintf(_buf, sizeof(_buf), "+%lu|" fmt, _t, ##__VA_ARGS__); \
        if (_len > 0) \
        { \
            DEBUGPORT.write((uint8_t*)_buf, _len); \
            if (debug_to_serial) \
                Serial.write((uint8_t*)_buf, _len); \
            if (webSerialReady()) \
                wsSendText(_buf, _len); \
        } \
    } while (0)

// ===== HELPERS =====
#define DEBUG_PRINT(s) \
    do { \
        uint32_t _t = dbg_delta_us(); \
        char _buf[256]; \
        int _len = snprintf(_buf, sizeof(_buf), "+%lu|%s", _t, (s)); \
        if (_len > 0) \
        { \
            DEBUGPORT.write((uint8_t*)_buf, _len); \
            if (debug_to_serial) \
                Serial.write((uint8_t*)_buf, _len); \
            if (webSerialReady()) \
                wsSendText(_buf, _len); \
        } \
    } while (0)

#define DEBUG_PRINTLN(s) \
    do { \
        uint32_t _t = dbg_delta_us(); \
        char _buf[256]; \
        int _len = snprintf(_buf, sizeof(_buf), "+%lu|%s\n", _t, (s)); \
        if (_len > 0) \
        { \
            DEBUGPORT.write((uint8_t*)_buf, _len); \
            if (debug_to_serial) \
                Serial.write((uint8_t*)_buf, _len); \
            if (webSerialReady()) \
                wsSendText(_buf, _len); \
        } \
    } while (0)

#else

#define DEBUG(...)         do {} while (0)
#define DEBUG_PRINT(...)   do {} while (0)
#define DEBUG_PRINTLN(...) do {} while (0)

#endif