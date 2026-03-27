#pragma once
#include "can_encoder.h"

class BinaryEncoderImpl : public ICanEncoder
{
public:
    void encode(const CANRxItem& item) override;   // ✅ SAME signature
};

extern BinaryEncoderImpl binaryEncoder; // ✅ exact type