#include "can_rx_buffer.h"
#include <Arduino.h>
#include "transport_tx_buffer.h"

void gvretStream()
{
    CANRxItem item;

    while (rxBufferPop(item))
    {
        const twai_message_t &m = item.msg;

        uint8_t buf[32];
        int idx = 0;

        // ===== HEADER =====
        buf[idx++] = 0xF1;
        buf[idx++] = 0x00;

        // ===== TIMESTAMP =====
        uint32_t ts = item.timestamp;
        buf[idx++] = ts & 0xFF;
        buf[idx++] = ts >> 8;
        buf[idx++] = ts >> 16;
        buf[idx++] = ts >> 24;

        // ===== ID =====
        uint32_t id = m.identifier;
        if (m.extd) id |= (1UL << 31);

        buf[idx++] = id & 0xFF;
        buf[idx++] = id >> 8;
        buf[idx++] = id >> 16;
        buf[idx++] = id >> 24;

        // ===== DLC + BUS =====
        uint8_t dlc = m.data_length_code & 0xF;
        buf[idx++] = (0 << 4) | dlc; // bus 0

        // ===== DATA =====
        for (int i = 0; i < dlc; i++)
            buf[idx++] = m.data[i];

        // ===== TERMINATOR =====
        buf[idx++] = 0;

        txPush(buf, idx);
    }
}