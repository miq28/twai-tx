#pragma once
#include <stdint.h>

enum CommandType
{
    CMD_NONE,
    CMD_START,
    CMD_STOP,
    CMD_SET_MODE,
    CMD_SET_BAUD,
    CMD_SET_EXTENDED,
    CMD_SET_FPS,
    CMD_SET_DELAY,
    CMD_LOCK_ID,
    CMD_SET_LISTEN,
    CMD_SET_FORMAT,
    CMD_SET_FILTER,
    CMD_SET_WIFIMODE,
    CMD_STATUS,
    CMD_RESET,
    CMD_HELP
};

struct Command
{
    CommandType type;

    int value_int;
    uint32_t value_u32;
    uint8_t value_u8;

    char str[16];
    bool value_bool;
};

struct InputContext
{
};

void dispatchByte(InputContext &ctx, uint8_t b);
void cliProcessByte(uint8_t b);
