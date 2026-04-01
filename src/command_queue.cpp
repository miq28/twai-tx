#include "command_queue.h"

#define CMD_Q_SIZE 32

static Command buf[CMD_Q_SIZE];
static volatile uint8_t head = 0, tail = 0;

bool cmdPush(const Command& cmd)
{
    uint8_t next = (head + 1) % CMD_Q_SIZE;
    if (next == tail) return false;

    buf[head] = cmd;
    head = next;
    return true;
}

bool cmdPop(Command& cmd)
{
    if (tail == head) return false;

    cmd = buf[tail];
    tail = (tail + 1) % CMD_Q_SIZE;
    return true;
}