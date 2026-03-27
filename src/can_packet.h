#pragma once
#include <stdint.h>
#include <Arduino.h>

// ===== Header (for sync + versioning) =====
struct __attribute__((packed)) CANPacketHeader
{
    uint16_t sync;   // e.g. 0xAA55
    uint8_t  version;
};

// ===== Payload =====
struct __attribute__((packed)) CANPacket
{
    uint32_t ts;
    uint32_t id;
    uint8_t  dlc;
    uint8_t  data[8];
    uint32_t flags;
};

// ===== Combined wire frame =====
struct __attribute__((packed)) CANFrameWire
{
    CANPacketHeader header;
    CANPacket pkt;
};

// constants
static constexpr uint16_t CAN_SYNC = 0xAA55;
static constexpr uint8_t  CAN_VER  = 1;
static constexpr size_t   CAN_FRAME_SIZE = sizeof(CANFrameWire);