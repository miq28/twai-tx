#include "gvret_encoder.h"
#include <Arduino.h>

GVRETEncoder gvretEncoder;

void GVRETEncoder::encode(const CANRxItem &item)
{
    const twai_message_t &m = item.msg;

    uint8_t buf[20];
    int idx = 0;

    buf[idx++] = 0xF1;
    buf[idx++] = 0; // PROTO_BUILD_CAN_FRAME

    uint32_t id = m.identifier;
    if (m.extd)
        id |= (1UL << 31);

    buf[idx++] = id & 0xFF;
    buf[idx++] = (id >> 8) & 0xFF;
    buf[idx++] = (id >> 16) & 0xFF;
    buf[idx++] = (id >> 24) & 0xFF;

    buf[idx++] = 0; // bus
    buf[idx++] = m.data_length_code & 0xF;

    for (int i = 0; i < m.data_length_code; i++)
        buf[idx++] = m.data[i];

    Serial.write(buf, idx);
}