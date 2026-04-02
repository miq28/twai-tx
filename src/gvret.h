#pragma once
#include "can_pipeline.h"

void gvretPush(const CANRxItem& item);
void gvretProcess();
void gvretProcessByte(uint8_t b);