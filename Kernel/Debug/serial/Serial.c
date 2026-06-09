#include <stdbool.h>
#include <stdint.h>

#define UART0_BASE 0x09000000UL

#define UART_DR   (*(volatile uint32_t *)(UART0_BASE + 0x00))
#define UART_FR   (*(volatile uint32_t *)(UART0_BASE + 0x18))
#define UART_IBRD (*(volatile uint32_t *)(UART0_BASE + 0x24))
#define UART_FBRD (*(volatile uint32_t *)(UART0_BASE + 0x28))
#define UART_LCRH (*(volatile uint32_t *)(UART0_BASE + 0x2C))
#define UART_CR   (*(volatile uint32_t *)(UART0_BASE + 0x30))

#define UART_FR_TXFF (1 << 5)

static void uart_write_char(char c) {
    uint32_t timeout = 0x100000;
    while (UART_FR & UART_FR_TXFF) {
        if (--timeout == 0) {
            return;
        }
    }

    UART_DR = (uint32_t)c;
}

void serial_init(void) {
    UART_CR = 0x00000000;

    UART_IBRD = 26;
    UART_FBRD = 3;

    UART_LCRH = (3 << 5);

    UART_CR = (1 << 0) | (1 << 8) | (1 << 9);
}

void serial_write_char(char c) {
    uart_write_char(c);
}

void serial_write_string(const char* str) {
    if (!str) return;

    while (*str) {
        if (*str == '\n') {
            uart_write_char('\r');
        }
        uart_write_char(*str++);
    }
}

static void serial_write_hex(uint64_t value, uint8_t nibbles) {
    static const char hex[] = "0123456789ABCDEF";

    serial_write_string("0x");

    for (int i = (int)((nibbles - 1u) * 4u); i >= 0; i -= 4) {
        serial_write_char(hex[(value >> i) & 0xF]);
    }
}

void serial_write_uint64(uint64_t value) {
    serial_write_hex(value, 16u);
}

void serial_write_uint32(uint32_t value) {
    serial_write_hex(value, 8u);
}

void serial_write_uint16(uint16_t value) {
    serial_write_hex(value, 4u);
}

void serial_write_uint8(uint8_t value) {
    serial_write_hex(value, 2u);
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