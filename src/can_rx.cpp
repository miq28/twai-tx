#include "can_rx.h"
#include <driver/twai.h>
#include <esp_timer.h>

#define RX_BUF_SIZE 1024  // power of 2

static CANRxFrame rxBuf[RX_BUF_SIZE];

static volatile uint32_t head = 0;
static volatile uint32_t tail = 0;

static uint32_t rx_drop = 0;

// ================= PUSH =================
static inline bool rxPush(const CANRxFrame &f)
{
    uint32_t next = (head + 1) & (RX_BUF_SIZE - 1);

    if (next == tail)
    {
        // buffer full → drop
        return false;
    }

    rxBuf[head] = f;
    head = next;
    return true;
}

// ================= POP =================
bool canRxPop(CANRxFrame &f)
{
    if (tail == head)
        return false;

    f = rxBuf[tail];
    tail = (tail + 1) & (RX_BUF_SIZE - 1);
    return true;
}

// ================= PROCESS =================
void canRxProcess()
{
    twai_message_t msg;

    while (twai_receive(&msg, 0) == ESP_OK)
    {
        CANRxFrame f;

        f.id = msg.identifier;
        f.dlc = msg.data_length_code;
        f.flags = (msg.extd ? 1 : 0) | (msg.rtr ? 2 : 0);
        f.timestamp = (uint32_t)esp_timer_get_time();

        for (int i = 0; i < f.dlc; i++)
            f.data[i] = msg.data[i];

        if (!rxPush(f))
            rx_drop++;
    }
}

// ================= INIT =================
void canRxInit()
{
    head = tail = 0;
    rx_drop = 0;
}

// ================= STATS =================
uint32_t canRxGetDropCount()
{
    return rx_drop;
}