#pragma once
#include "can_rx_buffer.h"

class ICanEncoder
{
public:
    virtual ~ICanEncoder() {}
    virtual void encode(const CANRxItem &item) = 0;
};