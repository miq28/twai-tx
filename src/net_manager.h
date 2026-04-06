#pragma once
#include <stddef.h>
#include <stdint.h>

void netInit();
void netLoop();

// TX entry point (used by transport)
size_t netWrite(const uint8_t* data, size_t len);
bool netClientConnected();