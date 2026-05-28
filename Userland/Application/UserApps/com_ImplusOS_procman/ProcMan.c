#include "../../../API/File.h"
#include "../../../API/Serial.h"
#include "../../../API/Process.h"
#include "../../../API/Graphics.h"
#include "../../../API/Window.h"
#include "../../../API/Input.h"
#include "../../../API/Time.h"
#include "../../../../libc/include/string.h"
#include "../../../../libc/include/stdlib.h"
#include "../../../../libc/include/stdio.h"

#define MAX_PROCESSES 128
#define REFRESH_INTERVAL_MS 1000

typedef struct {
    int32_t pid;
    char name[64];
    uint8_t state;
    uint64_t ticks;
    uint64_t mem;
    double cpu_usage;
} proc_entry_t;

static window_id_t g_win = 0;
static proc_entry_t g_procs[MAX_PROCESSES];
static uint64_t g_last_ticks[MAX_PROCESSES];
static int g_proc_count = 0;
static int g_selected_idx = 0;
static uint64_t g_total_mem = 0;
static uint64_t g_used_mem = 0;
static uint64_t g_last_update_ms = 0;

#define COLOR_BG      0xFF121212
#define COLOR_HEADER  0xFF1F1F1F
#define COLOR_TEXT    0xFFE0E0E0
#define COLOR_SEL     0xFF333333
#define COLOR_ACCENT  0xFF03DAC6
#define COLOR_DEAD    0xFF757575

static void refresh_procs(void) {
    int count = get_process_count();
    if (count > MAX_PROCESSES) count = MAX_PROCESSES;
    
    int actual_count = 0;
    uint64_t current_ms = get_uptime_ms();
    uint64_t delta_ms = current_ms - g_last_update_ms;
    
    for (int i = 0; i < count; i++) {
        process_info_t info;
        if (get_process_info(i, &info) == 0) {
            g_procs[actual_count].pid = info.pid;
            strncpy(g_procs[actual_count].name, info.name, 63);
            g_procs[actual_count].name[63] = '\0';
            g_procs[actual_count].state = info.state;
            g_procs[actual_count].ticks = info.total_ticks;
            g_procs[actual_count].mem = info.memory_usage;
            
            if (delta_ms > 0 && i < MAX_PROCESSES) {
                uint64_t tick_diff = info.total_ticks - g_last_ticks[i];
                // Just a rough estimate for display
                g_procs[actual_count].cpu_usage = (double)tick_diff * 100.0 / (double)delta_ms;
            } else {
                g_procs[actual_count].cpu_usage = 0;
            }
            if (i < MAX_PROCESSES) {
                g_last_ticks[i] = info.total_ticks;
            }
            actual_count++;
        }
    }
    g_proc_count = actual_count;
    g_total_mem = get_total_memory();
    g_used_mem = get_used_memory();
    g_last_update_ms = current_ms;

    if (g_selected_idx >= g_proc_count) {
        g_selected_idx = g_proc_count - 1;
    }
    if (g_selected_idx < 0 && g_proc_count > 0) {
        g_selected_idx = 0;
    }
}

static void draw(void) {
    window_clear(g_win);
    
    char buf[128];
    // Header
    uint64_t used_mb = g_used_mem / (1024 * 1024);
    uint64_t total_mb = g_total_mem / (1024 * 1024);
    snprintf(buf, sizeof(buf), "System Monitor | Memory: %llu MB / %llu MB", used_mb, total_mb);
    window_draw_text(g_win, 10, 10, buf, COLOR_ACCENT, 16.0f);
    
    // Table Header
    window_draw_text(g_win, 10, 40, "PID", COLOR_TEXT, 14.0f);
    window_draw_text(g_win, 60, 40, "Name", COLOR_TEXT, 14.0f);
    window_draw_text(g_win, 220, 40, "State", COLOR_TEXT, 14.0f);
    window_draw_text(g_win, 300, 40, "CPU%", COLOR_TEXT, 14.0f);
    window_draw_text(g_win, 380, 40, "Memory", COLOR_TEXT, 14.0f);
    
    // Rows
    for (int i = 0; i < g_proc_count; i++) {
        uint32_t text_color = (i == g_selected_idx) ? COLOR_ACCENT : COLOR_TEXT;
        int y = 70 + i * 20;
        
        snprintf(buf, sizeof(buf), "%d", g_procs[i].pid);
        window_draw_text(g_win, 10, y, buf, text_color, 14.0f);
        
        window_draw_text(g_win, 60, y, g_procs[i].name, text_color, 14.0f);
        
        const char *state_str = "Unknown";
        if (g_procs[i].state == 1) state_str = "READY";
        else if (g_procs[i].state == 2) state_str = "RUNNING";
        else if (g_procs[i].state == 3) state_str = "DEAD";
        else if (g_procs[i].state == 5) state_str = "ZOMBIE";
        window_draw_text(g_win, 220, y, state_str, text_color, 14.0f);
        
        snprintf(buf, sizeof(buf), "%.1f%%", g_procs[i].cpu_usage);
        window_draw_text(g_win, 300, y, buf, text_color, 14.0f);
        
        snprintf(buf, sizeof(buf), "%llu KB", g_procs[i].mem / 1024);
        window_draw_text(g_win, 380, y, buf, text_color, 14.0f);
    }
    
    // Footer
    window_draw_text(g_win, 10, 470, "[Up/Down] Select  [K] Kill  [R] Refresh", COLOR_DEAD, 12.0f);
}

void procman_main(void) {
    g_win = window_create(500, 500, "Process Manager");
    if (g_win == 0) return;
    
    window_subscribe_keyboard(g_win);
    
    refresh_procs();
    draw();
    
    uint64_t last_refresh = get_uptime_ms();
    
    while (1) {
        input_keyboard_event_t ev;
        if (window_input_keyboard_poll(&ev) > 0) {
            if (ev.pressed) {
                if (ev.keycode == 0x48) { // Up
                    if (g_selected_idx > 0) g_selected_idx--;
                    draw();
                } else if (ev.keycode == 0x50) { // Down
                    if (g_selected_idx < g_proc_count - 1) g_selected_idx++;
                    draw();
                } else if (ev.ascii == 'r' || ev.ascii == 'R') {
                    refresh_procs();
                    draw();
                } else if (ev.ascii == 'k' || ev.ascii == 'K') {
                    if (g_selected_idx >= 0 && g_selected_idx < g_proc_count) {
                        process_kill(g_procs[g_selected_idx].pid);
                        refresh_procs();
                        draw();
                    }
                }
            }
        }
        
        uint64_t now = get_uptime_ms();
        if (now - last_refresh >= REFRESH_INTERVAL_MS) {
            refresh_procs();
            draw();
            last_refresh = now;
        }
        
        process_yield();
    }
}

void _start(void) {
    procman_main();
    process_exit(0);
}
