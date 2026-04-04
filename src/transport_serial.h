#pragma once
#include "transport_if.h"

class SerialTransport : public ITransport
{
public:
    bool send(const uint8_t* data, int len) override;
    int available() override;
    uint8_t read() override;
};

extern SerialTransport serialTransport;
void transportInit();   // keep this exposed