#include "File.h"
#include "Serial.h"
#include "Process.h"
#include "Graphics.h"
#include "Window.h"
#include "Input.h"
#include "../../../../libc/include/string.h"
#include "../../../../libc/include/stdlib.h"
#include "../../../../libc/include/stdio.h"

#define IED_MAX_LINES    512
#define IED_MAX_LINE_LEN 256
#define IED_MAX_PATH     512

static window_id_t g_win = 0;
static int g_win_w = 640;
static int g_win_h = 400;
static int g_char_w = 8;
static int g_char_h = 16;

static char g_lines[IED_MAX_LINES][IED_MAX_LINE_LEN];
static int  g_line_count = 1;
static int  g_cursor_row = 0;
static int  g_cursor_col = 0;
static int  g_scroll_top = 0;
static int  g_visible_rows = 0;
static char g_filepath[IED_MAX_PATH] = {0};
static int  g_modified = 0;
static char g_status[128] = "Ctrl+S saves to /Userland/editor.txt. Ctrl+L reloads it. Ctrl+Q quits.";

static void editor_set_status(const char *status)
{
    strncpy(g_status, status ? status : "", sizeof(g_status) - 1);
    g_status[sizeof(g_status) - 1] = '\0';
}

static void editor_render(void)
{
    window_clear(g_win);
    draw_fill_rect(0, 0, (uint32_t)g_win_w, (uint32_t)g_win_h, 0xFF1E1E2E);
    draw_fill_rect(0, 0, 40, (uint32_t)(g_win_h - g_char_h - 4), 0xFF181825);
    draw_fill_rect(40, 0, 1, (uint32_t)(g_win_h - g_char_h - 4), 0xFF313244);

    draw_fill_rect(0, (uint32_t)(g_win_h - g_char_h - 4), (uint32_t)g_win_w, (uint32_t)(g_char_h + 4), 0xFF313244);

    char status[128];
    int spos = 0;
    const char *fname = g_filepath[0] ? g_filepath : "[new]";
    while (*fname && spos < 60) { status[spos++] = *fname++; }
    if (g_modified) { status[spos++] = '*'; }
    status[spos++] = ' ';
    status[spos++] = 'L';
    char num[16]; int nlen = 0;
    int32_t v = g_cursor_row + 1;
    if (v == 0) { num[nlen++] = '0'; }
    else { char tmp[16]; int ti = 0; while (v > 0) { tmp[ti++] = (char)('0' + v % 10); v /= 10; } for (int i = ti - 1; i >= 0; i--) num[nlen++] = tmp[i]; }
    for (int i = 0; i < nlen; i++) status[spos++] = num[i];
    status[spos++] = ':';
    v = g_cursor_col + 1; nlen = 0;
    if (v == 0) { num[nlen++] = '0'; }
    else { char tmp[16]; int ti = 0; while (v > 0) { tmp[ti++] = (char)('0' + v % 10); v /= 10; } for (int i = ti - 1; i >= 0; i--) num[nlen++] = tmp[i]; }
    for (int i = 0; i < nlen; i++) status[spos++] = num[i];
    status[spos] = '\0';

    window_draw_text(g_win, 4, (uint32_t)(g_win_h - g_char_h - 2), status, 0xFFBAC2DE, 14.0f);
    window_draw_text(g_win, 220, (uint32_t)(g_win_h - g_char_h - 2), g_status, 0xFF89B4FA, 12.0f);

    g_visible_rows = (g_win_h - g_char_h - 4) / g_char_h;
    for (int row = 0; row < g_visible_rows; row++) {
        int line_idx = g_scroll_top + row;
        if (line_idx >= g_line_count) break;

        char lnum[8];
        int li = 0;
        v = line_idx + 1;
        { char tmp[8]; int ti = 0; while (v > 0) { tmp[ti++] = (char)('0' + v % 10); v /= 10; } while (ti < 4) tmp[ti++] = ' '; for (int i = ti - 1; i >= 0; i--) lnum[li++] = tmp[i]; }
        lnum[li] = '\0';
        window_draw_text(g_win, 2, (uint32_t)(row * g_char_h + 2), lnum, 0xFF6C7086, 14.0f);

        int gutter_w = 5 * g_char_w;
        if (g_lines[line_idx][0] != '\0') {
            window_draw_text(g_win, (uint32_t)gutter_w, (uint32_t)(row * g_char_h + 2),
                             g_lines[line_idx], 0xFFCDD6F4, 14.0f);
        }

        if (line_idx == g_cursor_row) {
            int cx = gutter_w + g_cursor_col * g_char_w;
            int cy = row * g_char_h + 2;
            draw_fill_rect((uint32_t)cx, (uint32_t)cy, 2, (uint32_t)g_char_h, 0xFFF5C2E7);
        }
    }

    draw_present();
}

static void editor_load(const char *path)
{
    int32_t fd = file_open(path, 0);
    if (fd < 0) {
        editor_set_status("Could not open file.");
        return;
    }

    g_line_count = 1;
    memset(g_lines, 0, sizeof(g_lines));

    char buf[512];
    int64_t n;
    int line = 0, col = 0;
    while ((n = file_read(fd, buf, sizeof(buf) - 1)) > 0) {
        for (int64_t i = 0; i < n; i++) {
            if (buf[i] == '\n') {
                g_lines[line][col] = '\0';
                line++;
                col = 0;
                if (line >= IED_MAX_LINES - 1) goto done;
            } else if (buf[i] != '\r') {
                if (col < IED_MAX_LINE_LEN - 1) {
                    g_lines[line][col++] = buf[i];
                }
            }
        }
    }
done:
    g_lines[line][col] = '\0';
    g_line_count = line + 1;
    file_close(fd);
    strncpy(g_filepath, path, IED_MAX_PATH - 1);
    g_filepath[IED_MAX_PATH - 1] = '\0';
    g_cursor_row = 0;
    g_cursor_col = 0;
    g_scroll_top = 0;
    g_modified = 0;
    editor_set_status("Loaded file.");
}

static void editor_save(void)
{
    if (g_filepath[0] == '\0') {
        strncpy(g_filepath, "/Userland/editor.txt", IED_MAX_PATH - 1);
        g_filepath[IED_MAX_PATH - 1] = '\0';
    }

    int32_t fd = file_creat(g_filepath);
    if (fd < 0) {
        editor_set_status("Save failed.");
        return;
    }

    for (int i = 0; i < g_line_count; i++) {
        int len = (int)strlen(g_lines[i]);
        if (len > 0) {
            file_write(fd, g_lines[i], (uint64_t)len);
        }
        if (i < g_line_count - 1) {
            file_write(fd, "\n", 1);
        }
    }
    file_close(fd);
    g_modified = 0;
    editor_set_status("Saved.");
}

static void editor_insert_char(char c)
{
    int len = (int)strlen(g_lines[g_cursor_row]);
    if (len >= IED_MAX_LINE_LEN - 1) return;

    memmove(&g_lines[g_cursor_row][g_cursor_col + 1],
            &g_lines[g_cursor_row][g_cursor_col],
            (size_t)(len - g_cursor_col + 1));
    g_lines[g_cursor_row][g_cursor_col] = c;
    g_cursor_col++;
    g_modified = 1;
}

static void editor_newline(void)
{
    if (g_line_count >= IED_MAX_LINES - 1) return;

    for (int i = g_line_count; i > g_cursor_row + 1; i--) {
        memcpy(g_lines[i], g_lines[i - 1], IED_MAX_LINE_LEN);
    }

    int len = (int)strlen(g_lines[g_cursor_row]);
    strncpy(g_lines[g_cursor_row + 1], &g_lines[g_cursor_row][g_cursor_col],
            (size_t)(IED_MAX_LINE_LEN - 1));
    g_lines[g_cursor_row][g_cursor_col] = '\0';

    (void)len;
    g_line_count++;
    g_cursor_row++;
    g_cursor_col = 0;
    g_modified = 1;
}

static void editor_backspace(void)
{
    if (g_cursor_col > 0) {
        int len = (int)strlen(g_lines[g_cursor_row]);
        memmove(&g_lines[g_cursor_row][g_cursor_col - 1],
                &g_lines[g_cursor_row][g_cursor_col],
                (size_t)(len - g_cursor_col + 1));
        g_cursor_col--;
        g_modified = 1;
    } else if (g_cursor_row > 0) {
        int prev_len = (int)strlen(g_lines[g_cursor_row - 1]);
        g_cursor_col = prev_len;
        strncat(g_lines[g_cursor_row - 1], g_lines[g_cursor_row],
                (size_t)(IED_MAX_LINE_LEN - prev_len - 1));

        for (int i = g_cursor_row; i < g_line_count - 1; i++) {
            memcpy(g_lines[i], g_lines[i + 1], IED_MAX_LINE_LEN);
        }
        memset(g_lines[g_line_count - 1], 0, IED_MAX_LINE_LEN);
        g_line_count--;
        g_cursor_row--;
        g_modified = 1;
    }
}

static void editor_ensure_visible(void)
{
    if (g_cursor_row < g_scroll_top) {
        g_scroll_top = g_cursor_row;
    }
    if (g_cursor_row >= g_scroll_top + g_visible_rows) {
        g_scroll_top = g_cursor_row - g_visible_rows + 1;
    }
}

void _start(void)
{
    g_win = window_create_ex(100, 80, 640, 400, 0xFF1E1E2E, "ImplusOS Editor");
    if (g_win == 0) {
        while (1) process_yield();
    }

    window_subscribe_keyboard(g_win);
    graphics_init(g_win);

    uint32_t wx, wy, ww, wh;
    if (window_get_rect(g_win, &wx, &wy, &ww, &wh) == 0) {
        g_win_w = (int)ww;
        g_win_h = (int)wh;
    }
    g_visible_rows = (g_win_h - g_char_h - 4) / g_char_h;

    memset(g_lines, 0, sizeof(g_lines));
    g_line_count = 1;
    editor_load("/Userland/editor.txt");

    editor_render();

    while (1) {
        input_keyboard_event_t kbd;
        if (window_input_keyboard_poll(&kbd) > 0) {
            if (!kbd.pressed) {
                continue;
            }

            char c = (char)kbd.ascii;
            uint8_t mod = kbd.modifiers;

            if ((mod & INPUT_KBD_MOD_CTRL) && (c == 's' || c == 'S' || kbd.keycode == 0x1F)) {
                editor_save();
                editor_render();
                continue;
            }

            if ((mod & INPUT_KBD_MOD_CTRL) && (c == 'l' || c == 'L' || kbd.keycode == 0x26)) {
                editor_load(g_filepath[0] ? g_filepath : "/Userland/editor.txt");
                editor_render();
                continue;
            }

            if ((mod & INPUT_KBD_MOD_CTRL) && (c == 'q' || c == 'Q' || kbd.keycode == 0x10)) {
                process_exit(0);
            }

            if (c == '\n' || kbd.keycode == 0x1C) {
                editor_newline();
            } else if (c == '\b' || kbd.keycode == 0x0E) {
                editor_backspace();
            } else if (kbd.keycode == 0x48) {
                if (g_cursor_row > 0) {
                    g_cursor_row--;
                    int len = (int)strlen(g_lines[g_cursor_row]);
                    if (g_cursor_col > len) g_cursor_col = len;
                }
            } else if (kbd.keycode == 0x50) {
                if (g_cursor_row < g_line_count - 1) {
                    g_cursor_row++;
                    int len = (int)strlen(g_lines[g_cursor_row]);
                    if (g_cursor_col > len) g_cursor_col = len;
                }
            } else if (kbd.keycode == 0x4B) {
                if (g_cursor_col > 0) g_cursor_col--;
            } else if (kbd.keycode == 0x4D) {
                int len = (int)strlen(g_lines[g_cursor_row]);
                if (g_cursor_col < len) g_cursor_col++;
            } else if (c == '\t') {
                for (int i = 0; i < 4; i++) editor_insert_char(' ');
            } else if (c >= 0x20 && c <= 0x7E) {
                editor_insert_char(c);
            }

            editor_ensure_visible();
            editor_render();
        }
        process_yield();
    }
}
