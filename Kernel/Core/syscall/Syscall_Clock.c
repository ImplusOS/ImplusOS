#include "Syscall_Main.h"
#include "Core/process/ProcessManager.h"
#include "Core/timer/Timer.h"
#include "Drivers/RTC/RTC.h"

#include <stddef.h>
#include <stdint.h>

#define CLOCK_REALTIME  0
#define CLOCK_MONOTONIC 1

typedef struct {
    int64_t tv_sec;
    int64_t tv_nsec;
} kernel_timespec_t;

int64_t syscall_clock_gettime(int32_t clk_id, uint64_t tp_ptr)
{
    kernel_timespec_t *tp = (kernel_timespec_t *)(uintptr_t)tp_ptr;
    if (tp == NULL || !process_user_buffer_is_valid(tp, sizeof(*tp))) {
        return -14;
    }

    if (clk_id == CLOCK_MONOTONIC) {
        uint32_t hz = timer_hz();
        if (hz == 0) hz = 60;
        uint64_t ticks = timer_ticks();
        uint64_t ms = (ticks * 1000ULL) / hz;
        tp->tv_sec  = (int64_t)(ms / 1000ULL);
        tp->tv_nsec = (int64_t)((ms % 1000ULL) * 1000000LL);
        return 0;
    }

    if (clk_id == CLOCK_REALTIME) {
        rtc_time_t rtc;
        rtc_read_time(&rtc);

        uint64_t days = 0;
        for (uint16_t y = 1970; y < rtc.year; ++y) {
            days += (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0)) ? 366 : 365;
        }
        static const uint16_t mdays[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
        for (uint8_t m = 1; m < rtc.month && m <= 12; ++m) {
            days += mdays[m - 1];
            if (m == 2 && rtc.year % 4 == 0 &&
                (rtc.year % 100 != 0 || rtc.year % 400 == 0)) {
                days += 1;
            }
        }
        days += (uint64_t)(rtc.day - 1);
        uint64_t secs = days * 86400ULL + (uint64_t)rtc.hour * 3600ULL +
                        (uint64_t)rtc.minute * 60ULL + (uint64_t)rtc.second;
        tp->tv_sec  = (int64_t)secs;
        tp->tv_nsec = 0;
        return 0;
    }

    return -22;
}

int64_t syscall_clock_getres(int32_t clk_id, uint64_t res_ptr)
{
    kernel_timespec_t *res = (kernel_timespec_t *)(uintptr_t)res_ptr;
    if (res == NULL) return 0;
    if (!process_user_buffer_is_valid(res, sizeof(*res))) {
        return -14;
    }

    if (clk_id == CLOCK_REALTIME || clk_id == CLOCK_MONOTONIC) {
        res->tv_sec  = 0;
        res->tv_nsec = 1000000;
        return 0;
    }
    return -22;
}
