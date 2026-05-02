#pragma once
#include <Arduino.h>
#include <stdint.h>

namespace TxPipe
{
    void init();
    bool push(const uint8_t* data, uint16_t len);
    void task(void*);

    TaskHandle_t getTaskHandle();
}