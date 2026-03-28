#include "gvret_encoder.h"
#include <Arduino.h>

GVRETEncoder gvretEncoder;

void GVRETEncoder::encode(const CANRxItem &item)
{
    const twai_message_t &m = item.msg;

    uint8_t buf[20];
    uint8_t idx = 0;

    // ===== GVRET FRAME (minimal working) =====
    // [0] 0xF1  (frame marker)
    // [1] length
    // [2] cmd (0x00 = CAN frame)
    // payload...

    buf[idx++] = 0xF1;

    uint8_t len_index = idx++;   // placeholder

    buf[idx++] = 0x00; // CAN frame cmd

    // timestamp (4 bytes)
    uint32_t ts = item.timestamp;
    buf[idx++] = (ts >> 0) & 0xFF;
    buf[idx++] = (ts >> 8) & 0xFF;
    buf[idx++] = (ts >> 16) & 0xFF;
    buf[idx++] = (ts >> 24) & 0xFF;

    // ID (4 bytes)
    uint32_t id = m.identifier;
    buf[idx++] = (id >> 0) & 0xFF;
    buf[idx++] = (id >> 8) & 0xFF;
    buf[idx++] = (id >> 16) & 0xFF;
    buf[idx++] = (id >> 24) & 0xFF;

    // DLC
    uint8_t dlc = m.data_length_code;
    if (dlc > 8) dlc = 8;
    buf[idx++] = dlc;

    // FLAGS
    uint8_t flags = 0;
    if (m.extd) flags |= 0x01;
    if (m.rtr)  flags |= 0x02;
    buf[idx++] = flags;

    // DATA
    for (uint8_t i = 0; i < dlc; i++)
        buf[idx++] = m.data[i];

    // set length
    buf[len_index] = idx;

    Serial.write(buf, idx);
}