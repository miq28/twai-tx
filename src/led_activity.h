#pragma once

void ledActivityInit();
void ledActivityUpdate();

// CAN events
void ledRxEvent();
void ledTxEvent();
void ledCanErrorEvent();   // 🔴 renamed

// WiFi status
void ledWifiConnected(bool connected);