#include "can_rx_buffer.h"
#include <Arduino.h>
#include "transport_tx_buffer.h"

#define GVRET_Q_SIZE 512

static CANRxItem q[GVRET_Q_SIZE];
static volatile uint16_t head = 0;
static volatile uint16_t tail = 0;

void gvretPush(const CANRxItem& item)
{
    uint16_t next = (head + 1) % GVRET_Q_SIZE;
    if (next == tail) return; // drop-safe

    q[head] = item;
    head = next;
}

void gvretStream()
{
    CANRxItem item;
    int budget = 64;

    while (budget-- && tail != head)
    {
        item = q[tail];
        tail = (tail + 1) % GVRET_Q_SIZE;

        const twai_message_t &m = item.msg;

        uint8_t buf[32];
        int idx = 0;

        // HEADER
        buf[idx++] = 0xF1;
        buf[idx++] = 0x00;

        // TIMESTAMP
        uint32_t ts = item.timestamp;
        buf[idx++] = ts & 0xFF;
        buf[idx++] = ts >> 8;
        buf[idx++] = ts >> 16;
        buf[idx++] = ts >> 24;

        // ID
        uint32_t id = m.identifier;
        if (m.extd) id |= (1UL << 31);

        buf[idx++] = id & 0xFF;
        buf[idx++] = id >> 8;
        buf[idx++] = id >> 16;
        buf[idx++] = id >> 24;

        // DLC
        uint8_t dlc = m.data_length_code & 0xF;
        buf[idx++] = (0 << 4) | dlc;

        // DATA
        for (int i = 0; i < dlc; i++)
            buf[idx++] = m.data[i];

        // TERMINATOR
        buf[idx++] = 0;

        txPush(buf, idx);
    }
}