#pragma once
#include <stdint.h>

class ITransport
{
public:
    virtual bool send(const uint8_t* data, int len) = 0;
    virtual int available() = 0;
    virtual uint8_t read() = 0;
};