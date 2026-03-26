#pragma once
#include <driver/gpio.h>

#define CAN_TX GPIO_NUM_7
#define CAN_RX GPIO_NUM_6

#define DEFAULT_FPS 100

// ===== DEBUG =====
#define CAN_DEBUG 1

#if CAN_DEBUG
#define CAN_LOG(...) Serial.printf(__VA_ARGS__)
#else
#define CAN_LOG(...)
#endif