#include "command.h"
#include <string.h>
#include <stdlib.h>

bool parseCommand(char *buf, Command &cmd)
{
    // ===== START / STOP =====
    if (strcmp(buf, "start") == 0 || strcmp(buf, "s") == 0)
    {
        cmd.type = CMD_START;
        return true;
    }

    if (strcmp(buf, "stop") == 0 || strcmp(buf, "x") == 0)
    {
        cmd.type = CMD_STOP;
        return true;
    }

    // ===== BAUD =====
    if (strncmp(buf, "baud ", 5) == 0)
    {
        cmd.type = CMD_SET_BAUD;
        cmd.value_u32 = atoi(buf + 5);
        return true;
    }

    if (buf[0] == 'b')
    {
        cmd.type = CMD_SET_BAUD;
        cmd.value_u32 = atoi(buf + 1);
        return true;
    }

    // ===== FPS =====
    if (strncmp(buf, "fps ", 4) == 0)
    {
        cmd.type = CMD_SET_FPS;
        cmd.value_int = atoi(buf + 4);
        return true;
    }

    if (buf[0] == 'f')
    {
        cmd.type = CMD_SET_FPS;
        cmd.value_int = atoi(buf + 1);
        return true;
    }

    // ===== MODE =====
    if (strcmp(buf, "mode generator") == 0 || strcmp(buf, "m0") == 0)
    {
        cmd.type = CMD_SET_MODE;
        strcpy(cmd.str, "generator");
        return true;
    }

    if (strcmp(buf, "mode ecu") == 0 || strcmp(buf, "m2") == 0)
    {
        cmd.type = CMD_SET_MODE;
        strcpy(cmd.str, "ecu");
        return true;
    }

    // ===== STATUS =====
    if (strcmp(buf, "status") == 0)
    {
        cmd.type = CMD_STATUS;
        return true;
    }

    // ===== HELP =====
    if (strcmp(buf, "help") == 0)
    {
        cmd.type = CMD_HELP;
        return true;
    }

    // ===== detect command in JSON =====
    if (buf[0] == '{')
    {
        // parse JSON command
        // return parseJsonCommand(buf, cmd);
    }

    return false;
}