#include <stdint.h>
#include <string.h>
#include "WM_Protocol.h"
#include "Syscalls.h"
#include "Network.h"
#include "Process.h"
#include "Time.h"

#define NTP_PORT 123
#define GOOGLE_NTP_SERVER "time.google.com"
#define NTP_TIMESTAMP_DELTA 2208988800ull

typedef struct {
    uint8_t li_vn_mode;
    uint8_t stratum;
    uint8_t poll;
    uint8_t precision;
    uint32_t root_delay;
    uint32_t root_dispersion;
    uint32_t ref_id;
    uint32_t ref_ts_sec;
    uint32_t ref_ts_frac;
    uint32_t origin_ts_sec;
    uint32_t origin_ts_frac;
    uint32_t recv_ts_sec;
    uint32_t recv_ts_frac;
    uint32_t trans_ts_sec;
    uint32_t trans_ts_frac;
} ntp_packet_t;

static uint32_t swap_uint32(uint32_t val) {
    return ((val & 0xFF000000) >> 24) |
           ((val & 0x00FF0000) >> 8) |
           ((val & 0x0000FF00) << 8) |
           ((val & 0x000000FF) << 24);
}

static uint64_t ntp_sync(void) {
    uint32_t server_ip = dns_resolve(GOOGLE_NTP_SERVER);
    if (server_ip == 0) return 0;

    ntp_packet_t packet;
    memset(&packet, 0, sizeof(packet));
    packet.li_vn_mode = 0x1B;

    uint16_t src_port = 12345;
    if (udp_bind_port(src_port) < 0) return 0;

    if (!udp_send(server_ip, src_port, NTP_PORT, &packet, sizeof(packet))) {
        udp_unbind_port(src_port);
        return 0;
    }

    uint8_t recv_buf[512];
    int32_t len = 0;

    for (int i = 0; i < 50; i++) {
        len = udp_recv(src_port, recv_buf, sizeof(recv_buf));
        if (len > 0) break;
        sleep_ms(20);
    }

    udp_unbind_port(src_port);

    if (len < (int32_t)(8 + sizeof(ntp_packet_t))) return 0;

    ntp_packet_t *response = (ntp_packet_t *)(recv_buf + 8);
    uint32_t tx_sec = swap_uint32(response->trans_ts_sec);
    
    if (tx_sec == 0) return 0;
    
    return (uint64_t)(tx_sec - (uint32_t)NTP_TIMESTAMP_DELTA);
}

static void itoa_2d(char *buf, int val) {
    buf[0] = (char)('0' + (val / 10) % 10);
    buf[1] = (char)('0' + val % 10);
}

static void itoa_4d(char *buf, int val) {
    int v = val;
    buf[3] = (char)('0' + v % 10); v /= 10;
    buf[2] = (char)('0' + v % 10); v /= 10;
    buf[1] = (char)('0' + v % 10); v /= 10;
    buf[0] = (char)('0' + v % 10);
}

static void unix_to_rtc(uint64_t t, rtc_time_t *rtc) {
    t += 9 * 3600;
    
    rtc->second = (uint8_t)(t % 60); t /= 60;
    rtc->minute = (uint8_t)(t % 60); t /= 60;
    rtc->hour   = (uint8_t)(t % 24); t /= 24;
    
    uint32_t days = (uint32_t)t;
    int year = 1970;
    while (1) {
        int leap = (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
        int days_in_year = leap ? 366 : 365;
        if (days < (uint32_t)days_in_year) break;
        days -= (uint32_t)days_in_year;
        year++;
    }
    rtc->year = (uint16_t)year;
    
    int leap = (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
    static const uint8_t month_days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int month = 0;
    for (month = 0; month < 12; month++) {
        int d = month_days[month];
        if (month == 1 && leap) d = 29;
        if (days < (uint32_t)d) break;
        days -= (uint32_t)d;
    }
    rtc->month = (uint8_t)(month + 1);
    rtc->day = (uint8_t)(days + 1);
}

void _start(void) {
    int32_t wm_pid = window_get_wm_pid();
    if (wm_pid < 0) process_exit(1);

    uint64_t base_ntp_time = 0;
    uint64_t base_uptime_ms = 0;
    uint64_t last_sync_ms = 0;

    base_ntp_time = ntp_sync();
    if (base_ntp_time != 0) {
        base_uptime_ms = get_uptime_ms();
        last_sync_ms = base_uptime_ms;
    }

    struct {
        wm_msg_header_t hdr;
        char time_str[32];
    } msg;

    uint8_t last_sec = 255;

    while (1) {
        uint64_t current_uptime_ms = get_uptime_ms();

        if (current_uptime_ms - last_sync_ms > 3600000) {
            uint64_t new_ntp = ntp_sync();
            if (new_ntp != 0) {
                base_ntp_time = new_ntp;
                base_uptime_ms = current_uptime_ms;
                last_sync_ms = current_uptime_ms;
            }
        }

        rtc_time_t rtc;
        if (base_ntp_time != 0) {
            uint64_t current_unix_time = base_ntp_time + (current_uptime_ms - base_uptime_ms) / 1000;
            unix_to_rtc(current_unix_time, &rtc);
        } else {
            if (sys_get_rtc_time(&rtc) != 0) {
                rtc.year = 0; rtc.month = 1; rtc.day = 1;
                rtc.hour = 0; rtc.minute = 0; rtc.second = (uint8_t)((current_uptime_ms / 1000) % 60);
            }
        }

        if (rtc.second != last_sec) {
            last_sec = rtc.second;
            
            memset(&msg, 0, sizeof(msg));
            msg.hdr.type = WM_UPDATE_CLOCK;
            
            itoa_4d(&msg.time_str[0], rtc.year);
            msg.time_str[4] = '/';
            itoa_2d(&msg.time_str[5], rtc.month);
            msg.time_str[7] = '/';
            itoa_2d(&msg.time_str[8], rtc.day);
            msg.time_str[10] = ' ';
            itoa_2d(&msg.time_str[11], rtc.hour);
            msg.time_str[13] = ':';
            itoa_2d(&msg.time_str[14], rtc.minute);
            msg.time_str[16] = ':';
            itoa_2d(&msg.time_str[17], rtc.second);
            msg.time_str[19] = '\0';

            ipc_send_message(wm_pid, &msg, (uint32_t)sizeof(msg));
        }
        
        sleep_ms(500);
    }
}
