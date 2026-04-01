#pragma once
#include <stdint.h>

bool txPush(const uint8_t* data, uint16_t len);
int  txPop(uint8_t* out, int max);