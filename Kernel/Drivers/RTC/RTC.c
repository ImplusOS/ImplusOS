#include "RTC.h"
#include "../../IO/IO_Main.h"

#define CMOS_ADDR 0x70
#define CMOS_DATA 0x71

static int is_update_in_progress(void) {
    outb(CMOS_ADDR, 0x0A);
    return (inb(CMOS_DATA) & 0x80);
}

static uint8_t get_rtc_register(int reg) {
    outb(CMOS_ADDR, (uint8_t)reg);
    return inb(CMOS_DATA);
}

void rtc_init(void) {
}

void rtc_read_time(rtc_time_t *time) {
    while (is_update_in_progress());

    uint8_t second = get_rtc_register(0x00);
    uint8_t minute = get_rtc_register(0x02);
    uint8_t hour   = get_rtc_register(0x04);
    uint8_t day    = get_rtc_register(0x07);
    uint8_t month  = get_rtc_register(0x08);
    uint8_t year   = get_rtc_register(0x09);
    uint8_t registerB = get_rtc_register(0x0B);

    if (!(registerB & 0x04)) {
        second = (uint8_t)((second & 0x0F) + ((second / 16) * 10));
        minute = (uint8_t)((minute & 0x0F) + ((minute / 16) * 10));
        hour   = (uint8_t)(((hour & 0x0F) + (((hour & 0x70) / 16) * 10)) | (hour & 0x80));
        day    = (uint8_t)((day & 0x0F) + ((day / 16) * 10));
        month  = (uint8_t)((month & 0x0F) + ((month / 16) * 10));
        year   = (uint8_t)((year & 0x0F) + ((year / 16) * 10));
    }

    if (!(registerB & 0x02) && (hour & 0x80)) {
        hour = (uint8_t)(((hour & 0x7F) + 12) % 24);
    }

    time->second = second;
    time->minute = minute;
    time->hour   = hour;
    time->day    = day;
    time->month  = month;
    time->year   = (uint16_t)(2000 + year);
}
