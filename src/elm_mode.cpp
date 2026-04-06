#include <Arduino.h>
#include "elm_mode.h"
#include "can_bus.h"
#include "transport.h"
#include "app_mode.h"
#include <string.h>
#include <ctype.h>
#include "debug.h"

// ===== INPUT BUFFER =====
static char line[32];
static uint8_t idx = 0;

// ===== SESSION =====
static bool waiting = false;
static uint32_t startMs = 0;

// ===== ISO-TP RX =====
static uint8_t rxBuf[128];
static uint16_t rxLen = 0;
static uint16_t rxExpected = 0;
static bool multiFrame = false;

// ===== HELPERS =====
static void sendPrompt()
{
    const char *p = ">\r";
    transportWrite((const uint8_t *)p, strlen(p));
}

static uint8_t hexToByte(char h, char l)
{
    auto hex = [](char c) -> uint8_t
    {
        if (c >= '0' && c <= '9')
            return c - '0';
        if (c >= 'A' && c <= 'F')
            return c - 'A' + 10;
        return 0;
    };
    return (hex(h) << 4) | hex(l);
}

// ===== SEND REQUEST =====
static void sendCAN(uint8_t mode, uint8_t pid)
{
    twai_message_t tx = {};
    tx.identifier = 0x7DF;
    tx.data_length_code = 8;

    tx.data[0] = 0x02;
    tx.data[1] = mode;
    tx.data[2] = pid;

    CANDriver::send(tx);

    waiting = true;
    startMs = millis();

    // reset RX
    rxLen = 0;
    rxExpected = 0;
    multiFrame = false;
}

// ===== PROCESS LINE =====
static void processLine()
{
    // normalize (uppercase + remove spaces)
    char clean[32];
    int j = 0;

    for (int i = 0; i < idx; i++)
    {
        char c = line[i];
        if (c == ' ')
            continue;
        clean[j++] = toupper(c);
    }
    clean[j] = 0;

    DEBUG("%s\n", clean);

    // AT commands
    if (strcmp(clean, "ATZ") == 0)
    {
        const char *s = "ELM327 v1.5\r";
        transportWrite((const uint8_t *)s, strlen(s));
        sendPrompt();
        return;
    }

    if (strcmp(clean, "ATE0") == 0)
    {
        const char *s = "OK\r";
        transportWrite((const uint8_t *)s, strlen(s));
        sendPrompt();
        return;
    }

    // OBD
    if (strlen(clean) >= 4)
    {
        uint8_t mode = hexToByte(clean[0], clean[1]);
        uint8_t pid = hexToByte(clean[2], clean[3]);

        sendCAN(mode, pid);
        return;
    }

    sendPrompt();
}

// ===== INPUT =====
void elmProcessByte(uint8_t b)
{
    char c = (char)b;

    if (c == '\r' || c == '\n')
    {
        line[idx] = 0;
        processLine();
        idx = 0;
    }
    else if (idx < sizeof(line) - 1)
    {
        line[idx++] = c;
    }
}

// ===== ISO-TP RX =====
static void handleCAN()
{
    CANRxItem item;

    while (rxBufferPop(item))
    {
        const auto &m = item.msg;

        if (m.identifier < 0x7E8 || m.identifier > 0x7EF)
            continue;

        uint8_t pci = m.data[0] & 0xF0;

        // --- SINGLE FRAME ---
        if (pci == 0x00)
        {
            uint8_t len = m.data[0];

            for (int i = 0; i < len; i++)
                rxBuf[i] = m.data[i + 1];

            rxLen = len;
            waiting = false;
            return;
        }

        // --- FIRST FRAME ---
        if (pci == 0x10)
        {
            rxExpected = ((m.data[0] & 0x0F) << 8) | m.data[1];

            memcpy(rxBuf, &m.data[2], 6);
            rxLen = 6;
            multiFrame = true;

            // ===== SEND FLOW CONTROL =====
            twai_message_t fc = {};
            fc.identifier = 0x7E0;
            fc.extd = 0;
            fc.rtr = 0;
            fc.data_length_code = 8;

            fc.data[0] = 0x30; // CTS
            fc.data[1] = 0x00;
            fc.data[2] = 0x00;

            CANDriver::send(fc);

            return;
        }

        // --- CONSECUTIVE ---
        if (pci == 0x20 && multiFrame)
        {
            uint8_t chunk = m.data_length_code - 1;

            memcpy(rxBuf + rxLen, &m.data[1], chunk);
            rxLen += chunk;

            if (rxLen >= rxExpected)
            {
                waiting = false;
                return;
            }
        }
    }
}

// ===== OUTPUT =====
static void outputResponse()
{
    char out[256];
    int n = 0;

    for (int i = 0; i < rxLen; i++)
    {
        n += sprintf(out + n, "%02X ", rxBuf[i]);
    }

    out[n++] = '\r';

    transportWrite((uint8_t *)out, n);
    sendPrompt();
}

// ===== MAIN =====
void elmLoop()
{
    if (waiting)
    {
        handleCAN();

        if (!waiting && rxLen > 0)
        {
            outputResponse();
        }

        if (millis() - startMs > 200)
        {
            const char *s = "NO DATA\r";
            transportWrite((const uint8_t *)s, strlen(s));
            sendPrompt();
            waiting = false;
        }
    }
}