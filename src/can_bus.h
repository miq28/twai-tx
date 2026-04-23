#pragma once
#include "esp_twai.h"

struct CANRxItem
{
    twai_message_t msg;
    uint32_t timestamp;
};

namespace CANDriver
{
    // extern volatile twai_error_state_t g_errorState;
    // extern volatile bool g_errorStateChanged;
    // void init(uint32_t baud = 500000, bool listenOnly = false);
    // bool send(const twai_message_t &msg);
    // bool sendAsync(const twai_message_t &msg);
    // bool reinit(uint32_t baud, bool listenOnly);
    // bool isRunning();
    // uint32_t getCurrentBaud();
    // bool isListenOnly();
    // CANHealthState getCANHealth();
    // uint32_t getTxAttempt();
    // uint32_t getTxSuccess();
    // uint32_t getTxDrop();
    // uint32_t getTxQueueFree();
    // uint32_t getTxQueueUsed();
    // void flushTxQueue();

    // ===== EVENT-DRIVEN STATE =====
    extern volatile twai_error_state_t g_errorState;
    extern volatile bool g_errorStateChanged;

    extern volatile bool g_driverAlive;
    extern volatile bool g_driverStateChanged;

    void init(uint32_t baud = 500000, bool listenOnly = false);
    bool send(const twai_message_t &msg);
    bool sendAsync(const twai_message_t &msg);
    bool reinit(uint32_t baud, bool listenOnly);
    uint32_t getCurrentBaud();
    bool isListenOnly();

    uint32_t getTxAttempt();
    uint32_t getTxSuccess();
    uint32_t getTxDrop();
    uint32_t getTxQueueFree();
    uint32_t getTxQueueUsed();
    void flushTxQueue();

    struct Event;
    bool popEvent(Event &e);
    bool getStatus(twai_node_status_t &out);
    void requestRecover();
    void loop();
}

namespace CANRxBuffer
{
    bool pop(CANRxItem &out);
    int count();
    // void startTask();
    // void stopTask();

    // bool push(const twai_message_t &msg, uint32_t ts);
    bool pushFromISR(const twai_message_t &msg);

    void clear();
    // void task(void *);

    uint32_t getDropCount();
    uint32_t getTotalFrames();
    uint16_t getMaxUsage();
    void resetStats();
}