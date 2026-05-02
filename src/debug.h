#pragma once
#include "rs485.h"
#include <Arduino.h>
#include <stdarg.h>
#include <stdio.h>

// ===== CONFIG =====
#define DEBUGPORT RS485
// #define RELEASE

#ifndef RELEASE

// ===== RUNTIME CONTROL =====
inline bool debug_to_serial;   // kontrol Serial output

void webDebugWrite(const char *text);

// ===== DELTA TIMESTAMP =====
static inline uint32_t dbg_delta_us()
{
    static uint32_t last = 0;
    uint32_t now = micros();
    uint32_t delta = now - last;
    last = now;
    return delta;
}

static inline void debug_write_all(const char *text)
{
    DEBUGPORT.print(text);
    if (debug_to_serial)
        Serial.print(text);
    webDebugWrite(text);
}

static inline void debug_printf_impl(const char *fmt, ...)
{
    char body[256];
    char line[320];

    va_list args;
    va_start(args, fmt);
    vsnprintf(body, sizeof(body), fmt, args);
    va_end(args);

    snprintf(line, sizeof(line), "+%lu|%s", dbg_delta_us(), body);
    debug_write_all(line);
}

static inline void debug_print_impl(const char *text, bool newline)
{
    char line[320];
    snprintf(line, sizeof(line), "+%lu|%s%s", dbg_delta_us(), text, newline ? "\n" : "");
    debug_write_all(line);
}

// ===== MAIN LOG (RS485 + optional Serial + web terminal) =====
#define DEBUG(fmt, ...) debug_printf_impl(fmt, ##__VA_ARGS__)

// ===== HELPERS =====
#define DEBUG_PRINT(s) debug_print_impl((s), false)
#define DEBUG_PRINTLN(s) debug_print_impl((s), true)

#else

#define DEBUG(...)         do {} while (0)
#define DEBUG_PRINT(...)   do {} while (0)
#define DEBUG_PRINTLN(...) do {} while (0)

#endif
