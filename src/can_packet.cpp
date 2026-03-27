#include "can_packet.h"
#include <string.h>
#include <driver/twai.h>

// Pure mapping: internal -> wire format
inline void packCAN(const twai_message_t &msg, uint32_t ts, CANPacket &out)
{
    out.ts  = ts;
    out.id  = msg.identifier;
    out.dlc = msg.data_length_code;
    memcpy(out.data, msg.data, 8);
    out.flags = msg.flags;
}