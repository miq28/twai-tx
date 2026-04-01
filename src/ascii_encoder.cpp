#include "ascii_encoder.h"
#include <Arduino.h>
#include "transport_tx_buffer.h"

// global instance
ASCIIEncoderImpl asciiEncoder;

void ASCIIEncoderImpl::encode(const CANRxItem &item)
{
    const twai_message_t &m = item.msg;
    char line[128];

    // Timestamp (micros) → seconds.us
    uint32_t ts = item.timestamp;
    uint32_t sec = ts / 1000000UL;
    uint32_t usec = ts % 1000000UL;

    // ID (hex, 3 or 8 digits depending on ext)
    if (m.extd)
    {
        // Serial.printf("%lu.%06lu ID:%08lX ", sec, usec, m.identifier);
        int len = snprintf(line, sizeof(line), "%lu.%06lu ID:%08lX ", sec, usec, m.identifier);
        txPush((uint8_t *)line, len);
    }
    else
    {
        // Serial.printf("%lu.%06lu ID:%08lX ", sec, usec, m.identifier);
        int len = snprintf(line, sizeof(line), "%lu.%06lu ID:%03lX ", sec, usec, m.identifier);
        txPush((uint8_t *)line, len);
    }

    // DLC
    uint8_t dlc = m.data_length_code;
    if (dlc > 8)
        dlc = 8;
    // Serial.printf("DLC:%u ", dlc);
    int len = snprintf(line, sizeof(line), "DLC:%u ", dlc);
    txPush((uint8_t *)line, len);

    // Flags
    if (m.extd)
    {
        // Serial.print("EXT ");
        txPush((uint8_t *)"EXT ", 4);
    }
    if (m.rtr)
    {
        // Serial.print("RTR ");
        txPush((uint8_t *)"RTR ", 4);
    }

    // Data
    // Serial.print("DATA:");
    txPush((uint8_t *)"DATA", 4);
    for (uint8_t i = 0; i < dlc; i++)
    {
        // Serial.printf(" %02X", m.data[i]);
        int len = snprintf(line, sizeof(line), " %02X", m.data[i]);
        txPush((uint8_t *)line, len);
    }

    // Serial.print("\n");
    txPush((uint8_t *)"\n", 1);
}