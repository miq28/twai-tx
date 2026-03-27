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

    // ===== LISTEN-ONLY TOGGLE =====
    if (buf[0] == 'l')
    {
        cmd.type = CMD_SET_LISTEN;
        cmd.value_bool = atoi(buf + 1);
        return true;
    }

    // ===== DELAY =====
    if (buf[0] == 'd')
    {
        cmd.type = CMD_SET_DELAY;
        cmd.value_u32 = atoi(buf + 1);
        return true;
    }

    // ===== LOCK ID =====
    if (buf[0] == 'i')
    {
        cmd.type = CMD_LOCK_ID;
        int v = atoi(&buf[1]);
        cmd.value_u32 = (v >= 0 && v <= 9) ? v : -1;
        return true;
    }

    // ===== RESET =====
    if (buf[0] == 'r')
    {
        cmd.type = CMD_RESET;
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

    // ===== CAN FORMAT EXTENDED/STANDARD =====
    if (strcmp(buf, "ext 1") == 0)
    {
        cmd.type = CMD_SET_EXTENDED;
        cmd.value_int = 1;
        return true;
    }

    if (strcmp(buf, "ext 0") == 0)
    {
        cmd.type = CMD_SET_EXTENDED;
        cmd.value_int = 0;
        return true;
    }

    // ===== detect command in JSON =====
    if (buf[0] == '{')
    {
        // parse JSON command
        // return parseJsonCommand(buf, cmd);
        return true;
    }

    // ===== HELP =====
    if (strcmp(buf, "help") == 0)
    {
        cmd.type = CMD_HELP;
        return true;
    }

    // ===== MODE ANALYZER =====
    if (strcmp(buf, "mode analyzer") == 0 || strcmp(buf, "m3") == 0)
    {
        cmd.type = CMD_SET_MODE;
        strcpy(cmd.str, "analyzer");
        return true;
    }

    return false;
}
