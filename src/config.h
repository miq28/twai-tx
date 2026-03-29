#pragma once
#include <driver/gpio.h>

#if defined(SPARKLE_IOT_XH_S3E_N16R8)
#define CAN_TX GPIO_NUM_7
#define CAN_RX GPIO_NUM_6
#elif defined(WEACT_STUDIO_CAN485_V1)
#define CAN_TX GPIO_NUM_27
#define CAN_RX GPIO_NUM_26
#elif defined(WAVESHARE_ESP32_S3_RS485_CAN)
#define CAN_TX GPIO_NUM_15
#define CAN_RX GPIO_NUM_16
#endif

#define DEFAULT_FPS 100

// ===== DEBUG =====
#define CAN_DEBUG 1

#if CAN_DEBUG
#define CAN_LOG(...) Serial.printf(__VA_ARGS__)
#else
#define CAN_LOG(...)
#endif