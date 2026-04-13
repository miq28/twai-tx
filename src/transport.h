#pragma once
#include <stddef.h>
#include <stdint.h>

// ===== INIT / LOOP =====
void transportInit();
void transportProcess();

// ===== TX =====
void transportWrite(const uint8_t* data, size_t len);
void transportWritePriority(const uint8_t *data, size_t len);
void transportFlush();

// ===== RX DISPATCH (NEW) =====
void transportDispatchByte(uint8_t b);
void transportDispatchBuffer(const uint8_t* data, size_t len);