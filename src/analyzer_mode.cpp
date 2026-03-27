#include "analyzer_mode.h"
#include "can_rx_buffer.h"
#include "can_packet.h"
#include <Arduino.h>
#include <string.h>

void analyzerLoop()
{
    CANRxItem item;

    while (rxBufferPop(item))
    {
        CANFrameWire frame;

        // header
        frame.header.sync = CAN_SYNC;
        frame.header.version = CAN_VER;

        // payload
        frame.pkt.ts  = item.timestamp;
        frame.pkt.id  = item.msg.identifier;
        frame.pkt.dlc = item.msg.data_length_code;
        memcpy(frame.pkt.data, item.msg.data, 8);
        frame.pkt.flags = item.msg.flags;

        Serial.write((uint8_t*)&frame, CAN_FRAME_SIZE);
    }
}