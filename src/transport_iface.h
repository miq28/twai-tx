#pragma once
#include <stdint.h>

class ITransport
{
public:
    virtual void send(const uint8_t* data, int len) = 0;
};