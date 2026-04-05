#pragma once
#include "rs485.h"
#include <Arduino.h>

// ===== CONFIG =====
#define DEBUGPORT RS485
// #define RELEASE

#ifndef RELEASE

// --- delta timestamp ---
static inline uint32_t dbg_delta_us()
{
    static uint32_t last = 0;
    uint32_t now = micros();
    uint32_t delta = now - last;
    last = now;
    return delta;
}

// --- main log (tight format) ---
#define DEBUG(fmt, ...) \
    do { DEBUGPORT.printf("+%lu|" fmt, dbg_delta_us(), ##__VA_ARGS__); } while (0)

// --- helpers ---
#define DEBUG_PRINT(s) \
    do { DEBUGPORT.printf("+%lu|%s", dbg_delta_us(), (s)); } while (0)

#define DEBUG_PRINTLN(s) \
    do { DEBUGPORT.printf("+%lu|%s\n", dbg_delta_us(), (s)); } while (0)

#else

#define DEBUG(...)         do {} while (0)
#define DEBUG_PRINT(...)   do {} while (0)
#define DEBUG_PRINTLN(...) do {} while (0)

#endif