#include "can_driver.h"
#include "config.h"
#include <Arduino.h>

namespace CANDriver
{

static uint32_t currentBaud = 500000;
static bool currentListenOnly = false;
static bool driverRunning = false;


bool getTiming(uint32_t baud, twai_timing_config_t &t)
{
    switch (baud)
    {
    case 1000000: t = TWAI_TIMING_CONFIG_1MBITS(); return true;
    case 800000:  t = TWAI_TIMING_CONFIG_800KBITS(); return true;
    case 500000:  t = TWAI_TIMING_CONFIG_500KBITS(); return true;
    case 250000:  t = TWAI_TIMING_CONFIG_250KBITS(); return true;
    case 125000:  t = TWAI_TIMING_CONFIG_125KBITS(); return true;
    case 100000:  t = TWAI_TIMING_CONFIG_100KBITS(); return true;
    case 50000:   t = TWAI_TIMING_CONFIG_50KBITS(); return true;
    case 25000:   t = TWAI_TIMING_CONFIG_25KBITS(); return true;

    case 83333:
        t.brp = 48; t.tseg_1 = 15; t.tseg_2 = 4; t.sjw = 3; t.triple_sampling = false;
        return true;

    case 33333:
        t.brp = 120; t.tseg_1 = 15; t.tseg_2 = 4; t.sjw = 3; t.triple_sampling = false;
        return true;

    default:
        return false;
    }
}

static bool startDriver(uint32_t baud, bool listenOnly)
{
    twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(
        CAN_TX, CAN_RX,
        listenOnly ? TWAI_MODE_LISTEN_ONLY : TWAI_MODE_NORMAL);

    g_config.tx_queue_len = 32;
    g_config.rx_queue_len = 64;

    twai_timing_config_t t_config;

    if (!getTiming(baud, t_config))
    {
        CAN_LOG("[CAN] Unsupported baud %lu → fallback 500k\n", baud);
        t_config = TWAI_TIMING_CONFIG_500KBITS();
        baud = 500000;
    }
    else
    {
        CAN_LOG("[CAN] Init OK → baud: %lu\n", baud);
    }

    twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    if (twai_driver_install(&g_config, &t_config, &f_config) != ESP_OK)
    {
        CAN_LOG("[CAN] Install failed\n");
        driverRunning = false;
        return false;
    }

    if (twai_start() != ESP_OK)
    {
        CAN_LOG("[CAN] Start failed\n");
        return false;
    }

    currentBaud = baud;
    currentListenOnly = listenOnly;
    driverRunning = true;

    CAN_LOG("[CAN] Started (%s)\n", listenOnly ? "LISTEN ONLY" : "NORMAL");

    return true;
}

void init(uint32_t baud, bool listenOnly)
{
    startDriver(baud, listenOnly);
}

bool reinit(uint32_t baud, bool listenOnly)
{
    CAN_LOG("[CAN] Reinit requested → baud:%lu listen:%d\n", baud, listenOnly);

    twai_stop();
    driverRunning = false;
    twai_driver_uninstall();

    return startDriver(baud, listenOnly);
}

bool send(const twai_message_t &msg)
{
    return twai_transmit((twai_message_t *)&msg, 0) == ESP_OK;
}

bool isRunning()
{
    return driverRunning;
}

uint32_t getCurrentBaud()
{
    return currentBaud;
}

bool isListenOnly()
{
    return currentListenOnly;
}

}