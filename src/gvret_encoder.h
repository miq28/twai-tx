#pragma once
#include "can_encoder.h"

class GVRETEncoder : public ICanEncoder
{
public:
    void encode(const CANRxItem &item) override;
};

extern GVRETEncoder gvretEncoder;