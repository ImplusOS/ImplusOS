#include <stdbool.h>
#include <stdint.h>
#include "Platform/io/IO_Main.h"
#include "Debug/printf/printf.h"

#define COM1_PORT 0x3F8

static void serial_io_write_char(char c) {
    uint32_t timeout = 0x100000u;
    while ((inb(COM1_PORT + 5) & 0x20) == 0) {
        if (--timeout == 0) {
            return;
        }
    }
    outb(COM1_PORT, c);
}

void serial_init(void) {
    outb(COM1_PORT + 1, 0x00);
    outb(COM1_PORT + 3, 0x80);
    outb(COM1_PORT + 0, 0x03);
    outb(COM1_PORT + 1, 0x00);
    
    if (inb(COM1_PORT + 1) == 0xFF) {
        return;
    }

    outb(COM1_PORT + 3, 0x03);
    outb(COM1_PORT + 2, 0xC7);
    outb(COM1_PORT + 4, 0x0B);
}

void serial_write_char(char c) {
    serial_io_write_char(c);
}

void serial_write_string(const char* str) {
    if (!str) return;

    const char* p = str;

    while (*p) {
        if (*p == '\n')
            serial_write_char('\r');

        serial_write_char(*p);
        p++;
    }
}

void serial_write_uint64(uint64_t value) {
    char hex[] = "0123456789ABCDEF";
    serial_write_string("0x");
    for (int i = 60; i >= 0; i -= 4) {
        serial_write_char(hex[(value >> i) & 0xF]);
    }
}

void serial_write_uint32(uint32_t value) {
    char hex[] = "0123456789ABCDEF";
    serial_write_string("0x");
    for (int i = 28; i >= 0; i -= 4) {
        serial_write_char(hex[(value >> i) & 0xF]);
    }
}

void serial_write_uint16(uint16_t value) {
    char hex[] = "0123456789ABCDEF";
    serial_write_string("0x");
    for (int i = 12; i >= 0; i -= 4) {
        serial_write_char(hex[(value >> i) & 0xF]);
    }
}

void serial_write_uint8(uint8_t value) {
    char hex[] = "0123456789ABCDEF";
    serial_write_string("0x");
    serial_write_char(hex[(value >> 4) & 0xF]);
    serial_write_char(hex[value & 0xF]);
}

void panic(const char *msg) {
    serial_write_string("PANIC: ");
    serial_write_string(msg);
    while (1);
}

void serial_write_dec16(uint16_t v) {
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