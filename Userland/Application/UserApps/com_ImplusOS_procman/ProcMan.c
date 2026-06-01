#include "../../../API/File.h"
#include "../../../API/Serial.h"
#include "../../../API/Process.h"
#include "../../../API/Graphics.h"
#include "../../../API/Window.h"
#include "../../../API/Input.h"
#include "../../../API/Time.h"
#include "../../../API/SystemInfo.h"
#include "../../../../libc/include/string.h"
#include "../../../../libc/include/stdlib.h"
#include "../../../../libc/include/stdio.h"
#include <stdint.h>

#define MAX_PROCESSES       256
#define MAX_VISIBLE_ROWS    16
#define REFRESH_INTERVAL_MS 1000

#define MODE_PROCESSES   0
#define MODE_PERFORMANCE 1
#define MODE_HARDWARE    2

#define WIN_X 80
#define WIN_Y 60
#define WIN_W 620
#define WIN_H 520
#define WM_TITLE_HEIGHT 30

#define TAB_Y       10
#define TAB_H       30
#define TAB_W       140

#define HEADER_Y    60
#define ROW_START_Y 90
#define ROW_H       20

#define BTN_Y         410
#define BTN_H         24
#define BTN_W         80
#define BTN_NEW_X     10
#define BTN_KILL_X    100
#define BTN_REFRESH_X 190

#define COLOR_BG         0xFF121212
#define COLOR_TEXT       0xFFE0E0E0
#define COLOR_DIM        0xFF888888
#define COLOR_SEL_BG     0xFF1E3A38
#define COLOR_ACCENT     0xFF03DAC6
#define COLOR_DEAD       0xFF757575
#define COLOR_WARN       0xFFFFB86C
#define COLOR_GOOD       0xFF39C991
#define COLOR_GRAPH_BG   0xFF1A1A1A
#define COLOR_GRAPH_LINE 0xFF03DAC6
#define COLOR_BTN_BG     0xFF2A2A2A
#define COLOR_BTN_HOV    0xFF383838
#define COLOR_BTN_KILL   0xFF3A1E1E
#define COLOR_BTN_KILL_HOV 0xFF5A2A2A
#define COLOR_BTN_BORDER 0xFF444444

static const char *k_spawn_path =
    "/Userland/SystemApps/com_ImplusOS_shell/com_ImplusOS_shell.ELF";

#define CPU_HISTORY_SIZE 100

typedef struct {
    int32_t  pid;
    int32_t  parent_pid;
    char     name[64];
    uint8_t  state;
    uint64_t ticks;
    uint64_t mem;
    double   cpu_usage;
} proc_entry_t;

typedef struct {
    double cpu_values[CPU_HISTORY_SIZE];
    int    write_idx;
} cpu_history_t;

typedef struct { int x, y, w, h; } rect_t;

static window_id_t  g_win            = 0;
static proc_entry_t g_procs[MAX_PROCESSES];
static uint64_t     g_last_ticks[MAX_PROCESSES];
static uint8_t      g_tick_initialized[MAX_PROCESSES];
static int          g_proc_count     = 0;
static int          g_selected_idx   = 0;
static int          g_scroll_offset  = 0;
static uint64_t     g_total_mem      = 0;
static uint64_t     g_used_mem       = 0;
static uint64_t     g_last_update_ms = 0;
static char         g_status[128]    = {0};
static int          g_display_mode   = MODE_PROCESSES;
static cpu_history_t g_cpu_history   = {0};

static int g_mouse_x   = 0;
static int g_mouse_y   = 0;
static uint8_t g_mouse_btn      = 0;
static uint8_t g_prev_mouse_btn = 0;

static int g_hover_row = -1;
static int g_hover_tab = -1;
static int g_hover_btn = -1;

static inline bool rect_contains(rect_t r, int x, int y)
{
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

static bool mouse_btn_pressed(uint8_t btn_mask)
{
    return (g_mouse_btn & btn_mask) && !(g_prev_mouse_btn & btn_mask);
}

static void format_memory(uint64_t bytes, char *out, size_t out_size)
{
    if (bytes >= 1024ULL * 1024ULL * 1024ULL) {
        uint64_t gib  = bytes / (1024ULL * 1024ULL * 1024ULL);
        uint64_t frac = ((bytes % (1024ULL * 1024ULL * 1024ULL)) * 10ULL)
                        / (1024ULL * 1024ULL * 1024ULL);
        snprintf(out, out_size, "%llu.%llu GiB",
                 (unsigned long long)gib, (unsigned long long)frac);
    } else if (bytes >= 1024ULL * 1024ULL) {
        uint64_t mib  = bytes / (1024ULL * 1024ULL);
        uint64_t frac = ((bytes % (1024ULL * 1024ULL)) * 10ULL) / (1024ULL * 1024ULL);
        snprintf(out, out_size, "%llu.%llu MiB",
                 (unsigned long long)mib, (unsigned long long)frac);
    } else {
        snprintf(out, out_size, "%llu KB", (unsigned long long)(bytes / 1024ULL));
    }
}

static void format_double(double val, char *out, size_t out_size)
{
    int int_part  = (int)val;
    int frac_part = (int)((val - (double)int_part) * 10.0);
    if (frac_part < 0) frac_part = -frac_part;
    snprintf(out, out_size, "%d.%d", int_part, frac_part);
}

static const char *get_state_name(uint8_t state)
{
    switch (state) {
        case 1:  return "READY";
        case 2:  return "RUNNING";
        case 3:  return "DEAD";
        case 5:  return "ZOMBIE";
        default: return "UNKNOWN";
    }
}

static int compare_proc(const proc_entry_t *a, const proc_entry_t *b)
{
    if (a->cpu_usage > b->cpu_usage) return -1;
    if (a->cpu_usage < b->cpu_usage) return  1;
    if (a->mem > b->mem) return -1;
    if (a->mem < b->mem) return  1;
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

static void update_cpu_history(double total_cpu)
{
    g_cpu_history.cpu_values[g_cpu_history.write_idx] = total_cpu;
    g_cpu_history.write_idx = (g_cpu_history.write_idx + 1) % CPU_HISTORY_SIZE;
}

static void refresh_procs(void)
{
    int count = get_process_count();
    if (count < 0) count = 0;
    if (count > MAX_PROCESSES) count = MAX_PROCESSES;

    int      actual_count = 0;
    uint64_t current_ms   = get_uptime_ms();
    uint64_t delta_ms     = (g_last_update_ms == 0) ? 0
                            : current_ms - g_last_update_ms;
    double sum_cpu = 0.0;

    for (int i = 0; i < count; i++) {
        process_info_t info;
        if (get_process_info(i, &info) != 0 || info.state == 0) continue;
        if (actual_count >= MAX_PROCESSES) break;
        proc_entry_t *entry = &g_procs[actual_count];
        entry->pid        = info.pid;
        entry->parent_pid = info.parent_pid;
        strncpy(entry->name, info.name, sizeof(entry->name) - 1);
        entry->name[sizeof(entry->name) - 1] = '\0';
        entry->state = info.state;
        entry->ticks = info.total_ticks;
        entry->mem   = info.memory_usage;

        if (delta_ms > 0 && info.pid >= 0 && info.pid < MAX_PROCESSES
                && g_tick_initialized[info.pid]
                && info.total_ticks >= g_last_ticks[info.pid]) {
            uint64_t diff = info.total_ticks - g_last_ticks[info.pid];
            entry->cpu_usage = (double)diff * 100.0 / (double)delta_ms;
            if (entry->cpu_usage > 999.9) entry->cpu_usage = 999.9;
        } else {
            entry->cpu_usage = 0.0;
        }
        sum_cpu += entry->cpu_usage;

        if (info.pid >= 0 && info.pid < MAX_PROCESSES) {
            g_last_ticks[info.pid]       = info.total_ticks;
            g_tick_initialized[info.pid] = 1;
        }
        actual_count++;
    }

    g_proc_count     = actual_count;
    g_total_mem      = get_total_memory();
    g_used_mem       = get_used_memory();
    g_last_update_ms = current_ms;

    if (sum_cpu > 100.0) sum_cpu = 100.0;
    update_cpu_history(sum_cpu);
    sort_procs();

    if (g_proc_count == 0) {
        g_selected_idx  = 0;
        g_scroll_offset = 0;
    } else {
        if (g_selected_idx >= g_proc_count) g_selected_idx = g_proc_count - 1;
        if (g_selected_idx < 0)             g_selected_idx = 0;
        if (g_selected_idx < g_scroll_offset)
            g_scroll_offset = g_selected_idx;
        else if (g_selected_idx >= g_scroll_offset + MAX_VISIBLE_ROWS)
            g_scroll_offset = g_selected_idx - MAX_VISIBLE_ROWS + 1;
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

static void clamp_scroll(void)
{
    if (g_scroll_offset < 0) g_scroll_offset = 0;
    int max_scroll = g_proc_count - MAX_VISIBLE_ROWS;
    if (max_scroll < 0) max_scroll = 0;
    if (g_scroll_offset > max_scroll) g_scroll_offset = max_scroll;
}

static rect_t tab_rect(int i)
{
    return (rect_t){ 10 + i * TAB_W, TAB_Y, TAB_W - 20, TAB_H };
}

static rect_t row_rect(int row)
{
    return (rect_t){ 0, ROW_START_Y + row * ROW_H, WIN_W, ROW_H };
}

static rect_t btn_rect(int b)
{
    static const int xs[] = { BTN_NEW_X, BTN_KILL_X, BTN_REFRESH_X };
    return (rect_t){ xs[b], BTN_Y, BTN_W, BTN_H };
}

static void draw_button(int idx, const char *label,
                        uint32_t base_color, uint32_t hov_color)
{
    rect_t r  = btn_rect(idx);
    uint32_t bg = (g_hover_btn == idx) ? hov_color : base_color;
    draw_fill_rect(r.x, r.y, r.w, r.h, bg);
    draw_fill_rect(r.x, r.y,           r.w, 1,   COLOR_BTN_BORDER);
    draw_fill_rect(r.x, r.y + r.h - 1, r.w, 1,   COLOR_BTN_BORDER);
    draw_fill_rect(r.x, r.y,           1,   r.h,  COLOR_BTN_BORDER);
    draw_fill_rect(r.x + r.w - 1, r.y, 1,   r.h,  COLOR_BTN_BORDER);
    int tx = r.x + (r.w - (int)(strlen(label) * 7)) / 2;
    int ty = r.y + 5;
    window_draw_text(g_win, tx, ty, label, COLOR_TEXT, 13.0f);
}

static void draw_tabs(void)
{
    const char *tabs[] = { "[1] Processes", "[2] Performance", "[3] Hardware" };
    for (int i = 0; i < 3; i++) {
        uint32_t color = (g_display_mode == i) ? COLOR_ACCENT : COLOR_DIM;
        int tx = 10 + i * TAB_W + 10;
        window_draw_text(g_win, tx, TAB_Y + 8, tabs[i], color, 14.0f);
        if (g_display_mode == i)
            draw_fill_rect(10 + i * TAB_W, TAB_Y + TAB_H - 2,
                           TAB_W - 20, 2, COLOR_ACCENT);
    }
}

static void draw_cpu_graph(int x, int y, int width, int height)
{
    draw_fill_rect(x, y, width, height, COLOR_GRAPH_BG);
    for (int i = 1; i <= 4; i++) {
        int gy = y + (height * i) / 5;
        draw_fill_rect(x, gy, width, 1, 0xFF333333);
    }
    for (int i = 0; i < CPU_HISTORY_SIZE; i++) {
        int idx = (g_cpu_history.write_idx + i) % CPU_HISTORY_SIZE;
        double val = g_cpu_history.cpu_values[idx];
        int gx = x + (i * width) / (CPU_HISTORY_SIZE - 1);
        int gy = y + height - (int)((val / 100.0) * height);
        if (gy < y)          gy = y;
        if (gy >= y + height) gy = y + height - 1;
        draw_fill_rect(gx, gy,     3, 3,                    COLOR_GRAPH_LINE);
        draw_fill_rect(gx, gy + 3, 3, y + height - gy - 3,  0x4003DAC6);
    }
}

static void draw_processes(void)
{
    char buf[128];
    draw_fill_rect(0, HEADER_Y - 2, WIN_W, 1, 0xFF2A2A2A);

    window_draw_text(g_win,  20, HEADER_Y, "PID",   COLOR_ACCENT, 14.0f);
    window_draw_text(g_win,  80, HEADER_Y, "PPID",  COLOR_ACCENT, 14.0f);
    window_draw_text(g_win, 140, HEADER_Y, "Name",  COLOR_ACCENT, 14.0f);
    window_draw_text(g_win, 300, HEADER_Y, "State", COLOR_ACCENT, 14.0f);
    window_draw_text(g_win, 360, HEADER_Y, "CPU%",  COLOR_ACCENT, 14.0f);
    window_draw_text(g_win, 440, HEADER_Y, "Mem",   COLOR_ACCENT, 14.0f);

    for (int row = 0; row < MAX_VISIBLE_ROWS; row++) {
        int index = g_scroll_offset + row;
        if (index >= g_proc_count) break;

        proc_entry_t *entry   = &g_procs[index];
        bool          selected = (index == g_selected_idx);
        bool          hovered  = (g_hover_row == row);
        int           y        = ROW_START_Y + row * ROW_H;

        if (selected)
            draw_fill_rect(0, y, WIN_W, ROW_H, COLOR_SEL_BG);
        else if (hovered)
            draw_fill_rect(0, y, WIN_W, ROW_H, 0xFF1A1A1A);

        uint32_t tc = selected ? COLOR_ACCENT : COLOR_TEXT;

        window_draw_text(g_win, 5, y + 2,
            selected ? ">" : " ",
            selected ? COLOR_ACCENT : COLOR_DEAD, 14.0f);

        snprintf(buf, sizeof(buf), "%d", entry->pid);
        window_draw_text(g_win,  20, y + 2, buf, tc, 14.0f);

        snprintf(buf, sizeof(buf), "%d", entry->parent_pid);
        window_draw_text(g_win,  80, y + 2, buf, tc, 14.0f);

        window_draw_text(g_win, 140, y + 2, entry->name, tc, 14.0f);
        window_draw_text(g_win, 300, y + 2, get_state_name(entry->state), tc, 14.0f);

        char cpu_str[32];
        format_double(entry->cpu_usage, cpu_str, sizeof(cpu_str));
        snprintf(buf, sizeof(buf), "%s%%", cpu_str);
        window_draw_text(g_win, 360, y + 2, buf, tc, 14.0f);

        char mem_str[64];
        format_memory(entry->mem, mem_str, sizeof(mem_str));
        window_draw_text(g_win, 440, y + 2, mem_str, tc, 14.0f);
    }

    draw_button(0, "New",     COLOR_BTN_BG,   COLOR_BTN_HOV);
    draw_button(1, "Kill",    COLOR_BTN_KILL, COLOR_BTN_KILL_HOV);
    draw_button(2, "Refresh", COLOR_BTN_BG,   COLOR_BTN_HOV);
}

static void draw_performance(void)
{
    char buf[128];
    window_draw_text(g_win, 20, 60, "CPU Usage History", COLOR_ACCENT, 14.0f);
    draw_cpu_graph(20, 80, 560, 150);

    char used_str[64], total_str[64];
    format_memory(g_used_mem,  used_str,  sizeof(used_str));
    format_memory(g_total_mem, total_str, sizeof(total_str));

    snprintf(buf, sizeof(buf), "Memory Usage: %s / %s", used_str, total_str);
    window_draw_text(g_win, 20, 250, buf, COLOR_TEXT, 14.0f);

    draw_fill_rect(20, 280, 560, 20, COLOR_GRAPH_BG);
    int mem_bar_w = (g_total_mem > 0)
        ? (int)(((double)g_used_mem / (double)g_total_mem) * 560.0) : 0;
    if (mem_bar_w > 560) mem_bar_w = 560;
    draw_fill_rect(20, 280, mem_bar_w, 20, COLOR_GOOD);

    snprintf(buf, sizeof(buf), "Running Processes: %d / %d slots",
             g_proc_count, MAX_PROCESSES);
    window_draw_text(g_win, 20, 320, buf, COLOR_TEXT, 14.0f);
}

static void draw_hardware(void)
{
    char buf[128];
    window_draw_text(g_win, 20, 60, "System Hardware Information",
                     COLOR_ACCENT, 16.0f);

    system_cpu_info_t cpu_info;
    if (os_get_cpu_info(&cpu_info) == 0) {
        snprintf(buf, sizeof(buf), "CPU Vendor: %s", cpu_info.vendor);
        window_draw_text(g_win, 20,  90, buf, COLOR_TEXT, 14.0f);
        snprintf(buf, sizeof(buf), "CPU Brand: %s", cpu_info.brand);
        window_draw_text(g_win, 20, 110, buf, COLOR_TEXT, 14.0f);
        snprintf(buf, sizeof(buf), "Cores: %d Physical / %d Logical",
                 cpu_info.physical_cores, cpu_info.logical_cores);
        window_draw_text(g_win, 20, 130, buf, COLOR_TEXT, 14.0f);
    }

    system_graphics_info_t gfx_info;
    if (os_get_graphics_info(&gfx_info) == 0) {
        snprintf(buf, sizeof(buf), "Graphics: %s %s",
                 gfx_info.vendor, gfx_info.model);
        window_draw_text(g_win, 20, 160, buf, COLOR_TEXT, 14.0f);
        snprintf(buf, sizeof(buf), "Display: %dx%d @ %d bpp",
                 gfx_info.display_width, gfx_info.display_height,
                 gfx_info.bits_per_pixel);
        window_draw_text(g_win, 20, 180, buf, COLOR_TEXT, 14.0f);
    }

    uint32_t disk_count = 0;
    if (os_get_disk_count(&disk_count) == 0) {
        snprintf(buf, sizeof(buf), "Disks Detected: %d", disk_count);
        window_draw_text(g_win, 20, 210, buf, COLOR_TEXT, 14.0f);
        for (uint32_t i = 0; i < disk_count && i < 3; i++) {
            system_disk_info_t dinfo;
            if (os_get_disk_info(i, &dinfo) == 0) {
                char size_str[64];
                format_memory(dinfo.total_bytes, size_str, sizeof(size_str));
                snprintf(buf, sizeof(buf), " [%d] %s %s (%s)",
                         i, dinfo.manufacturer, dinfo.model, size_str);
                window_draw_text(g_win, 20, 230 + i * 20, buf, COLOR_DIM, 13.0f);
            }
        }
    }
}

static void draw(void)
{
    window_clear(g_win);
    draw_fill_rect(0, 0, WIN_W, WIN_H, COLOR_BG);
    draw_fill_rect(0, TAB_Y + TAB_H, WIN_W, 1, 0xFF2A2A2A);

    draw_tabs();

    if      (g_display_mode == MODE_PROCESSES)   draw_processes();
    else if (g_display_mode == MODE_PERFORMANCE) draw_performance();
    else if (g_display_mode == MODE_HARDWARE)    draw_hardware();

    char buf[128];
    if (g_display_mode == MODE_PROCESSES && g_proc_count > 0) {
        int start = g_scroll_offset + 1;
        int end   = g_scroll_offset + MAX_VISIBLE_ROWS;
        if (end > g_proc_count) end = g_proc_count;
        snprintf(buf, sizeof(buf), "Showing %d-%d of %d", start, end, g_proc_count);
        window_draw_text(g_win, 10, 440, buf, COLOR_TEXT, 12.0f);
    }

    if (g_status[0])
        window_draw_text(g_win, 10, 455, g_status, COLOR_WARN, 12.0f);

    if (g_display_mode == MODE_PROCESSES)
        window_draw_text(g_win, 10, 475,
            "[1-3] Tabs  [Up/Down] Select  [N] New  [K] Kill  [R] Refresh  [Q] Quit",
            COLOR_DEAD, 11.0f);
    else
        window_draw_text(g_win, 10, 475,
            "[1-3] Tabs  [R] Refresh  [Q] Quit",
            COLOR_DEAD, 11.0f);

    draw_present();
}

static void update_hover(void)
{
    int mx = g_mouse_x;
    int my = g_mouse_y;

    g_hover_tab = -1;
    g_hover_row = -1;
    g_hover_btn = -1;

    for (int i = 0; i < 3; i++) {
        if (rect_contains(tab_rect(i), mx, my)) {
            g_hover_tab = i;
            break;
        }
    }

    if (g_display_mode == MODE_PROCESSES) {
        for (int row = 0; row < MAX_VISIBLE_ROWS; row++) {
            int index = g_scroll_offset + row;
            if (index >= g_proc_count) break;
            if (rect_contains(row_rect(row), mx, my)) {
                g_hover_row = row;
                break;
            }
        }
        for (int b = 0; b < 3; b++) {
            if (rect_contains(btn_rect(b), mx, my)) {
                g_hover_btn = b;
                break;
            }
        }
    }
}

static void handle_mouse_event(void)
{
    int mx = g_mouse_x;
    int my = g_mouse_y;

    for (int i = 0; i < 3; i++) {
        if (rect_contains(tab_rect(i), mx, my)) {
            if (mouse_btn_pressed(INPUT_MOUSE_BTN_LEFT)) g_display_mode = i;
            return;
        }
    }

    if (g_display_mode != MODE_PROCESSES) return;

    for (int row = 0; row < MAX_VISIBLE_ROWS; row++) {
        int index = g_scroll_offset + row;
        if (index >= g_proc_count) break;
        if (rect_contains(row_rect(row), mx, my)) {
            if (mouse_btn_pressed(INPUT_MOUSE_BTN_LEFT)) {
                g_selected_idx = index;
            } else if (mouse_btn_pressed(INPUT_MOUSE_BTN_RIGHT)) {
                // Right-click: Show context menu or kill process directly
                int32_t target_pid = g_procs[index].pid;
                if (target_pid == process_get_current_pid())
                    set_status("Cannot kill this process from within procman.");
                else if (process_kill(target_pid) == 0)
                    set_status("Killed process successfully.");
                else
                    set_status("Failed to kill process.");
                refresh_procs();
            }
            return;
        }
    }

    if (rect_contains(btn_rect(0), mx, my)) {
        if (mouse_btn_pressed(INPUT_MOUSE_BTN_LEFT)) {
            int32_t child_pid = process_spawn(k_spawn_path);
            set_status(child_pid >= 0 ? "Spawned new shell process."
                                      : "Failed to start new process.");
            refresh_procs();
        }
        return;
    }

    if (rect_contains(btn_rect(1), mx, my)) {
        if (mouse_btn_pressed(INPUT_MOUSE_BTN_LEFT)) {
            if (g_selected_idx >= 0 && g_selected_idx < g_proc_count) {
                int32_t target_pid = g_procs[g_selected_idx].pid;
                if (target_pid == process_get_current_pid())
                    set_status("Cannot kill this process from within procman.");
                else if (process_kill(target_pid) == 0)
                    set_status("Killed process successfully.");
                else
                    set_status("Failed to kill process.");
                refresh_procs();
            }
        }
        return;
    }

    if (rect_contains(btn_rect(2), mx, my)) {
        if (mouse_btn_pressed(INPUT_MOUSE_BTN_LEFT)) {
            refresh_procs();
            set_status("Refreshed.");
        }
        return;
    }
}

static void handle_scroll(int8_t wheel)
{
    if (g_display_mode != MODE_PROCESSES) return;
    if (wheel < 0) g_scroll_offset += 3;
    else if (wheel > 0) g_scroll_offset -= 3;
    clamp_scroll();
}

void procman_main(void)
{
    g_win = window_create_ex(WIN_X, WIN_Y, WIN_W, WIN_H,
                             COLOR_BG, "Process Manager");
    if (g_win == 0) return;

    window_subscribe_keyboard(g_win);
    window_subscribe_mouse(g_win);
    graphics_init(g_win);

    refresh_procs();
    set_status("Ready");
    draw();

    uint64_t last_refresh = get_uptime_ms();

    while (1) {
        bool needs_draw = false;

        input_keyboard_event_t kev;
        if (window_input_keyboard_poll(&kev) > 0 && kev.pressed) {
            if (kev.ascii == '1') {
                g_display_mode = MODE_PROCESSES; needs_draw = true;
            } else if (kev.ascii == '2') {
                g_display_mode = MODE_PERFORMANCE; needs_draw = true;
            } else if (kev.ascii == '3') {
                g_display_mode = MODE_HARDWARE; needs_draw = true;
            } else if (kev.keycode == 0x48 && g_display_mode == MODE_PROCESSES) {
                if (g_selected_idx > 0) {
                    g_selected_idx--;
                    if (g_selected_idx < g_scroll_offset)
                        g_scroll_offset = g_selected_idx;
                }
                needs_draw = true;
            } else if (kev.keycode == 0x50 && g_display_mode == MODE_PROCESSES) {
                if (g_selected_idx < g_proc_count - 1) {
                    g_selected_idx++;
                    if (g_selected_idx >= g_scroll_offset + MAX_VISIBLE_ROWS)
                        g_scroll_offset = g_selected_idx - MAX_VISIBLE_ROWS + 1;
                }
                needs_draw = true;
            } else if (kev.ascii == 'r' || kev.ascii == 'R') {
                refresh_procs();
                set_status("Refreshed.");
                needs_draw = true;
            } else if ((kev.ascii == 'n' || kev.ascii == 'N')
                       && g_display_mode == MODE_PROCESSES) {
                int32_t child_pid = process_spawn(k_spawn_path);
                set_status(child_pid >= 0 ? "Spawned new shell process."
                                          : "Failed to start new process.");
                refresh_procs();
                needs_draw = true;
            } else if ((kev.ascii == 'k' || kev.ascii == 'K')
                       && g_display_mode == MODE_PROCESSES) {
                if (g_selected_idx >= 0 && g_selected_idx < g_proc_count) {
                    int32_t target_pid = g_procs[g_selected_idx].pid;
                    if (target_pid == process_get_current_pid())
                        set_status("Cannot kill this process from within procman.");
                    else if (process_kill(target_pid) == 0)
                        set_status("Killed process successfully.");
                    else
                        set_status("Failed to kill process.");
                    refresh_procs();
                }
                needs_draw = true;
            } else if (kev.ascii == 'q' || kev.ascii == 'Q') {
                break;
            }
        }

        input_mouse_event_t mev;
        if (window_input_mouse_poll(&mev) > 0) {
            g_prev_mouse_btn = g_mouse_btn;
            g_mouse_btn      = mev.buttons;
            
            g_mouse_x = (int)(int16_t)mev.x;
            g_mouse_y = (int)(int16_t)mev.y;

            if (g_mouse_x < 0) g_mouse_x = 0;
            if (g_mouse_x >= WIN_W) g_mouse_x = WIN_W - 1;
            if (g_mouse_y < 0) g_mouse_y = 0;
            if (g_mouse_y >= WIN_H) g_mouse_y = WIN_H - 1;

            update_hover();

            if (mouse_btn_pressed(INPUT_MOUSE_BTN_LEFT) || mouse_btn_pressed(INPUT_MOUSE_BTN_RIGHT))
                handle_mouse_event();

            if (mev.wheel != 0)
                handle_scroll(mev.wheel);

            needs_draw = true;
        }

        uint64_t now = get_uptime_ms();
        if (now - last_refresh >= REFRESH_INTERVAL_MS) {
            refresh_procs();
            set_status("Auto-refreshed.");
            last_refresh = now;
            needs_draw   = true;
        }

        if (needs_draw) draw();

        process_yield();
    }
}

void _start(void)
{
    procman_main();
    process_exit(0);
}