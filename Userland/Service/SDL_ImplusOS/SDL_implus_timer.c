#include "SDL_internal.h"

#include "time/SDL_time_c.h"

#include <stdint.h>
#include "Userland/API/Process.h"
#include "Userland/API/Time.h"

static int is_leap_year(int year)
{
    return ((year % 4) == 0 && (year % 100) != 0) || ((year % 400) == 0);
}

static int days_in_month(int year, int month)
{
    static const int days[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if (month == 2 && is_leap_year(year)) {
        return 29;
    }
    if (month < 1 || month > 12) {
        return 30;
    }
    return days[month - 1];
}

static Sint64 civil_to_days(int year, int month, int day)
{
    year -= month <= 2;
    const int era = (year >= 0 ? year : year - 399) / 400;
    const unsigned yoe = (unsigned)(year - era * 400);
    const unsigned doy = (153u * (unsigned)(month > 2 ? month - 3 : month + 9) + 2u) / 5u + (unsigned)day - 1u;
    const unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
    return (Sint64)era * 146097 + (Sint64)doe - 719468;
}

static void days_to_civil(Sint64 z, int *year, int *month, int *day)
{
    z += 719468;
    const Sint64 era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = (unsigned)(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const Sint64 y = (Sint64)yoe + era * 400;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    const unsigned d = doy - (153 * mp + 2) / 5 + 1;
    const unsigned m = mp < 10 ? mp + 3 : mp - 9;

    *year = (int)(y + (m <= 2));
    *month = (int)m;
    *day = (int)d;
}

Uint64 SDL_GetPerformanceCounter(void)
{
    return get_uptime_ms() * SDL_NS_PER_MS;
}

Uint64 SDL_GetPerformanceFrequency(void)
{
    return SDL_NS_PER_SECOND;
}

void SDL_SYS_DelayNS(Uint64 ns)
{
    Uint64 ms = ns / SDL_NS_PER_MS;
    if ((ns % SDL_NS_PER_MS) != 0) {
        ++ms;
    }
    sleep_ms(ms);
}

void SDL_GetSystemTimeLocalePreferences(SDL_DateFormat *df, SDL_TimeFormat *tf)
{
    if (df) {
        *df = SDL_DATE_FORMAT_YYYYMMDD;
    }
    if (tf) {
        *tf = SDL_TIME_FORMAT_24HR;
    }
}

bool SDL_GetCurrentTime(SDL_Time *ticks)
{
    CHECK_PARAM(!ticks) {
        return SDL_InvalidParamError("ticks");
    }

    rtc_time_t rtc;
    if (sys_get_rtc_time(&rtc) < 0) {
        return SDL_SetError("sys_get_rtc_time failed");
    }

    Sint64 days = civil_to_days((int)rtc.year, (int)rtc.month, (int)rtc.day);
    Sint64 seconds = days * 86400 +
                     (Sint64)rtc.hour * 3600 +
                     (Sint64)rtc.minute * 60 +
                     (Sint64)rtc.second;
    *ticks = (SDL_Time)(seconds * SDL_NS_PER_SECOND);
    return true;
}

bool SDL_TimeToDateTime(SDL_Time ticks, SDL_DateTime *dt, bool localTime)
{
    (void)localTime;
    CHECK_PARAM(!dt) {
        return SDL_InvalidParamError("dt");
    }

    Sint64 seconds = ticks / SDL_NS_PER_SECOND;
    int nanosecond = (int)(ticks % SDL_NS_PER_SECOND);
    if (nanosecond < 0) {
        nanosecond += SDL_NS_PER_SECOND;
        --seconds;
    }

    Sint64 days = seconds / 86400;
    Sint64 rem = seconds % 86400;
    if (rem < 0) {
        rem += 86400;
        --days;
    }

    int year = 1970;
    int month = 1;
    int day = 1;
    days_to_civil(days, &year, &month, &day);

    dt->year = year;
    dt->month = month;
    dt->day = day;
    dt->hour = (int)(rem / 3600);
    rem %= 3600;
    dt->minute = (int)(rem / 60);
    dt->second = (int)(rem % 60);
    dt->nanosecond = nanosecond;
    dt->day_of_week = (int)((days + 4) % 7);
    if (dt->day_of_week < 0) {
        dt->day_of_week += 7;
    }
    dt->utc_offset = 0;

    int yday = 0;
    for (int m = 1; m < month; ++m) {
        yday += days_in_month(year, m);
    }
    (void)yday;
    return true;
}

