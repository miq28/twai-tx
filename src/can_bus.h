#pragma once
#include "esp_twai.h"

struct CANRxItem
{
    twai_message_t msg;
    uint32_t timestamp;
};

enum CANHealthState
{
    CAN_HEALTH_OK,
    CAN_HEALTH_DEGRADED,
    CAN_HEALTH_ERROR,
    CAN_HEALTH_BUS_OFF
};

namespace CANDriver
{
    void init(uint32_t baud = 500000, bool listenOnly = false);
    bool send(const twai_message_t &msg);
    bool sendAsync(const twai_message_t &msg);
    bool reinit(uint32_t baud, bool listenOnly);
    bool isRunning();
    uint32_t getCurrentBaud();
    bool isListenOnly();
    CANHealthState getCANHealth();
    uint32_t getTxAttempt();
    uint32_t getTxSuccess();
    uint32_t getTxDrop();
    uint32_t getTxQueueFree();
    uint32_t getTxQueueUsed();
    void flushTxQueue();
}

namespace CANRxBuffer
{
    bool pop(CANRxItem &out);
    int count();
    void startTask();
    void stopTask();

    bool push(const twai_message_t &msg, uint32_t ts);
    bool pushFromISR(const twai_message_t &msg);

    void clear();
    void task(void *);

    uint32_t getDropCount();
    uint32_t getTotalFrames();
    uint16_t getMaxUsage();
    void resetStats();
}