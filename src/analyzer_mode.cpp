#include "analyzer_mode.h"
#include "can_bus.h"
#include <Arduino.h>
#include <string.h>
#include "debug.h"
#include "transport.h"
#include "app_mode.h"
#include "web_server.h"
#include "config.h"

namespace
{
    struct AnalyzerConfig
    {
        AnalyzerFormat format;
        bool filterEnabled;
        uint32_t filterId;
    };

    struct __attribute__((packed)) CANPacketHeader
    {
        uint16_t sync;
        uint8_t version;
    };

    struct __attribute__((packed)) CANPacket
    {
        uint32_t ts;
        uint32_t id;
        uint8_t dlc;
        uint8_t data[8];
        uint32_t flags;
    };

    struct __attribute__((packed)) CANFrameWire
    {
        CANPacketHeader header;
        CANPacket pkt;
    };

    constexpr uint16_t CAN_SYNC = 0xAA55;
    constexpr uint8_t CAN_VER = 1;

    AnalyzerConfig analyzerCfg;

    void encodeAscii(const CANRxItem &item)
    {
        const twai_message_t &msg = item.msg;

        uint32_t sec  = item.timestamp / 1000000UL;
        uint32_t usec = item.timestamp % 1000000UL;

        uint8_t dlc = msg.data_length_code;
        if (dlc > 8) dlc = 8;

        char buf[128];
        int idx = 0;

        idx += snprintf(buf + idx, sizeof(buf) - idx,
            msg.extd ? "%lu.%06lu ID:%08lX DLC:%u "
                    : "%lu.%06lu ID:%03lX DLC:%u ",
            sec, usec, msg.identifier, dlc);

        idx += snprintf(buf + idx, sizeof(buf) - idx,
            "%s ", msg.extd ? "EXT" : "STD");

        if (msg.rtr)
        {
            idx += snprintf(buf + idx, sizeof(buf) - idx,
                            "RTR DLC:%u", dlc);
        }
        else
        {
            idx += snprintf(buf + idx, sizeof(buf) - idx, "DATA:");

            for (uint8_t i = 0; i < dlc; i++)
            {
                idx += snprintf(buf + idx, sizeof(buf) - idx,
                                " %02X", msg.data[i]);
            }
        }

        idx += snprintf(buf + idx, sizeof(buf) - idx, "\n");

        DEBUG("%s", buf);
    }

    void encodeBinary(const CANRxItem &item)
    {
        CANFrameWire frame = {};
        frame.header.sync = CAN_SYNC;
        frame.header.version = CAN_VER;
        frame.pkt.ts = item.timestamp;
        frame.pkt.id = item.msg.identifier;

        uint8_t dlc = item.msg.data_length_code;
        if (dlc > 8)
            dlc = 8;

        frame.pkt.dlc = dlc;

        if (!item.msg.rtr)
        {
            memcpy(frame.pkt.data, item.msg.data, dlc);
        }
        else
        {
            memset(frame.pkt.data, 0, sizeof(frame.pkt.data));
        }

        frame.pkt.flags = item.msg.flags;
        // transportWrite((uint8_t *)&frame, sizeof(frame));
        streamPush((uint8_t *)&frame, sizeof(frame));
    }
}

void analyzerInit()
{
    analyzerCfg.format = ANALYZER_FORMAT_ASCII;
    analyzerCfg.filterEnabled = false;
    analyzerCfg.filterId = 0;
}

void analyzerSetFormat(AnalyzerFormat format)
{
    analyzerCfg.format = format;
}

void analyzerSetFilter(bool enable, uint32_t id)
{
    analyzerCfg.filterEnabled = enable;
    analyzerCfg.filterId = id;
}

void analyzerLoop()
{
    if (settings.mode != MODE_ANALYZER) return;

    CANRxItem item;

    int budget = 50;  // 🔥 prevent starvation

    while (budget-- && CANRxBuffer::pop(item))
    {
        if (analyzerCfg.filterEnabled && item.msg.identifier != analyzerCfg.filterId)
            continue;

        if (analyzerCfg.format == ANALYZER_FORMAT_BINARY)
            encodeBinary(item);
        else
            encodeAscii(item);
    }
}
