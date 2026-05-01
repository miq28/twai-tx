#pragma once

#include "can_bus.h"   // where CANHealthState is defined

void ledActivityInit();

// events
void ledRxEvent();
void ledTxEvent();

// replace old error API
void ledSetCANHealth(CANHealthState state);

void ledWifiConnected(bool connected);

void ledSetEnabled(bool en);