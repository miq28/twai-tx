#pragma once
#include <driver/twai.h>

struct CANRxItem
{
    twai_message_t msg;
    uint32_t timestamp;
};

namespace CANDriver
{
    void init(uint32_t baud = 500000, bool listenOnly = false);
    bool send(const twai_message_t &msg);
    bool getTiming(uint32_t baud, twai_timing_config_t &timing);
    bool reinit(uint32_t baud, bool listenOnly);
    bool isRunning();
    uint32_t getCurrentBaud();
    bool isListenOnly();

}

namespace CANRxBuffer
{
    bool pop(CANRxItem &out);
    int count();
    void startTask();
    bool push(const twai_message_t &msg, uint32_t ts);
    void clear();
    void task(void *);

    uint32_t getDropCount();
    uint32_t getTotalFrames();
    uint16_t getMaxUsage();
    void resetStats();
}
