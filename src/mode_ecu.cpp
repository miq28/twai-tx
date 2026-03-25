#include "mode_ecu.h"
#include "can_driver.h"
#include "app_mode.h"
#include <esp_timer.h>

// Simple simulated ECU signals
// - ID 0x100: engine RPM (10 ms)
// - ID 0x200: vehicle speed (20 ms)
// - ID 0x300: heartbeat (1000 ms)

static uint64_t last_10ms = 0;
static uint64_t last_20ms = 0;
static uint64_t last_1000ms = 0;

static uint16_t rpm = 800;
static uint16_t speed = 0;

void ecuLoop()
{
    if (!appState.running)
        return;

    uint64_t now = esp_timer_get_time();

    twai_message_t msg;
    msg.extd = 0;
    msg.rtr = 0;

    // ================= RPM (10 ms) =================
    if (now - last_10ms >= 10000)
    {
        last_10ms = now;

        msg.identifier = 0x100;
        msg.data_length_code = 2;

        rpm += 10;
        if (rpm > 4000) rpm = 800;

        msg.data[0] = rpm & 0xFF;
        msg.data[1] = (rpm >> 8) & 0xFF;

        CANDriver::send(msg);
    }

    // ================= SPEED (20 ms) =================
    if (now - last_20ms >= 20000)
    {
        last_20ms = now;

        msg.identifier = 0x200;
        msg.data_length_code = 2;

        speed += 1;
        if (speed > 120) speed = 0;

        msg.data[0] = speed & 0xFF;
        msg.data[1] = (speed >> 8) & 0xFF;

        CANDriver::send(msg);
    }

    // ================= HEARTBEAT (1 sec) =================
    if (now - last_1000ms >= 1000000)
    {
        last_1000ms = now;

        msg.identifier = 0x300;
        msg.data_length_code = 1;
        msg.data[0] = 0xAA;

        CANDriver::send(msg);
    }
}