#include "ascii_encoder.h"
#include <Arduino.h>

// global instance
ASCIIEncoderImpl asciiEncoder;

void ASCIIEncoderImpl::encode(const CANRxItem &item)
{
    const twai_message_t &m = item.msg;

    // Timestamp (micros) → seconds.us
    uint32_t ts = item.timestamp;
    uint32_t sec = ts / 1000000UL;
    uint32_t usec = ts % 1000000UL;

    // ID (hex, 3 or 8 digits depending on ext)
    if (m.extd)
        Serial.printf("%lu.%06lu ID:%08lX ", sec, usec, m.identifier);
    else
        Serial.printf("%lu.%06lu ID:%03lX ", sec, usec, m.identifier);

    // DLC
    uint8_t dlc = m.data_length_code;
    if (dlc > 8)
        dlc = 8;
    Serial.printf("DLC:%u ", dlc);

    // Flags
    if (m.extd)
        Serial.print("EXT ");
    if (m.rtr)
        Serial.print("RTR ");

    // Data
    Serial.print("DATA:");
    for (uint8_t i = 0; i < dlc; i++)
    {
        Serial.printf(" %02X", m.data[i]);
    }

    Serial.print("\n");
}