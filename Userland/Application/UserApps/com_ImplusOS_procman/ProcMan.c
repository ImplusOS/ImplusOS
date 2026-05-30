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
#include <stdint.h>

#define MAX_PROCESSES 256
#define MAX_VISIBLE_ROWS 18
#define REFRESH_INTERVAL_MS 1000

static const char *k_spawn_path = "/Userland/SystemApps/com_ImplusOS_shell/com_ImplusOS_shell.ELF";

typedef struct {
    int32_t pid;
    int32_t parent_pid;
    char name[64];
    uint8_t state;
    uint64_t ticks;
    uint64_t mem;
    double cpu_usage;
} proc_entry_t;

static window_id_t g_win = 0;
static proc_entry_t g_procs[MAX_PROCESSES];
static uint64_t g_last_ticks[MAX_PROCESSES];
static uint8_t g_tick_initialized[MAX_PROCESSES];
static int g_proc_count = 0;
static int g_selected_idx = 0;
static int g_scroll_offset = 0;
static uint64_t g_total_mem = 0;
static uint64_t g_used_mem = 0;
static uint64_t g_last_update_ms = 0;
static char g_status[128] = {0};

#define COLOR_BG      0xFF121212
#define COLOR_HEADER  0xFF1F1F1F
#define COLOR_TEXT    0xFFE0E0E0
#define COLOR_SEL     0xFF03DAC6
#define COLOR_ACCENT  0xFF03DAC6
#define COLOR_DEAD    0xFF757575
#define COLOR_WARN    0xFFFFB86C

static const char *get_state_name(uint8_t state)
{
    switch (state) {
        case 1: return "READY";
        case 2: return "RUNNING";
        case 3: return "DEAD";
        case 5: return "ZOMBIE";
        default: return "UNKNOWN";
    }
}

static int compare_proc(const proc_entry_t *a, const proc_entry_t *b)
{
    if (a->cpu_usage > b->cpu_usage) return -1;
    if (a->cpu_usage < b->cpu_usage) return 1;
    if (a->mem > b->mem) return -1;
    if (a->mem < b->mem) return 1;
    return a->pid - b->pid;
}

static void sort_procs(void)
{
    for (int i = 1; i < g_proc_count; i++) {
        proc_entry_t key = g_procs[i];
        int j = i - 1;
        while (j >= 0 && compare_proc(&key, &g_procs[j]) < 0) {
            g_procs[j + 1] = g_procs[j];
            j--;
        }
        g_procs[j + 1] = key;
    }
}

static void format_memory(uint64_t bytes, char *out, size_t out_size)
{
    if (bytes >= 1024ULL * 1024ULL * 1024ULL) {
        double gib = (double)bytes / (1024.0 * 1024.0 * 1024.0);
        snprintf(out, out_size, "%.1f GiB", gib);
    } else if (bytes >= 1024ULL * 1024ULL) {
        double mib = (double)bytes / (1024.0 * 1024.0);
        snprintf(out, out_size, "%.1f MiB", mib);
    } else {
        snprintf(out, out_size, "%llu KB", (unsigned long long)(bytes / 1024ULL));
    }
}

static void refresh_procs(void)
{
    int count = MAX_PROCESSES;

    int actual_count = 0;
    uint64_t current_ms = get_uptime_ms();
    uint64_t delta_ms = (g_last_update_ms == 0) ? 0 : current_ms - g_last_update_ms;

    for (int i = 0; i < count; i++) {
        process_info_t info;
        if (get_process_info(i, &info) == 0) {
            proc_entry_t *entry = &g_procs[actual_count];
            entry->pid = info.pid;
            entry->parent_pid = info.parent_pid;
            strncpy(entry->name, info.name, sizeof(entry->name) - 1);
            entry->name[sizeof(entry->name) - 1] = '\0';
            entry->state = info.state;
            entry->ticks = info.total_ticks;
            entry->mem = info.memory_usage;

            if (delta_ms > 0 && info.pid >= 0 && info.pid < MAX_PROCESSES && g_tick_initialized[info.pid] && info.total_ticks >= g_last_ticks[info.pid]) {
                uint64_t tick_diff = info.total_ticks - g_last_ticks[info.pid];
                entry->cpu_usage = (double)tick_diff * 100.0 / (double)delta_ms;
            } else {
                entry->cpu_usage = 0.0;
            }

            if (info.pid >= 0 && info.pid < MAX_PROCESSES) {
                g_last_ticks[info.pid] = info.total_ticks;
                g_tick_initialized[info.pid] = 1;
            }
            actual_count++;
        }
    }

    g_proc_count = actual_count;
    g_total_mem = get_total_memory();
    g_used_mem = get_used_memory();
    g_last_update_ms = current_ms;

    sort_procs();

    if (g_proc_count == 0) {
        g_selected_idx = 0;
        g_scroll_offset = 0;
    } else {
        if (g_selected_idx >= g_proc_count) g_selected_idx = g_proc_count - 1;
        if (g_selected_idx < 0) g_selected_idx = 0;
        if (g_selected_idx < g_scroll_offset) {
            g_scroll_offset = g_selected_idx;
        } else if (g_selected_idx >= g_scroll_offset + MAX_VISIBLE_ROWS) {
            g_scroll_offset = g_selected_idx - MAX_VISIBLE_ROWS + 1;
        }
    }
}

static void set_status(const char *text)
{
    if (text) {
        strncpy(g_status, text, sizeof(g_status) - 1);
        g_status[sizeof(g_status) - 1] = '\0';
    } else {
        g_status[0] = '\0';
    }
}

static void draw(void)
{
    window_clear(g_win);

    char buf[128];
    char used_str[64];
    char total_str[64];
    format_memory(g_used_mem, used_str, sizeof(used_str));
    format_memory(g_total_mem, total_str, sizeof(total_str));

    snprintf(buf, sizeof(buf), "Process Manager — %d active / %d slots", g_proc_count, MAX_PROCESSES);
    window_draw_text(g_win, 10, 10, buf, COLOR_ACCENT, 16.0f);

    snprintf(buf, sizeof(buf), "Memory: %s / %s", used_str, total_str);
    window_draw_text(g_win, 10, 30, buf, COLOR_TEXT, 13.0f);

    window_draw_text(g_win, 5, 60, " ", COLOR_BG, 14.0f);
    window_draw_text(g_win, 20, 60, "PID", COLOR_ACCENT, 14.0f);
    window_draw_text(g_win, 80, 60, "PPID", COLOR_ACCENT, 14.0f);
    window_draw_text(g_win, 140, 60, "Name", COLOR_ACCENT, 14.0f);
    window_draw_text(g_win, 300, 60, "State", COLOR_ACCENT, 14.0f);
    window_draw_text(g_win, 360, 60, "CPU%", COLOR_ACCENT, 14.0f);
    window_draw_text(g_win, 440, 60, "Mem", COLOR_ACCENT, 14.0f);

    for (int row = 0; row < MAX_VISIBLE_ROWS; row++) {
        int index = g_scroll_offset + row;
        if (index >= g_proc_count) break;

        proc_entry_t *entry = &g_procs[index];
        bool selected = (index == g_selected_idx);
        uint32_t text_color = selected ? COLOR_ACCENT : COLOR_TEXT;
        int y = 90 + row * 20;

        window_draw_text(g_win, 5, y, selected ? ">" : " ", selected ? COLOR_ACCENT : COLOR_DEAD, 14.0f);
        snprintf(buf, sizeof(buf), "%d", entry->pid);
        window_draw_text(g_win, 20, y, buf, text_color, 14.0f);

        snprintf(buf, sizeof(buf), "%d", entry->parent_pid);
        window_draw_text(g_win, 80, y, buf, text_color, 14.0f);

        window_draw_text(g_win, 140, y, entry->name, text_color, 14.0f);
        window_draw_text(g_win, 300, y, get_state_name(entry->state), text_color, 14.0f);

        snprintf(buf, sizeof(buf), "%.1f%%", entry->cpu_usage);
        window_draw_text(g_win, 360, y, buf, text_color, 14.0f);

        char mem_str[64];
        format_memory(entry->mem, mem_str, sizeof(mem_str));
        window_draw_text(g_win, 440, y, mem_str, text_color, 14.0f);
    }

    if (g_proc_count > 0) {
        int start = g_scroll_offset + 1;
        int end = g_scroll_offset + MAX_VISIBLE_ROWS;
        if (end > g_proc_count) end = g_proc_count;
        snprintf(buf, sizeof(buf), "Showing %d-%d of %d", start, end, g_proc_count);
        window_draw_text(g_win, 10, 430, buf, COLOR_TEXT, 12.0f);
    }

    if (g_status[0]) {
        window_draw_text(g_win, 10, 450, g_status, COLOR_WARN, 12.0f);
    }
    window_draw_text(g_win, 10, 470, "[Up/Down] Select  [N] New  [K] Kill  [R] Refresh  [Q] Quit", COLOR_DEAD, 12.0f);
}

void procman_main(void)
{
    g_win = window_create_ex(80, 60, 620, 520, COLOR_BG, "Process Manager");
    if (g_win == 0) return;

    window_subscribe_keyboard(g_win);

    refresh_procs();
    set_status("Ready");
    draw();

    uint64_t last_refresh = get_uptime_ms();

    while (1) {
        input_keyboard_event_t ev;
        if (window_input_keyboard_poll(&ev) > 0) {
            if (ev.pressed) {
                if (ev.keycode == 0x48) {
                    if (g_selected_idx > 0) {
                        g_selected_idx--;
                        if (g_selected_idx < g_scroll_offset) {
                            g_scroll_offset = g_selected_idx;
                        }
                    }
                    draw();
                } else if (ev.keycode == 0x50) {
                    if (g_selected_idx < g_proc_count - 1) {
                        g_selected_idx++;
                        if (g_selected_idx >= g_scroll_offset + MAX_VISIBLE_ROWS) {
                            g_scroll_offset = g_selected_idx - MAX_VISIBLE_ROWS + 1;
                        }
                    }
                    draw();
                } else if (ev.ascii == 'r' || ev.ascii == 'R') {
                    refresh_procs();
                    set_status("Refreshed");
                    draw();
                } else if (ev.ascii == 'n' || ev.ascii == 'N') {
                    int32_t child_pid = process_spawn(k_spawn_path);
                    if (child_pid >= 0) {
                        set_status("Spawned new shell process.");
                    } else {
                        set_status("Failed to start new process.");
                    }
                    refresh_procs();
                    draw();
                } else if (ev.ascii == 'k' || ev.ascii == 'K') {
                    if (g_selected_idx >= 0 && g_selected_idx < g_proc_count) {
                        int32_t target_pid = g_procs[g_selected_idx].pid;
                        if (target_pid == process_get_current_pid()) {
                            set_status("Cannot kill this process from within procman.");
                        } else if (process_kill(target_pid) == 0) {
                            set_status("Killed process successfully.");
                        } else {
                            set_status("Failed to kill process.");
                        }
                        refresh_procs();
                        draw();
                    }
                } else if (ev.ascii == 'q' || ev.ascii == 'Q') {
                    break;
                }
            }
        }

        uint64_t now = get_uptime_ms();
        if (now - last_refresh >= REFRESH_INTERVAL_MS) {
            refresh_procs();
            set_status("Auto-refreshed.");
            draw();
            last_refresh = now;
        }

        process_yield();
    }
}

void _start(void)
{
    procman_main();
    process_exit(0);
}
