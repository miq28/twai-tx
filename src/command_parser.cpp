#include "command.h"
#include <string.h>
#include <stdlib.h>

bool parseCommand(char *buf, Command &cmd)
{
    if (strcmp(buf, "start") == 0)
    {
        cmd.type = CMD_START;
        return true;
    }

    if (strcmp(buf, "stop") == 0)
    {
        cmd.type = CMD_STOP;
        return true;
    }

    if (strncmp(buf, "baud ", 5) == 0)
    {
        cmd.type = CMD_SET_BAUD;
        cmd.value_u32 = atoi(buf + 5);
        return true;
    }

    if (strncmp(buf, "fps ", 4) == 0)
    {
        cmd.type = CMD_SET_FPS;
        cmd.value_int = atoi(buf + 4);
        return true;
    }

    if (strcmp(buf, "mode generator") == 0)
    {
        cmd.type = CMD_SET_MODE;
        strcpy(cmd.str, "generator");
        return true;
    }

    if (strcmp(buf, "mode ecu") == 0)
    {
        cmd.type = CMD_SET_MODE;
        strcpy(cmd.str, "ecu");
        return true;
    }

    if (strcmp(buf, "status") == 0)
    {
        cmd.type = CMD_STATUS;
        return true;
    }

    return false;
}