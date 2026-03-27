// can_rx_buffer.cpp

#include "can_rx_buffer.h"

static CANRxItem buffer[RX_BUF_SIZE];
static volatile uint16_t head = 0;
static volatile uint16_t tail = 0;

bool rxBufferPush(const twai_message_t &msg, uint32_t ts)
{
    uint16_t next = (head + 1) % RX_BUF_SIZE;
    if (next == tail)
        return false; // overflow

    buffer[head].msg = msg;
    buffer[head].timestamp = ts;
    head = next;
    return true;
}

bool rxBufferPop(CANRxItem &out)
{
    if (tail == head)
        return false;

    out = buffer[tail];
    tail = (tail + 1) % RX_BUF_SIZE;
    return true;
}

int rxBufferCount()
{
    return (head - tail + RX_BUF_SIZE) % RX_BUF_SIZE;
}