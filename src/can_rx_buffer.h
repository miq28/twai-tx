// can_rx_buffer.h

#pragma once
#include <driver/twai.h>

#define RX_BUF_SIZE 1024

struct CANRxItem
{
    twai_message_t msg;
    uint32_t timestamp;
};

bool rxBufferPush(const twai_message_t &msg, uint32_t ts);
bool rxBufferPop(CANRxItem &out);
int  rxBufferCount();