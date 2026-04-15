#include "rs485.h"
#include "config.h"
#include <stdarg.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

static HardwareSerial RS485Serial(2);
RS485Port RS485;

// ===== TX QUEUE =====
typedef struct {
    uint16_t len;
    uint8_t data[256];
} tx_item_t;

static QueueHandle_t txQueue;

// ===== STDOUT HOOK =====
extern "C" int _write(int fd, const void *data, size_t size)
{
    RS485.write((const uint8_t*)data, size);
    return size;
}

// ===== INIT =====
void RS485Port::begin(uint32_t baud)
{
    pinMode((int)RS485_DE, OUTPUT);
    digitalWrite((int)RS485_DE, LOW);

    RS485Serial.begin(baud, SERIAL_8N1, (int)RS485_RO, (int)RS485_DI);

    txQueue = xQueueCreate(64, sizeof(tx_item_t));

    xTaskCreatePinnedToCore(
        txTask,
        "rs485_tx",
        4096,
        this,
        1,          // LOW priority (important)
        NULL,
        1
    );
}

// ===== LOW LEVEL =====
void RS485Port::setTX()
{
    digitalWrite((int)RS485_DE, HIGH);
}

void RS485Port::setRX()
{
    digitalWrite((int)RS485_DE, LOW);
}

// ===== ENQUEUE =====
void RS485Port::enqueue(const uint8_t *data, size_t len)
{
    if (!txQueue) return;

    tx_item_t item;
    item.len = len > sizeof(item.data) ? sizeof(item.data) : len;
    memcpy(item.data, data, item.len);

    xQueueSend(txQueue, &item, 0); // NON-BLOCKING
}

// ===== PRINT API =====
void RS485Port::print(const char *str)
{
    enqueue((const uint8_t*)str, strlen(str));
}

void RS485Port::println(const char *str)
{
    enqueue((const uint8_t*)str, strlen(str));
    enqueue((const uint8_t*)"\r\n", 2);
}

void RS485Port::printf(const char *format, ...)
{
    char buffer[256];

    va_list args;
    va_start(args, format);
    int len = vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    if (len > 0)
    {
        enqueue((uint8_t*)buffer, len);
    }
}

void RS485Port::write(const uint8_t *data, size_t len)
{
    enqueue(data, len);
}

// ===== TX TASK =====
void RS485Port::txTask(void *param)
{
    RS485Port *self = (RS485Port*)param;
    tx_item_t item;

    while (true)
    {
        if (xQueueReceive(txQueue, &item, portMAX_DELAY))
        {
            self->setTX();

            // send first
            RS485Serial.write(item.data, item.len);

            // batch send (IMPORTANT)
            while (uxQueueMessagesWaiting(txQueue))
            {
                if (xQueueReceive(txQueue, &item, 0))
                {
                    RS485Serial.write(item.data, item.len);
                }
            }

            RS485Serial.flush();
            self->setRX();
        }
    }
}

// ===== READ =====
int RS485Port::available()
{
    return RS485Serial.available();
}

int RS485Port::read()
{
    return RS485Serial.read();
}

size_t RS485Port::readBytes(uint8_t* buf, size_t maxLen)
{
    size_t n = 0;

    while (available() && n < maxLen)
    {
        int b = read();
        if (b < 0) break;
        buf[n++] = (uint8_t)b;
    }

    return n;
}

// ===== printf hook =====
int rs485_vprintf(const char *fmt, va_list args)
{
    char buffer[256];
    int len = vsnprintf(buffer, sizeof(buffer), fmt, args);

    if (len > 0)
    {
        RS485.write((uint8_t*)buffer, len);
    }
    return len;
}