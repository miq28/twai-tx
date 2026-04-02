#include "command.h"
#include "can_driver.h"
#include "app_mode.h"
#include "rs485.h"
#include <string.h>
#include <stdlib.h>

#define Q_SIZE 16
static Command q[Q_SIZE];
static uint8_t h=0,t=0;

// ================= QUEUE =================
void cmdPush(const Command& c)
{
    uint8_t n=(h+1)%Q_SIZE;
    if(n==t) return;
    q[h]=c;
    h=n;
}

// ================= PARSER =================
bool commandParse(const char* buf, Command& cmd)
{
    cmd = {};

    // -------- start / stop --------
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

    // -------- mode --------
    if (strncmp(buf, "mode ", 5) == 0)
    {
        cmd.type = CMD_SET_MODE;
        strncpy(cmd.str, buf + 5, sizeof(cmd.str));
        return true;
    }

    // -------- baud --------
    if (strncmp(buf, "baud ", 5) == 0)
    {
        cmd.type = CMD_SET_BAUD;
        cmd.value_u32 = atoi(buf + 5);
        return true;
    }

    // -------- fps --------
    if (strncmp(buf, "fps ", 4) == 0)
    {
        cmd.type = CMD_SET_FPS;
        cmd.value_int = atoi(buf + 4);
        return true;
    }

    // -------- delay --------
    if (strncmp(buf, "delay ", 6) == 0)
    {
        cmd.type = CMD_SET_DELAY;
        cmd.value_int = atoi(buf + 6);
        return true;
    }

    // -------- lock id --------
    if (strncmp(buf, "id ", 3) == 0)
    {
        cmd.type = CMD_LOCK_ID;
        cmd.value_int = atoi(buf + 3);
        return true;
    }

    // -------- extended --------
    if (strcmp(buf, "ext 1") == 0)
    {
        cmd.type = CMD_SET_EXTENDED;
        cmd.value_bool = true;
        return true;
    }
    if (strcmp(buf, "ext 0") == 0)
    {
        cmd.type = CMD_SET_EXTENDED;
        cmd.value_bool = false;
        return true;
    }

    // -------- listen --------
    if (strcmp(buf, "listen 1") == 0)
    {
        cmd.type = CMD_SET_LISTEN;
        cmd.value_bool = true;
        return true;
    }
    if (strcmp(buf, "listen 0") == 0)
    {
        cmd.type = CMD_SET_LISTEN;
        cmd.value_bool = false;
        return true;
    }

    // -------- status --------
    if (strcmp(buf, "status") == 0)
    {
        cmd.type = CMD_STATUS;
        return true;
    }

    // -------- help --------
    if (strcmp(buf, "help") == 0)
    {
        cmd.type = CMD_HELP;
        return true;
    }

    return false;
}

// ================= EXECUTOR =================
void commandProcess()
{
    if (t == h) return;

    Command c = q[t];
    t = (t + 1) % Q_SIZE;

    switch (c.type)
    {
    case CMD_START:
        appState.running = true;
        RS485.println("[CMD] start");
        break;

    case CMD_STOP:
        appState.running = false;
        RS485.println("[CMD] stop");
        break;

    case CMD_SET_MODE:
        if (strcmp(c.str, "generator") == 0) appState.mode = MODE_GENERATOR;
        else if (strcmp(c.str, "ecu") == 0) appState.mode = MODE_ECU;
        else if (strcmp(c.str, "analyzer") == 0) appState.mode = MODE_ANALYZER;
        else if (strcmp(c.str, "savvycan") == 0) appState.mode = MODE_SAVVYCAN;

        RS485.printf("[CMD] mode=%d\n", appState.mode);
        break;

    case CMD_SET_BAUD:
        CANDriver::reinit(c.value_u32, false);
        RS485.printf("[CMD] baud=%lu\n", c.value_u32);
        break;

    case CMD_SET_FPS:
        appState.target_fps = c.value_int;
        RS485.printf("[CMD] fps=%d\n", c.value_int);
        break;

    case CMD_SET_DELAY:
        appState.delay_us = c.value_int;
        RS485.printf("[CMD] delay=%d\n", c.value_int);
        break;

    case CMD_LOCK_ID:
        appState.locked_id = c.value_int;
        RS485.printf("[CMD] id=%d\n", c.value_int);
        break;

    case CMD_SET_EXTENDED:
        canFrameCfg.extended = c.value_bool;
        RS485.printf("[CMD] ext=%d\n", c.value_bool);
        break;

    case CMD_SET_LISTEN:
        CANDriver::reinit(CANDriver::getCurrentBaud(), c.value_bool);
        RS485.printf("[CMD] listen=%d\n", c.value_bool);
        break;

    case CMD_STATUS:
        RS485.printf("mode=%d fps=%d delay=%d id=%d\n",
            appState.mode,
            appState.target_fps,
            appState.delay_us,
            appState.locked_id);
        break;

    case CMD_HELP:
        RS485.println("Commands:");
        RS485.println(" start / stop");
        RS485.println(" mode generator|ecu|analyzer|savvycan");
        RS485.println(" baud 500000");
        RS485.println(" fps 1000");
        RS485.println(" delay 100");
        RS485.println(" id 123");
        RS485.println(" ext 0|1");
        RS485.println(" listen 0|1");
        RS485.println(" status");
        break;

    default:
        break;
    }
}