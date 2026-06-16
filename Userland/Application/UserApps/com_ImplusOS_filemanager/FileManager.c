#include "../../../API/File.h"
#include "../../../API/Serial.h"
#include "../../../API/Process.h"
#include "../../../API/Graphics.h"
#include "../../../API/Window.h"
#include "../../../API/Input.h"
#include "../../../../libc/I_libc/include/string.h"
#include "../../../../libc/I_libc/include/stdlib.h"
#include "../../../../libc/I_libc/include/stdio.h"

#define FM_MAX_ENTRIES   128
#define FM_MAX_NAME_LEN  64
#define FM_MAX_PATH      512
#define FM_STATUS_LEN    128
#define FM_ENTRY_HEIGHT  24
#define FM_ASSOC_FILE    "/Userland/file-associations.conf"
#define FM_EDITOR_PATH   "/Userland/UserApps/com_ImplusOS_editor/com_ImplusOS_editor.ELF"

typedef struct {
    char    name[FM_MAX_NAME_LEN];
    uint8_t is_dir;
    uint32_t size;
} fm_entry_t;

static window_id_t g_win = 0;
static int g_win_w = 560;
static int g_win_h = 380;

static char g_cwd[FM_MAX_PATH] = "/";
static char g_status[FM_STATUS_LEN] = "Choose a folder and press Enter.";
static fm_entry_t g_entries[FM_MAX_ENTRIES];
static int  g_entry_count = 0;
static int  g_selected = 0;
static int  g_scroll_top = 0;
static int  g_visible_rows = 0;
static int  g_refresh_failed = 0;
static int  g_rename_active = 0;
static char g_rename_buffer[FM_MAX_NAME_LEN];
static int  g_rename_length = 0;

static void fm_refresh(void);

#define COLOR_BG        0xFF0F1720
#define COLOR_HEADER    0xFF162330
#define COLOR_PATH_BG   0xFF0C161F
#define COLOR_TABLE     0xFF1B2C3A
#define COLOR_TEXT      0xFFE7F4F7
#define COLOR_DIM       0xFF9AAFB8
#define COLOR_SEL       0xFF223F53
#define COLOR_SEL_BAR   0xFF47B6D6
#define COLOR_DIR_ICON  0xFF55B9D9
#define COLOR_FILE_ICON 0xFF9DD3A8
#define COLOR_ACCENT    0xFF47B6D6
#define COLOR_WARN      0xFFF2B56B

static void fm_set_status(const char *status)
{
    if (!status) {
        g_status[0] = '\0';
        return;
    }
    strncpy(g_status, status, FM_STATUS_LEN - 1);
    g_status[FM_STATUS_LEN - 1] = '\0';
}

static char fm_ascii_lower(char c)
{
    if (c >= 'A' && c <= 'Z') {
        return (char)(c + ('a' - 'A'));
    }
    return c;
}

static int fm_name_compare(const char *a, const char *b)
{
    while (*a && *b) {
        char ca = fm_ascii_lower(*a);
        char cb = fm_ascii_lower(*b);
        if (ca != cb) return (int)(unsigned char)ca - (int)(unsigned char)cb;
        ++a;
        ++b;
    }
    return (int)(unsigned char)fm_ascii_lower(*a) - (int)(unsigned char)fm_ascii_lower(*b);
}

static int fm_entry_compare(const void *left, const void *right)
{
    const fm_entry_t *a = (const fm_entry_t *)left;
    const fm_entry_t *b = (const fm_entry_t *)right;
    if (a->is_dir != b->is_dir) {
        return a->is_dir ? -1 : 1;
    }
    return fm_name_compare(a->name, b->name);
}

static void fm_sort_entries(int start_index)
{
    if (start_index < 0) start_index = 0;
    if (start_index >= g_entry_count - 1) return;
    size_t count = (size_t)(g_entry_count - start_index);
    qsort(&g_entries[start_index], count, sizeof(g_entries[0]),
          fm_entry_compare);
}

static void fm_join_path(const char *base, const char *name, char *out, int out_len)
{
    if (!out || out_len <= 0) return;
    if (!base || !name) {
        out[0] = '\0';
        return;
    }
    if (strcmp(base, "/") == 0) {
        snprintf(out, (size_t)out_len, "/%s", name);
    } else {
        snprintf(out, (size_t)out_len, "%s/%s", base, name);
    }
}

static int fm_copy_file(const char *source_path, const char *destination_path)
{
    int32_t source = file_open(source_path, 0);
    if (source < 0) return -1;
    int32_t destination = file_creat(destination_path);
    if (destination < 0) {
        file_close(source);
        return -1;
    }
    char buffer[4096];
    int result = 0;
    for (;;) {
        int64_t count = file_read(source, buffer, sizeof(buffer));
        if (count < 0) {
            result = -1;
            break;
        }
        if (count == 0) break;
        int64_t written = 0;
        while (written < count) {
            int64_t chunk = file_write(destination, buffer + written,
                                       (uint64_t)(count - written));
            if (chunk <= 0) {
                result = -1;
                break;
            }
            written += chunk;
        }
        if (result < 0) break;
    }
    file_close(source);
    file_close(destination);
    if (result < 0) file_unlink(destination_path);
    return result;
}

static const char *fm_extension(const char *name)
{
    const char *dot = strrchr(name, '.');
    return dot && dot[1] ? dot + 1 : "";
}

static int fm_find_association(const char *name, char *path, size_t path_size)
{
    const char *extension = fm_extension(name);
    int32_t fd = file_open(FM_ASSOC_FILE, 0);
    if (fd >= 0) {
        char buffer[16385];
        int64_t count = file_read(fd, buffer, sizeof(buffer) - 1u);
        file_close(fd);
        if (count > 0) {
            buffer[count] = '\0';
            char *line = buffer;
            while (*line) {
                char *end = strchr(line, '\n');
                if (end) *end = '\0';
                char *separator = strchr(line, '=');
                if (separator) {
                    *separator = '\0';
                    if (strcasecmp(line, extension) == 0 &&
                        separator[1] == '/') {
                        strlcpy(path, separator + 1, path_size);
                        return path[0] ? 0 : -1;
                    }
                }
                if (!end) break;
                line = end + 1;
            }
        }
    }
    if (strcasecmp(extension, "txt") == 0 ||
        strcasecmp(extension, "md") == 0 ||
        strcasecmp(extension, "log") == 0 ||
        strcasecmp(extension, "conf") == 0 ||
        strcasecmp(extension, "c") == 0 ||
        strcasecmp(extension, "h") == 0) {
        strlcpy(path, FM_EDITOR_PATH, path_size);
        return 0;
    }
    return -1;
}

static void fm_open_selected_file(const fm_entry_t *entry)
{
    char full_path[FM_MAX_PATH];
    char app_path[FM_MAX_PATH];
    fm_join_path(g_cwd, entry->name, full_path, FM_MAX_PATH);
    if (fm_find_association(entry->name, app_path, sizeof(app_path)) < 0) {
        fm_set_status("No application is associated with this file type.");
        return;
    }
    int32_t pid = process_spawn_with_arg(app_path, full_path);
    if (pid < 0) {
        fm_set_status("The associated application could not be started.");
        return;
    }
    char status[FM_STATUS_LEN];
    snprintf(status, sizeof(status), "Opened %s", entry->name);
    fm_set_status(status);
}

static void fm_copy_selected(const fm_entry_t *entry)
{
    if (!entry || entry->is_dir) {
        fm_set_status("Folder copying is not supported.");
        return;
    }
    char source[FM_MAX_PATH];
    char destination[FM_MAX_PATH];
    char copy_name[FM_MAX_NAME_LEN];
    fm_join_path(g_cwd, entry->name, source, FM_MAX_PATH);
    for (unsigned suffix = 1u; suffix < 1000u; ++suffix) {
        snprintf(copy_name, sizeof(copy_name), "%s.copy%u", entry->name,
                 suffix);
        fm_join_path(g_cwd, copy_name, destination, FM_MAX_PATH);
        file_stat_t stat;
        if (file_stat(destination, &stat) < 0 || !stat.exists) break;
    }
    if (fm_copy_file(source, destination) < 0) {
        fm_set_status("Copy failed.");
        return;
    }
    fm_refresh();
    fm_set_status("File copied.");
}

static void fm_delete_selected(const fm_entry_t *entry)
{
    if (!entry || entry->is_dir) {
        fm_set_status("Only regular files can be deleted.");
        return;
    }
    char path[FM_MAX_PATH];
    fm_join_path(g_cwd, entry->name, path, FM_MAX_PATH);
    if (file_unlink(path) < 0) {
        fm_set_status("Delete failed.");
        return;
    }
    fm_refresh();
    fm_set_status("File deleted.");
}

static void fm_begin_rename(const fm_entry_t *entry)
{
    if (!entry || strcmp(entry->name, "..") == 0) return;
    strlcpy(g_rename_buffer, entry->name, sizeof(g_rename_buffer));
    g_rename_length = (int)strlen(g_rename_buffer);
    g_rename_active = 1;
}

static void fm_finish_rename(void)
{
    if (g_selected < 0 || g_selected >= g_entry_count ||
        g_rename_length == 0 || strchr(g_rename_buffer, '/')) {
        g_rename_active = 0;
        fm_set_status("Invalid file name.");
        return;
    }
    char old_path[FM_MAX_PATH];
    char new_path[FM_MAX_PATH];
    fm_join_path(g_cwd, g_entries[g_selected].name, old_path, FM_MAX_PATH);
    fm_join_path(g_cwd, g_rename_buffer, new_path, FM_MAX_PATH);
    g_rename_active = 0;
    if (file_rename(old_path, new_path) < 0) {
        fm_set_status("Rename failed.");
        return;
    }
    fm_refresh();
    fm_set_status("Item renamed.");
}

static void fm_refresh(void)
{
    g_entry_count = 0;
    g_selected = 0;
    g_scroll_top = 0;
    g_refresh_failed = 0;

    int32_t dh = file_opendir(g_cwd);
    if (dh < 0 && strcmp(g_cwd, "/") != 0) {
        strncpy(g_cwd, "/", FM_MAX_PATH - 1);
        g_cwd[FM_MAX_PATH - 1] = '\0';
        dh = file_opendir(g_cwd);
        if (dh >= 0) {
            fm_set_status("The previous folder was unavailable, so the view returned to /.");
        }
    }
    if (dh < 0) {
        g_refresh_failed = 1;
        fm_set_status("This folder could not be opened.");
        return;
    }

    if (strlen(g_cwd) > 1 && g_entry_count < FM_MAX_ENTRIES) {
        strncpy(g_entries[g_entry_count].name, "..", FM_MAX_NAME_LEN - 1);
        g_entries[g_entry_count].name[FM_MAX_NAME_LEN - 1] = '\0';
        g_entries[g_entry_count].is_dir = 1;
        g_entries[g_entry_count].size = 0;
        g_entry_count++;
    }

    file_dirent_t de;
    while (file_readdir(dh, &de) == 0 && g_entry_count < FM_MAX_ENTRIES) {
        if (de.name[0] == '\0' || de.name[0] == '.') continue;
        strncpy(g_entries[g_entry_count].name, de.name, FM_MAX_NAME_LEN - 1);
        g_entries[g_entry_count].name[FM_MAX_NAME_LEN - 1] = '\0';
        g_entries[g_entry_count].is_dir = (de.attributes & 0x10u) ? 1 : 0;
        g_entries[g_entry_count].size = de.size;
        g_entry_count++;
    }
    file_closedir(dh);

    fm_sort_entries((strlen(g_cwd) > 1) ? 1 : 0);

    {
        int item_count = g_entry_count - ((strlen(g_cwd) > 1) ? 1 : 0);
        char status[FM_STATUS_LEN];
        if (item_count <= 0) {
            snprintf(status, sizeof(status), "%s is empty.", g_cwd);
        } else {
            snprintf(status, sizeof(status), "%d item%s in %s", item_count, item_count == 1 ? "" : "s", g_cwd);
        }
        fm_set_status(status);
    }
}

static void fm_enter_dir(const char *name)
{
    char next_path[FM_MAX_PATH];

    if (strcmp(name, "..") == 0) {
        int len = (int)strlen(g_cwd);
        if (len <= 1) {
            fm_set_status("Already at the root folder.");
            return;
        }
        strncpy(next_path, g_cwd, FM_MAX_PATH - 1);
        next_path[FM_MAX_PATH - 1] = '\0';
        if (next_path[len - 1] == '/') next_path[--len] = '\0';
        char *last = next_path;
        for (char *p = next_path; *p; ++p) {
            if (*p == '/') last = p;
        }
        if (last == next_path) next_path[1] = '\0';
        else *last = '\0';
    } else {
        fm_join_path(g_cwd, name, next_path, FM_MAX_PATH);
    }

    {
        file_stat_t st;
        if (file_stat(next_path, &st) < 0 || !st.exists || !st.is_dir) {
            char status[FM_STATUS_LEN];
            snprintf(status, sizeof(status), "Folder not available: %s", next_path);
            fm_set_status(status);
            return;
        }
    }

    strncpy(g_cwd, next_path, FM_MAX_PATH - 1);
    g_cwd[FM_MAX_PATH - 1] = '\0';
    fm_refresh();
}

static void fm_format_size(uint32_t size, char *buf, int buf_len)
{
    if (!buf || buf_len <= 0) return;
    if (size < 1024) {
        snprintf(buf, (size_t)buf_len, "%u B", size);
    } else if (size < 1024 * 1024) {
        snprintf(buf, (size_t)buf_len, "%u KB", size / 1024);
    } else {
        snprintf(buf, (size_t)buf_len, "%u MB", size / (1024 * 1024));
    }
}

static void fm_render(void)
{
    const int header_h = 56;
    const int table_h = 24;
    const int footer_h = 42;
    const int list_y_start = header_h + table_h + 6;
    const int kind_x = g_win_w - 150;
    const int size_x = g_win_w - 78;

    window_clear(g_win);
    draw_fill_rect(0, 0, (uint32_t)g_win_w, (uint32_t)g_win_h, COLOR_BG);
    draw_fill_rect(0, 0, (uint32_t)g_win_w, (uint32_t)header_h, COLOR_HEADER);
    draw_fill_rect(12, 26, (uint32_t)(g_win_w - 24), 20, COLOR_PATH_BG);
    draw_fill_rect(0, (uint32_t)header_h, (uint32_t)g_win_w, (uint32_t)table_h, COLOR_TABLE);
    draw_fill_rect(0, (uint32_t)(g_win_h - footer_h), (uint32_t)g_win_w, (uint32_t)footer_h, COLOR_HEADER);

    window_draw_text(g_win, 16, 8, "File Manager", COLOR_TEXT, 14.0f);
    window_draw_text(g_win, 18, 29, g_cwd, COLOR_ACCENT, 12.0f);

    {
        char meta[48];
        int item_count = g_entry_count - ((strlen(g_cwd) > 1) ? 1 : 0);
        if (g_refresh_failed) {
            snprintf(meta, sizeof(meta), "Unavailable");
        } else {
            snprintf(meta, sizeof(meta), "%d item%s", item_count, item_count == 1 ? "" : "s");
        }
        window_draw_text(g_win, (uint32_t)(g_win_w - 96), 8, meta, g_refresh_failed ? COLOR_WARN : COLOR_DIM, 12.0f);
    }

    window_draw_text(g_win, 16, (uint32_t)(header_h + 5), "Name", COLOR_DIM, 12.0f);
    window_draw_text(g_win, (uint32_t)kind_x, (uint32_t)(header_h + 5), "Type", COLOR_DIM, 12.0f);
    window_draw_text(g_win, (uint32_t)size_x, (uint32_t)(header_h + 5), "Size", COLOR_DIM, 12.0f);

    g_visible_rows = (g_win_h - list_y_start - footer_h - 4) / FM_ENTRY_HEIGHT;
    if (g_visible_rows < 1) g_visible_rows = 1;

    if (g_entry_count == 0) {
        draw_fill_rect(16, (uint32_t)list_y_start, (uint32_t)(g_win_w - 32), 48, COLOR_PATH_BG);
        window_draw_text(g_win, 24, (uint32_t)(list_y_start + 16), g_status, g_refresh_failed ? COLOR_WARN : COLOR_DIM, 13.0f);
    }
    for (int i = 0; i < g_visible_rows; i++) {
        int idx = g_scroll_top + i;
        if (idx >= g_entry_count) break;

        int y = list_y_start + i * FM_ENTRY_HEIGHT;
        fm_entry_t *e = &g_entries[idx];

        if (idx == g_selected) {
            draw_fill_rect(0, (uint32_t)y, (uint32_t)g_win_w, (uint32_t)FM_ENTRY_HEIGHT, COLOR_SEL);
            draw_fill_rect(0, (uint32_t)y, 4, (uint32_t)FM_ENTRY_HEIGHT, COLOR_SEL_BAR);
        }

        if (e->is_dir) {
            draw_fill_rect(16, (uint32_t)(y + 6), 14, 10, COLOR_DIR_ICON);
            draw_fill_rect(16, (uint32_t)(y + 4), 8, 4, COLOR_DIR_ICON);
        } else {
            draw_fill_rect(18, (uint32_t)(y + 4), 10, 14, COLOR_FILE_ICON);
            draw_fill_rect(20, (uint32_t)(y + 7), 6, 1, COLOR_BG);
        }

        {
            uint32_t text_color = (idx == g_selected) ? COLOR_TEXT : (e->is_dir ? COLOR_DIR_ICON : COLOR_TEXT);
            window_draw_text(g_win, 40, (uint32_t)(y + 4), e->name, text_color, 13.0f);
        }

        if (e->is_dir) {
            window_draw_text(g_win, (uint32_t)kind_x, (uint32_t)(y + 4), strcmp(e->name, "..") == 0 ? "UP" : "DIR", COLOR_DIM, 12.0f);
            window_draw_text(g_win, (uint32_t)size_x, (uint32_t)(y + 4), "--", COLOR_DIM, 12.0f);
        } else {
            char size_str[16];
            fm_format_size(e->size, size_str, (int)sizeof(size_str));
            window_draw_text(g_win, (uint32_t)kind_x, (uint32_t)(y + 4), "FILE", COLOR_DIM, 12.0f);
            window_draw_text(g_win, (uint32_t)size_x, (uint32_t)(y + 4), size_str, COLOR_DIM, 12.0f);
        }
    }

    if (g_entry_count > g_visible_rows) {
        uint32_t sb_h = (uint32_t)(g_win_h - list_y_start - footer_h);
        uint32_t thumb_h = (sb_h * (uint32_t)g_visible_rows) / (uint32_t)g_entry_count;
        if (thumb_h < 10) thumb_h = 10;
        uint32_t thumb_y = (uint32_t)list_y_start + (sb_h * (uint32_t)g_scroll_top) / (uint32_t)g_entry_count;
        draw_fill_rect((uint32_t)(g_win_w - 4), (uint32_t)list_y_start, 4, sb_h, COLOR_PATH_BG);
        draw_fill_rect((uint32_t)(g_win_w - 4), thumb_y, 4, thumb_h, COLOR_ACCENT);
    }

    if (g_rename_active) {
        char prompt[FM_STATUS_LEN];
        snprintf(prompt, sizeof(prompt), "Rename to: %s", g_rename_buffer);
        window_draw_text(g_win, 12, (uint32_t)(g_win_h - footer_h + 6),
                         prompt, COLOR_TEXT, 12.0f);
        window_draw_text(g_win, 12, (uint32_t)(g_win_h - footer_h + 22),
                         "Enter: apply   Esc: cancel", COLOR_DIM, 12.0f);
    } else {
        window_draw_text(g_win, 12, (uint32_t)(g_win_h - footer_h + 6), g_status, g_refresh_failed ? COLOR_WARN : COLOR_DIM, 12.0f);
        window_draw_text(g_win, 12, (uint32_t)(g_win_h - footer_h + 22), "Enter: open  C: copy  D: delete  N: rename  R: refresh", COLOR_DIM, 12.0f);
    }

}

static void fm_ensure_visible(void)
{
    if (g_selected < g_scroll_top) {
        g_scroll_top = g_selected;
    }
    if (g_selected >= g_scroll_top + g_visible_rows) {
        g_scroll_top = g_selected - g_visible_rows + 1;
    }
}

void _start(void)
{
    g_win = window_create_ex(120, 60, 560, 380, COLOR_BG, "File Manager");
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

    fm_refresh();
    fm_render();

    while (1) {
        input_keyboard_event_t kbd;
        if (window_input_keyboard_poll(&kbd) > 0) {
            if (!kbd.pressed) {
                continue;
            }

            if (g_rename_active) {
                if (kbd.ascii == '\n' || kbd.keycode == 0x1C) {
                    fm_finish_rename();
                } else if (kbd.keycode == 0x01) {
                    g_rename_active = 0;
                    fm_set_status("Rename cancelled.");
                } else if (kbd.ascii == '\b' || kbd.keycode == 0x0E) {
                    if (g_rename_length > 0)
                        g_rename_buffer[--g_rename_length] = '\0';
                } else if (kbd.ascii >= 0x20 && kbd.ascii <= 0x7Eu &&
                           g_rename_length < FM_MAX_NAME_LEN - 1) {
                    g_rename_buffer[g_rename_length++] = (char)kbd.ascii;
                    g_rename_buffer[g_rename_length] = '\0';
                }
                fm_render();
                continue;
            }

            if (kbd.keycode == 0x48) {
                if (g_selected > 0) g_selected--;
                fm_ensure_visible();
                fm_render();
            }
            else if (kbd.keycode == 0x50) {
                if (g_selected < g_entry_count - 1) g_selected++;
                fm_ensure_visible();
                fm_render();
            }
            else if (kbd.ascii == '\n' || kbd.keycode == 0x1C) {
                if (g_selected >= 0 && g_selected < g_entry_count) {
                    fm_entry_t *e = &g_entries[g_selected];
                    if (e->is_dir) {
                        fm_enter_dir(e->name);
                        fm_render();
                    } else {
                        fm_open_selected_file(e);
                        fm_render();
                    }
                }
            }

            else if (kbd.ascii == '\b' || kbd.keycode == 0x0E) {
                fm_enter_dir("..");
                fm_render();
            }

            else if (kbd.ascii == 'r' || kbd.ascii == 'R') {
                fm_refresh();
                fm_render();
            }

            else if (kbd.ascii == 'c' || kbd.ascii == 'C') {
                if (g_selected >= 0 && g_selected < g_entry_count)
                    fm_copy_selected(&g_entries[g_selected]);
                fm_render();
            }

            else if (kbd.ascii == 'd' || kbd.ascii == 'D') {
                if (g_selected >= 0 && g_selected < g_entry_count)
                    fm_delete_selected(&g_entries[g_selected]);
                fm_render();
            }

            else if (kbd.ascii == 'n' || kbd.ascii == 'N') {
                if (g_selected >= 0 && g_selected < g_entry_count)
                    fm_begin_rename(&g_entries[g_selected]);
                fm_render();
            }
            
            else if (kbd.ascii == 'q' || kbd.ascii == 'Q') {
                process_exit(0);
            }
        }
        process_yield();
    }
}
