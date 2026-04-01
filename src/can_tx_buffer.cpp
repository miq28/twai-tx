#include "can_tx_buffer.h"

#define CAN_TX_BUF_SIZE 256

static twai_message_t buf[CAN_TX_BUF_SIZE];
static volatile uint16_t head = 0;
static volatile uint16_t tail = 0;

bool canTxPush(const twai_message_t& msg)
{
    uint16_t next = (head + 1) % CAN_TX_BUF_SIZE;
    if (next == tail) return false;

    buf[head] = msg;
    head = next;
    return true;
}

bool canTxPop(twai_message_t& msg)
{
    if (tail == head) return false;

    msg = buf[tail];
    tail = (tail + 1) % CAN_TX_BUF_SIZE;
    return true;
}