#pragma once
#include <stdint.h>

struct __attribute__((packed)) CANRxFrame
{
    uint32_t id;
    uint8_t dlc;
    uint8_t data[8];
    uint8_t flags;
    uint32_t timestamp;
};

// init (optional for future)
void canRxInit();

// must be called frequently (drain TWAI)
void canRxProcess();

// pop for transport
bool canRxPop(CANRxFrame &f);

// stats
uint32_t canRxGetDropCount();