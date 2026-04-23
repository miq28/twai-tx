#pragma once

void ledActivityInit();

// events
void ledRxEvent();
void ledTxEvent();

void ledWifiConnected(bool connected);