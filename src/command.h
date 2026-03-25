#pragma once
#include <stdint.h>

enum CommandType
{
    CMD_NONE,
    CMD_START,
    CMD_STOP,
    CMD_SET_MODE,
    CMD_SET_BAUD,
    CMD_SET_FPS,
    CMD_SET_LISTEN,
    CMD_STATUS
};

struct Command
{
    CommandType type;

    int value_int;
    uint32_t value_u32;

    char str[16];
};