#pragma once
#include <driver/twai.h>

namespace CANDriver
{
    void init(uint32_t baud = 500000, bool listenOnly = false);
    bool send(const twai_message_t &msg);

    // New
    bool getTiming(uint32_t baud, twai_timing_config_t &t);
    // 🔥 NEW
    bool reinit(uint32_t baud, bool listenOnly);

    extern uint32_t tx_drop;
}