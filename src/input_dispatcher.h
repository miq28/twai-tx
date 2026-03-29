#pragma once
#include <stdint.h>

struct InputContext
{
    bool binaryMode = false;
};

// main entry
void dispatchByte(InputContext& ctx, uint8_t b);

// CLI byte handler (exposed for reuse/testing)
void cliProcessByte(uint8_t b);