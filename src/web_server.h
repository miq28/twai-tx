#pragma once

#include <ESPAsyncWebServer.h>
#include "can_bus.h"

// ===== INIT =====
void webInit();

// ===== CAN → WEB =====
void webPushFrame(const CANRxItem &item);

// ===== HIGH SPEED STREAM (SHARED) =====
void streamInit();
void streamPush(const uint8_t* data, size_t len);
void streamFlush();

bool wsHasClient();
void wsSendText(const char* data, size_t len);