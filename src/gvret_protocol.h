#pragma once
#include <stdint.h>
#include <driver/twai.h>

class GVRETProtocol
{
public:
    void handleByte(uint8_t b);

    bool isBusEnabled() const { return busEnabled; }
    bool isHandshakeDone() const { return handshakeDone; }

    GVRETProtocol();
    void reset();



private:
    // enum State {
    //     IDLE,
    //     GET_COMMAND,
    //     BUILD_FRAME
    // };

    // State state = IDLE;

    uint8_t step = 0;
    uint32_t id = 0;
    uint8_t dlc = 0;
    uint8_t data[8];
    bool extended = false;

    bool busEnabled = false;
    bool handshakeDone = false;

    // void reset();
    void handleCommand(uint8_t cmd);
    void buildFrame(uint8_t b);
};