#pragma once
#include "transport_if.h"

void transportManagerInit();
void transportManagerAdd(ITransport* t);
bool transportManagerSend(const uint8_t* data, int len);
void transportManagerProcessRx();