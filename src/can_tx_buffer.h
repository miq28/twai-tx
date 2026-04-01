#pragma once
#include <driver/twai.h>

bool canTxPush(const twai_message_t& msg);
bool canTxPop(twai_message_t& msg);