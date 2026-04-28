#pragma once
#include "rs485.h"
#include <Arduino.h>

// ===== CONFIG =====
#define DEBUGPORT RS485
// #define RELEASE

#ifndef RELEASE

// ===== EXTERNAL HOOKS =====
void wsSendText(const char* data, size_t len);
bool wsHasClient();

// ===== RUNTIME CONTROL =====
inline bool debug_to_serial;   // control Serial output

// ===== DELTA TIMESTAMP =====
static inline uint32_t dbg_delta_us()
{
    static uint32_t last = 0;
    uint32_t now = micros();
    uint32_t delta = now - last;
    last = now;
    return delta;
}

// ===== MAIN LOG (RS485 ALWAYS, Serial + WS OPTIONAL) =====
#define DEBUG(fmt, ...) \
    do { \
        uint32_t _t = dbg_delta_us(); \
        \
        /* If NO WS and NO Serial → skip formatting */ \
        if (!wsHasClient() && !debug_to_serial) { \
            /* fallback: minimal RS485 print WITHOUT snprintf */ \
            DEBUGPORT.printf("+%lu|", _t); \
            DEBUGPORT.printf(fmt, ##__VA_ARGS__); \
        } else { \
            char _buf[256]; \
            int _l = snprintf(_buf, sizeof(_buf), "+%lu|" fmt, _t, ##__VA_ARGS__); \
            if (_l > 0) { \
                /* ALWAYS send to RS485 */ \
                DEBUGPORT.write((const uint8_t*)_buf, _l); \
                \
                /* OPTIONAL Serial */ \
                if (debug_to_serial) \
                    Serial.write((const uint8_t*)_buf, _l); \
                \
                /* OPTIONAL WebSocket */ \
                if (wsHasClient()) \
                    wsSendText(_buf, _l); \
            } \
        } \
    } while (0)


// ===== HELPERS =====
#define DEBUG_PRINT(s) \
    do { \
        uint32_t _t = dbg_delta_us(); \
        if (!wsHasClient() && !debug_to_serial) { \
            DEBUGPORT.printf("+%lu|%s", _t, (s)); \
        } else { \
            char _buf[256]; \
            int _l = snprintf(_buf, sizeof(_buf), "+%lu|%s", _t, (s)); \
            if (_l > 0) { \
                DEBUGPORT.write((const uint8_t*)_buf, _l); \
                if (debug_to_serial) \
                    Serial.write((const uint8_t*)_buf, _l); \
                if (wsHasClient()) \
                    wsSendText(_buf, _l); \
            } \
        } \
    } while (0)


#define DEBUG_PRINTLN(s) \
    do { \
        uint32_t _t = dbg_delta_us(); \
        if (!wsHasClient() && !debug_to_serial) { \
            DEBUGPORT.printf("+%lu|%s\n", _t, (s)); \
        } else { \
            char _buf[256]; \
            int _l = snprintf(_buf, sizeof(_buf), "+%lu|%s\n", _t, (s)); \
            if (_l > 0) { \
                DEBUGPORT.write((const uint8_t*)_buf, _l); \
                if (debug_to_serial) \
                    Serial.write((const uint8_t*)_buf, _l); \
                if (wsHasClient()) \
                    wsSendText(_buf, _l); \
            } \
        } \
    } while (0)

#else

#define DEBUG(...)         do {} while (0)
#define DEBUG_PRINT(...)   do {} while (0)
#define DEBUG_PRINTLN(...) do {} while (0)

#endif