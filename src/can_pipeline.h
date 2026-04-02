#pragma once
#include <driver/twai.h>

struct CANRxItem
{
    twai_message_t msg;
    uint32_t timestamp;
};

bool canRxPop(CANRxItem &out);
bool canTxPush(const twai_message_t &msg);
void canPipelineInit();
uint32_t canGetRxOverflow();