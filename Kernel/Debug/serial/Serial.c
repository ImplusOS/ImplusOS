#include "Serial.h"

#include <stddef.h>
#include <stdint.h>

#include "interfaces/uart_hal.h"

static const uart_hal_t *g_uart = NULL;

static void serial_backend_init(void)
{
    if (g_uart == NULL) {
        g_uart = uart_hal_get();
    }
}

void serial_init(void)
{
    serial_backend_init();
    if (g_uart && g_uart->init) {
        g_uart->init(115200);
    }
}

void serial_write_char(char c)
{
    serial_backend_init();
    if (!g_uart || !g_uart->write_char) {
        return;
    }
    g_uart->write_char(c);
}

void serial_write_string(const char *str)
{
    if (!str) {
        return;
    }

    serial_backend_init();
    if (!g_uart) {
        return;
    }

    if (g_uart->write_string) {
        g_uart->write_string(str);
        return;
    }

    while (*str) {
        serial_write_char(*str++);
    }
}

static void serial_write_hex(uint64_t value, uint8_t nibbles)
{
    static const char hex[] = "0123456789ABCDEF";

    serial_write_string("0x");

    for (int i = (int)((nibbles - 1u) * 4u); i >= 0; i -= 4) {
        serial_write_char(hex[(value >> i) & 0xF]);
    }
}

void serial_write_uint64(uint64_t value)
{
    serial_write_hex(value, 16u);
}

void serial_write_uint32(uint32_t value)
{
    serial_write_hex(value, 8u);
}

void serial_write_uint16(uint16_t value)
{
    serial_write_hex(value, 4u);
}

void serial_write_uint8(uint8_t value)
{
    serial_write_hex(value, 2u);
}

void serial_write_dec16(uint16_t v)
{
    char buf[6];
    int i = (int)sizeof(buf);
    buf[--i] = '\0';

    if (v == 0) {
        buf[--i] = '0';
    } else {
        while (v > 0 && i > 0) {
            buf[--i] = (char)('0' + (v % 10));
            v /= 10;
        }
    }

    serial_write_string(&buf[i]);
}
