#include "can_packet.h"
#include "can_encoder.h"
#include <Arduino.h>
#include <string.h>
#include "transport_tx_buffer.h"

class BinaryEncoder : public ICanEncoder
{
public:
    void encode(const CANRxItem &item) override
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

        // Serial.write((uint8_t*)&frame, sizeof(frame));
        txPush((uint8_t*)&frame, sizeof(frame));
    }
};

BinaryEncoder binaryEncoder;