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
    CMD_STATUS,
    CMD_RESET,
    CMD_HELP
};

struct Command
{
    CommandType type;

    int value_int;
    uint32_t value_u32;

    char str[16];
    bool value_bool;
};