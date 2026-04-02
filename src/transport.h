#pragma once
#include <stdint.h>

void transportInit();
void transportProcess();
bool transportSend(const uint8_t* data, int len);