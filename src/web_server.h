#pragma once

#include <ESPAsyncWebServer.h>
#include "can_bus.h"

// ===== INIT =====
void webInit();

// ===== CAN → WEB =====
void webPushFrame(const CANRxItem &item);