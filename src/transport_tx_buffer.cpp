#include "transport_tx_buffer.h"

#define TX_BUF_SIZE 4096

static uint8_t buf[TX_BUF_SIZE];
static volatile uint16_t head = 0;
static volatile uint16_t tail = 0;

bool txPush(const uint8_t* data, uint16_t len)
{
    for (int i = 0; i < len; i++)
    {
        uint16_t next = (head + 1) % TX_BUF_SIZE;
        if (next == tail) return false;
        buf[head] = data[i];
        head = next;
    }
    return true;
}

int txPop(uint8_t* out, int max)
{
    int count = 0;
    while (tail != head && count < max)
    {
        out[count++] = buf[tail];
        tail = (tail + 1) % TX_BUF_SIZE;
    }
    return count;
}