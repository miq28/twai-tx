#include <Arduino.h>
#include <driver/twai.h>
#include "can_driver.h"
#include "can_pipeline.h"

static volatile uint32_t rxOverflow = 0;

// ================= RX =================
#if defined(WEACT_STUDIO_CAN485_V1)
#define RX_SIZE 1024
#else
#define RX_SIZE 4096
#endif
static CANRxItem rxBuf[RX_SIZE];
static volatile uint16_t rxHead = 0, rxTail = 0;

// ================= TX =================
#if defined(WEACT_STUDIO_CAN485_V1)
#define TX_SIZE 128
#else
#define TX_SIZE 256
#endif

static twai_message_t txBuf[TX_SIZE];
static volatile uint16_t txHead = 0, txTail = 0;

// ================= RX TASK =================
static void canRxTask(void*)
{
    twai_message_t msg;

    while (1)
    {
        if (twai_receive(&msg, portMAX_DELAY) == ESP_OK)
        {
            uint16_t next = (rxHead + 1) % RX_SIZE;
            if (next != rxTail)
            {
                rxBuf[rxHead].msg = msg;
                rxBuf[rxHead].timestamp = micros();
                rxHead = next;
            }
        }
        else
        {
            rxOverflow++; // debug
        }
    }
}

// ================= TX TASK =================
static void canTxTask(void*)
{
    while (1)
    {
        if (txTail != txHead)
        {
            twai_message_t msg = txBuf[txTail];
            txTail = (txTail + 1) % TX_SIZE;

            twai_transmit(&msg, portMAX_DELAY);
        }
        else
        {
            vTaskDelay(1);
        }
    }
}

// ================= API =================
bool canRxPop(CANRxItem &out)
{
    if (rxTail == rxHead) return false;

    out = rxBuf[rxTail];
    rxTail = (rxTail + 1) % RX_SIZE;
    return true;
}

bool canTxPush(const twai_message_t &msg)
{
    uint16_t next = (txHead + 1) % TX_SIZE;
    if (next == txTail) return false;

    txBuf[txHead] = msg;
    txHead = next;
    return true;
}

void canPipelineInit()
{
    xTaskCreatePinnedToCore(canRxTask, "can_rx", 4096, NULL, 20, NULL, 1);
    xTaskCreatePinnedToCore(canTxTask, "can_tx", 4096, NULL, 18, NULL, 1);
}

uint32_t canGetRxOverflow()
{
    return rxOverflow;
}