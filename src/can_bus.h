#pragma once
#include <Arduino.h>
#include <driver/twai.h>

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
    bool getTiming(uint32_t baud, twai_timing_config_t &timing);
    bool reinit(uint32_t baud, bool listenOnly);
    bool isRunning();
    uint32_t getCurrentBaud();
    bool isListenOnly();
    CANHealthState getCANHealth();

    twai_state_t getStateRaw();
    const char*  getStateStr(twai_state_t s);
    const char*  getStateStr(); // convenience (calls getStateRaw)

    CANHealthState getHealthFromState(twai_state_t s, uint32_t tec, uint32_t rec);
}

namespace CANRxBuffer
{
    bool pop(CANRxItem &out);
    int count();
    void startTask();
    void stopTask();
    bool push(const twai_message_t &msg, uint32_t ts);
    void clear();
    void task(void *);

    uint32_t getDropCount();
    uint32_t getTotalFrames();
    uint16_t getMaxUsage();
    void resetStats();
}



// ===== CAN LOG CONTROL =====
enum CANAlertCategory : uint32_t
{
    CAN_ALERT_CRITICAL     = 1 << 0,
    CAN_ALERT_OPERATIONAL  = 1 << 1,
    CAN_ALERT_INFO         = 1 << 2,
    CAN_ALERT_RARE         = 1 << 3,
};

uint32_t parseCategories(const String &csv);
String   categoriesToString(uint32_t mask);
void setCanLogPreset(const char *name);

namespace CANEvents
{
    void startTask();
    void stopTask();
    void process();
}

namespace CANTxBuffer
{
    void startTask();
    void stopTask();
    bool push(const twai_message_t &msg);

    uint32_t getTxOk();
    uint32_t getTxFail();
    uint32_t getTxDrop();
    void resetStats();
}