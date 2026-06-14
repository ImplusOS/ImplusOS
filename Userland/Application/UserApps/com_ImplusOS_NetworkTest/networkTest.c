#include "../../../API/File.h"
#include "../../../API/Serial.h"
#include "../../../API/Process.h"
#include "../../../API/Network.h"
#include "../../../API/Graphics.h"
#include "../../../API/Window.h"
#include "../../../API/Input.h"
#include "networkTest.h"
#include "../../../../libc/include/string.h"
#include "../../../../libc/include/stdio.h"

#define NETTEST_LOG_LINES 12
#define NETTEST_LINE_LEN  96

static window_id_t g_win = 0;
static char g_log[NETTEST_LOG_LINES][NETTEST_LINE_LEN];
static int g_log_count = 0;

static void ip_to_string(uint32_t ip, char *out, size_t out_size)
{
    snprintf(out, out_size, "%u.%u.%u.%u",
             (unsigned)((ip >> 24) & 0xFFu),
             (unsigned)((ip >> 16) & 0xFFu),
             (unsigned)((ip >> 8) & 0xFFu),
             (unsigned)(ip & 0xFFu));
}

static void add_log(const char *line)
{
    if (!line) return;
    if (g_log_count < NETTEST_LOG_LINES) {
        strncpy(g_log[g_log_count], line, NETTEST_LINE_LEN - 1);
        g_log[g_log_count][NETTEST_LINE_LEN - 1] = '\0';
        g_log_count++;
        return;
    }

    for (int i = 1; i < NETTEST_LOG_LINES; ++i) {
        memcpy(g_log[i - 1], g_log[i], NETTEST_LINE_LEN);
    }
    strncpy(g_log[NETTEST_LOG_LINES - 1], line, NETTEST_LINE_LEN - 1);
    g_log[NETTEST_LOG_LINES - 1][NETTEST_LINE_LEN - 1] = '\0';
}

static void render(void)
{
    window_clear(g_win);
    draw_fill_rect(0, 0, 520, 360, 0xFF101820);
    draw_fill_rect(0, 0, 520, 48, 0xFF183044);
    window_draw_text(g_win, 16, 12, "Network Test", 0xFFE6F7FF, 18.0f);
    window_draw_text(g_win, 360, 16, "R refresh  Q quit", 0xFF9FC7D5, 12.0f);

    for (int i = 0; i < g_log_count; ++i) {
        window_draw_text(g_win, 18, (uint32_t)(64 + i * 22), g_log[i], 0xFFD8E8EE, 13.0f);
    }
}

static void run_tests(void)
{
    g_log_count = 0;
    add_log("UDP send to 10.0.2.2:12346...");
    const char *message = "Hello, Network!";
    uint16_t len = (uint16_t)strlen(message);
    bool success = udp_send(0x0A000202u, 12345, 12346, message, len);
    add_log(success ? "  ok: packet queued" : "  failed: udp_send returned false");

    add_log("UDP broadcast to 255.255.255.255:12346...");
    success = udp_send(0xFFFFFFFFu, 12345, 12346, message, len);
    add_log(success ? "  ok: broadcast packet queued" : "  failed: broadcast send returned false");

    const char *hosts[] = {"example.com", "www.google.com"};
    for (uint32_t i = 0; i < sizeof(hosts) / sizeof(hosts[0]); ++i) {
        char line[NETTEST_LINE_LEN];
        snprintf(line, sizeof(line), "DNS resolve %s...", hosts[i]);
        add_log(line);
        uint32_t ip = dns_resolve(hosts[i]);
        if (ip == 0u) {
            snprintf(line, sizeof(line), "  failed: no address returned");
        } else {
            char ip_buf[24];
            ip_to_string(ip, ip_buf, sizeof(ip_buf));
            snprintf(line, sizeof(line), "  ok: %s", ip_buf);
        }
        add_log(line);
    }
}

void networkTest_main(void)
{
    g_win = window_create_ex(140, 90, 520, 360, 0xFF101820, "Network Test");
    if (g_win == 0) return;
    window_subscribe_keyboard(g_win);
    graphics_init(g_win);
    run_tests();
    render();
}

void _start(void)
{
    networkTest_main();
    while (1) {
        input_keyboard_event_t ev;
        if (window_input_keyboard_poll(&ev) > 0 && ev.pressed) {
            if (ev.ascii == 'q' || ev.ascii == 'Q') {
                process_exit(0);
            }
            if (ev.ascii == 'r' || ev.ascii == 'R') {
                run_tests();
                render();
            }
        }
        process_yield();
    }
}
