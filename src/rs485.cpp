// FILE: rs485.cpp
// PURPOSE: RS485 debug transport using UART2 and DE pin control.

#include "rs485.h"
#include "config.h"
#include <stdarg.h>
// #include <freertos/FreeRTOS.h>
// #include <freertos/semphr.h>

// #define RS485_DE_PIN 17
// #define RS485_RO_PIN 21
// #define RS485_DI_PIN 22

static HardwareSerial RS485Serial(2);

RS485Port RS485;

extern "C" int _write(int fd, const void *data, size_t size)
{
    RS485.write((const uint8_t*)data, size);
    return size;
}

void RS485Port::begin(uint32_t baud)
{
    pinMode((int)RS485_DE, OUTPUT);
    digitalWrite((int)RS485_DE, LOW); // start in RX

    RS485Serial.begin(baud, SERIAL_8N1, (int)RS485_RO, (int)RS485_DI);
}

void RS485Port::setTX()
{
    digitalWrite((int)RS485_DE, HIGH);
}

void RS485Port::setRX()
{
    digitalWrite((int)RS485_DE, LOW);
}

void RS485Port::print(const char *str)
{
    setTX();
    RS485Serial.print(str);
    RS485Serial.flush();
    setRX();
}

void RS485Port::println(const char *str)
{
    setTX();
    RS485Serial.println(str);
    RS485Serial.flush();
    setRX();
}

void RS485Port::printf(const char *format, ...)
{
    char buffer[256];

    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    setTX();
    RS485Serial.print(buffer);
    RS485Serial.flush();
    setRX();
}

void RS485Port::write(const uint8_t *data, size_t len)
{
    setTX();
    RS485Serial.write(data, len);
    RS485Serial.flush();
    setRX();
}

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