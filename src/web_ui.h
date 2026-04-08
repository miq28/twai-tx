#pragma once
#include "can_bus.h"

void webPushFrame(const CANRxItem &item);
static bool webPopFrame(CANRxItem &out);
void webInit();
void webLoop();