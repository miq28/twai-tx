#pragma once
#include "can_encoder.h"

class ASCIIEncoderImpl : public ICanEncoder
{
public:
    void encode(const CANRxItem& item) override;
};

extern ASCIIEncoderImpl asciiEncoder;

