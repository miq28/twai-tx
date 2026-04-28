#pragma once
#include <driver/gpio.h>
#include "debug.h"
#include "app_mode.h"

#if defined(SPARKLE_IOT_XH_S3E_N16R8)
#define CAN_TX GPIO_NUM_7
#define CAN_RX GPIO_NUM_6

#elif defined(WEACT_STUDIO_CAN485_V1)
// CAN / TWAI
#define CAN_TX GPIO_NUM_27
#define CAN_RX GPIO_NUM_26
// TF / SD card SPI
#define SD_CS GPIO_NUM_13
#define SD_SCK GPIO_NUM_14
#define SD_MOSI GPIO_NUM_15
#define SD_MISO GPIO_NUM_2
// RS485
#define RS485_DE GPIO_NUM_17
#define RS485_RO GPIO_NUM_21 // RX into ESP32
#define RS485_DI GPIO_NUM_22 // TX from ESP32
// VIN monitor
#define VIN_ADC GPIO_NUM_36
// WS2812
#define RGB_LED GPIO_NUM_4
// Button
#define KEY GPIO_NUM_0

#elif defined(WAVESHARE_ESP32_S3_RS485_CAN)
// CAN / TWAI
#define CAN_TX GPIO_NUM_15
#define CAN_RX GPIO_NUM_16
// RS485
#define RS485_DE GPIO_NUM_21 // EN
#define RS485_RO GPIO_NUM_18 // RX into ESP32
#define RS485_DI GPIO_NUM_17 // TX from ESP32
// RTC
#define RTC_SCL GPIO_NUM_38
#define RTC_SDA GPIO_NUM_39
#define RTC_INT GPIO_NUM_40
// Not assigned (external SH1.0 connector)
#define NOT_ASSIGNED_1 GPIO_NUM_1
#define NOT_ASSIGNED_2 GPIO_NUM_2
// Used for USB CDC - do not use!!
#define USB_D_P GPIO_NUM_19 // USB D+
#define USB_D_N GPIO_NUM_20 // USB D-
// Button
#define KEY GPIO_NUM_0
#endif

#define DEFAULT_FPS 10

// ===== DEBUG =====
#define CAN_DEBUG 1

#if CAN_DEBUG
#define CAN_LOG(...) DEBUG(__VA_ARGS__)
#else
#define CAN_LOG(...)
#endif

#define SETTINGS_MAGIC   0xC0FFEE42
#define SETTINGS_VERSION 1

struct Settings
{
#if defined(WAVESHARE_ESP32_S3_RS485_CAN)
#define PREF_NAME "wave_can485" // max 15 characaters
#elif defined(WEACT_STUDIO_CAN485_V1)
#define PREF_NAME "weact_can485" // max 15 characaters
#endif
    uint32_t magic;     // sanity check
    uint16_t version;   // struct version

    uint32_t CANBaud;
    uint32_t fdSpeed;
    bool enabled;
    bool listenOnly;
    bool fdMode;

    bool useBinarySerialComm; // use a binary protocol on the serial link or human readable format?

    uint8_t logLevel;   // Level of logging to output on serial line
    uint8_t systemType; // 0 = A0RET, 1 = EVTV ESP32 Board, 2 = Macchine 5-CAN board

    bool enableBT; // are we enabling bluetooth too?
    char btName[32];
    int sendingBus;

    bool enableLawicel;

    // if we're using WiFi then output to serial is disabled (it's far too slow to keep up)
    uint8_t wifiMode; // 0 = don't use wifi, 1 = connect to an AP, 2 = Create an AP
    char AP_SSID[32];    // null terminated string for the SSID
    char AP_PASS[64]; // Null terminated string for the key. Can be a passphase or the actual key
    char STA_SSID[32];    // null terminated string for the SSID
    char STA_PASS[64]; // Null terminated string for the key. Can be a passphase or the actual key

    Mode mode; ;       // <-- ADD (store Mode enum as u8)

    uint32_t crc;       // checksum (last field)
} __attribute__((__packed__));

extern Settings settings;

void checkESPBoard();

void loadSettings();

void applyCANConfig(uint32_t baud, bool listenOnly);
void changeWifiMode(uint8_t mode);
void changePrefsString(const char * key, const char * str);

void saveSettings();   // <-- ADD