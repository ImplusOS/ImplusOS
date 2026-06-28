#include "../../../API/File.h"
#include "../../../API/Serial.h"
#include "../../../API/Process.h"
#include "../../../API/Graphics.h"
#include "../../../API/Window.h"
#include "../../../API/Input.h"
#include "../../../../libc/I_libc/include/string.h"
#include "../../../../libc/I_libc/include/stdlib.h"
#include "../../../../libc/I_libc/include/stdio.h"

#define FM_MAX_ENTRIES    256
#define FM_MAX_NAME_LEN   64
#define FM_MAX_PATH       512
#define FM_STATUS_LEN     128
#define FM_ENTRY_H        24
#define FM_MAX_SEGMENTS   32
#define FM_MAX_QUICK      12
#define FM_DCLICK_MS      400

#define FM_ASSOC_FILE     "/Userland/file-associations.conf"
#define FM_EDITOR_PATH    "/Userland/UserApps/com_ImplusOS_editor/com_ImplusOS_editor.ELF"
#define FM_PNG_VIEWER_PATH "/Userland/UserApps/com_ImplusOS_pngTest/com_ImplusOS_pngTest.ELF"

#define ADDR_BAR_H  44
#define SIDEBAR_W   180
#define COL_H       26
#define STATUS_H    50
#define SCROLL_W    7
#define PAD         12

typedef struct {
    char    name[FM_MAX_NAME_LEN];
    uint8_t is_dir;
    uint32_t size;
} fm_entry_t;

typedef struct {
    const char *label;
    const char *path;
    int         available;
} fm_quick_entry_t;

static window_id_t g_win = 0;
static int g_ww = 860;
static int g_wh = 540;

static char g_cwd[FM_MAX_PATH] = "/";
static char g_status[FM_STATUS_LEN] = "";
static fm_entry_t g_entries[FM_MAX_ENTRIES];
static int g_entry_count = 0;
static int g_sel = 0;
static int g_scroll = 0;
static int g_vis_rows = 0;
static int g_refresh_failed = 0;

static int g_ren_active = 0;
static char g_ren_buf[FM_MAX_NAME_LEN];
static int g_ren_len = 0;

static uint8_t g_mouse_btn = 0;
static uint8_t g_prev_mouse_btn = 0;
static int g_mx = 0, g_my = 0;
static int g_wheel = 0;

static uint64_t g_last_click_ms = 0;
static int g_last_click_idx = -1;

static char g_seg_name[FM_MAX_SEGMENTS][FM_MAX_NAME_LEN];
static int  g_seg_count = 0;
static int  g_seg_x[FM_MAX_SEGMENTS];
static int  g_seg_path_end[FM_MAX_SEGMENTS];

static int g_path_bar_end = 0;

#define mouse_pressed(btn) ((g_mouse_btn & (btn)) && !(g_prev_mouse_btn & (btn)))
#define mouse_released(btn) (!(g_mouse_btn & (btn)) && (g_prev_mouse_btn & (btn)))

#define C_WHITE      0xFFFFFFFF
#define C_BG         0xFFFFFFFF
#define C_BG2        0xFFF5F5F5
#define C_SIDEBAR    0xFFFAFAFA
#define C_TEXT       0xFF222222
#define C_DIM        0xFF888888
#define C_BORDER     0xFFDDDDDD
#define C_SEL        0xFFE8E8E8
#define C_SEL_BAR    0xFFBBBBBB
#define C_HOVER      0xFFF0F0F0
#define C_ACCENT     0xFF444444
#define C_WARN       0xFFCC6600
#define C_DIR_ICON   0xFF666666
#define C_FILE_ICON  0xFF999999
#define C_ROW_EVEN   0xFFFFFFFF
#define C_ROW_ODD    0xFFF8F8F8

static fm_quick_entry_t g_quick[FM_MAX_QUICK] = {
    { "Root",        "/",                        1 },
    { "Kernel",      "/Kernel/",                 1 },
    { "Drivers",     "/Kernel/Driver/",          1 },
    { "Userland",    "/Userland/",               1 },
    { "System Apps", "/Userland/SystemApps/",    1 },
    { "User Apps",   "/Userland/UserApps/",      1 },
    { "BootManager", "/BootManager/",            1 },
};
static int g_quick_count = 7;
static int g_quick_hover = -1;

static void fm_refresh(void);

static void fm_set_status(const char *s)
{
    if (!s) { g_status[0] = '\0'; return; }
    strncpy(g_status, s, FM_STATUS_LEN - 1);
    g_status[FM_STATUS_LEN - 1] = '\0';
}

static char fm_ascii_lower(char c)
{
    if (c >= 'A' && c <= 'Z') return (char)(c + ('a' - 'A'));
    return c;
}

static int fm_name_compare(const char *a, const char *b)
{
    while (*a && *b) {
        char ca = fm_ascii_lower(*a);
        char cb = fm_ascii_lower(*b);
        if (ca != cb) return (int)(unsigned char)ca - (int)(unsigned char)cb;
        ++a; ++b;
    }
    return (int)(unsigned char)fm_ascii_lower(*a) - (int)(unsigned char)fm_ascii_lower(*b);
}

static int fm_entry_compare(const void *left, const void *right)
{
    const fm_entry_t *a = (const fm_entry_t *)left;
    const fm_entry_t *b = (const fm_entry_t *)right;
    if (a->is_dir != b->is_dir) return a->is_dir ? -1 : 1;
    return fm_name_compare(a->name, b->name);
}

static void fm_sort_entries(int start)
{
    if (start < 0) start = 0;
    if (start >= g_entry_count - 1) return;
    qsort(&g_entries[start], (size_t)(g_entry_count - start), sizeof(g_entries[0]), fm_entry_compare);
}

static void fm_join_path(const char *base, const char *name, char *out, int out_len)
{
    if (!out || out_len <= 0) return;
    if (!base || !name) { out[0] = '\0'; return; }
    if (strcmp(base, "/") == 0)
        snprintf(out, (size_t)out_len, "/%s", name);
    else
        snprintf(out, (size_t)out_len, "%s/%s", base, name);
}

static int fm_copy_file(const char *src, const char *dst)
{
    int32_t s = file_open(src, 0);
    if (s < 0) return -1;
    int32_t d = file_creat(dst);
    if (d < 0) { file_close(s); return -1; }
    char buf[4096];
    int r = 0;
    for (;;) {
        int64_t n = file_read(s, buf, sizeof(buf));
        if (n < 0) { r = -1; break; }
        if (n == 0) break;
        int64_t w = 0;
        while (w < n) {
            int64_t c = file_write(d, buf + w, (uint64_t)(n - w));
            if (c <= 0) { r = -1; break; }
            w += c;
        }
        if (r < 0) break;
    }
    file_close(s);
    file_close(d);
    if (r < 0) file_unlink(dst);
    return r;
}

static const char *fm_extension(const char *name)
{
    const char *dot = strrchr(name, '.');
    return dot && dot[1] ? dot + 1 : "";
}

static int fm_find_association(const char *name, char *path, size_t path_size)
{
    const char *ext = fm_extension(name);
    int32_t fd = file_open(FM_ASSOC_FILE, 0);
    if (fd >= 0) {
        char buf[16385];
        int64_t n = file_read(fd, buf, sizeof(buf) - 1u);
        file_close(fd);
        if (n > 0) {
            buf[n] = '\0';
            char *line = buf;
            while (*line) {
                char *end = strchr(line, '\n');
                if (end) *end = '\0';
                char *sep = strchr(line, '=');
                if (sep) {
                    *sep = '\0';
                    if (strcasecmp(line, ext) == 0 && sep[1] == '/') {
                        strlcpy(path, sep + 1, path_size);
                        return path[0] ? 0 : -1;
                    }
                }
                if (!end) break;
                line = end + 1;
            }
        }
    }
    if (strcasecmp(ext, "txt") == 0 || strcasecmp(ext, "md") == 0 ||
        strcasecmp(ext, "log") == 0 || strcasecmp(ext, "conf") == 0 ||
        strcasecmp(ext, "c") == 0 || strcasecmp(ext, "h") == 0) {
        strlcpy(path, FM_EDITOR_PATH, path_size);
        return 0;
    }
    if (strcasecmp(ext, "png") == 0) {
        strlcpy(path, FM_PNG_VIEWER_PATH, path_size);
        return 0;
    }
    return -1;
}

static void fm_open_selected_file(const fm_entry_t *e)
{
    char fp[FM_MAX_PATH], ap[FM_MAX_PATH];
    fm_join_path(g_cwd, e->name, fp, FM_MAX_PATH);
    if (fm_find_association(e->name, ap, sizeof(ap)) < 0) {
        fm_set_status("No application is associated with this file type.");
        return;
    }
    int32_t pid = process_spawn_with_arg(ap, fp);
    if (pid < 0) { fm_set_status("The associated application could not be started."); return; }
    char s[FM_STATUS_LEN];
    snprintf(s, sizeof(s), "Opened %s", e->name);
    fm_set_status(s);
}

static void fm_copy_selected(const fm_entry_t *e)
{
    if (!e || e->is_dir) { fm_set_status("Folder copying is not supported."); return; }
    char src[FM_MAX_PATH], dst[FM_MAX_PATH], cn[FM_MAX_NAME_LEN];
    fm_join_path(g_cwd, e->name, src, FM_MAX_PATH);
    unsigned sfx = 1;
    for (; sfx < 1000u; ++sfx) {
        snprintf(cn, sizeof(cn), "%s.copy%u", e->name, sfx);
        fm_join_path(g_cwd, cn, dst, FM_MAX_PATH);
        file_stat_t st;
        if (file_stat(dst, &st) < 0 || !st.exists) break;
    }
    if (fm_copy_file(src, dst) < 0) { fm_set_status("Copy failed."); return; }
    fm_refresh();
    fm_set_status("File copied.");
}

static void fm_delete_selected(const fm_entry_t *e)
{
    if (!e || e->is_dir) { fm_set_status("Only regular files can be deleted."); return; }
    char p[FM_MAX_PATH];
    fm_join_path(g_cwd, e->name, p, FM_MAX_PATH);
    if (file_unlink(p) < 0) { fm_set_status("Delete failed."); return; }
    fm_refresh();
    fm_set_status("File deleted.");
}

static void fm_begin_rename(const fm_entry_t *e)
{
    if (!e || strcmp(e->name, "..") == 0) return;
    strlcpy(g_ren_buf, e->name, sizeof(g_ren_buf));
    g_ren_len = (int)strlen(g_ren_buf);
    g_ren_active = 1;
}

static void fm_finish_rename(void)
{
    if (g_sel < 0 || g_sel >= g_entry_count || g_ren_len == 0 || strchr(g_ren_buf, '/')) {
        g_ren_active = 0;
        fm_set_status("Invalid file name.");
        return;
    }
    char op[FM_MAX_PATH], np[FM_MAX_PATH];
    fm_join_path(g_cwd, g_entries[g_sel].name, op, FM_MAX_PATH);
    fm_join_path(g_cwd, g_ren_buf, np, FM_MAX_PATH);
    g_ren_active = 0;
    if (file_rename(op, np) < 0) { fm_set_status("Rename failed."); return; }
    fm_refresh();
    fm_set_status("Item renamed.");
}

static void fm_refresh(void)
{
    g_entry_count = 0;
    g_sel = 0;
    g_scroll = 0;
    g_refresh_failed = 0;

    int32_t dh = file_opendir(g_cwd);
    if (dh < 0 && strcmp(g_cwd, "/") != 0) {
        strncpy(g_cwd, "/", FM_MAX_PATH - 1);
        g_cwd[FM_MAX_PATH - 1] = '\0';
        dh = file_opendir(g_cwd);
        if (dh >= 0) fm_set_status("The previous folder was unavailable, so the view returned to /.");
    }
    if (dh < 0) { g_refresh_failed = 1; fm_set_status("This folder could not be opened."); return; }

    if (strlen(g_cwd) > 1 && g_entry_count < FM_MAX_ENTRIES) {
        strncpy(g_entries[g_entry_count].name, "..", FM_MAX_NAME_LEN - 1);
        g_entries[g_entry_count].name[FM_MAX_NAME_LEN - 1] = '\0';
        g_entries[g_entry_count].is_dir = 1;
        g_entries[g_entry_count].size = 0;
        g_entry_count++;
    }

    file_dirent_t de;
    while (file_readdir(dh, &de) > 0 && g_entry_count < FM_MAX_ENTRIES) {
        if (de.name[0] == '\0' || de.name[0] == '.') continue;
        strncpy(g_entries[g_entry_count].name, de.name, FM_MAX_NAME_LEN - 1);
        g_entries[g_entry_count].name[FM_MAX_NAME_LEN - 1] = '\0';
        g_entries[g_entry_count].is_dir = (de.attributes & 0x10u) ? 1 : 0;
        g_entries[g_entry_count].size = de.size;
        g_entry_count++;
    }
    file_closedir(dh);

    fm_sort_entries((strlen(g_cwd) > 1) ? 1 : 0);

    for (int i = 0; i < g_quick_count; i++) {
        file_stat_t st;
        g_quick[i].available = (file_stat(g_quick[i].path, &st) >= 0 && st.exists) ? 1 : 0;
    }

    int item_count = g_entry_count - ((strlen(g_cwd) > 1) ? 1 : 0);
    if (item_count <= 0)
        fm_set_status(g_refresh_failed ? "Unavailable" : "This folder is empty.");
    else {
        char s[FM_STATUS_LEN];
        snprintf(s, sizeof(s), "%d item%s in %s", item_count, item_count == 1 ? "" : "s", g_cwd);
        fm_set_status(s);
    }
}

static void fm_enter_dir(const char *name)
{
    char next[FM_MAX_PATH];
    if (strcmp(name, "..") == 0) {
        int len = (int)strlen(g_cwd);
        if (len <= 1) { fm_set_status("Already at the root folder."); return; }
        strncpy(next, g_cwd, FM_MAX_PATH - 1);
        next[FM_MAX_PATH - 1] = '\0';
        if (next[len - 1] == '/') next[--len] = '\0';
        char *last = next;
        for (char *p = next; *p; ++p) if (*p == '/') last = p;
        if (last == next) next[1] = '\0';
        else *last = '\0';
    } else {
        fm_join_path(g_cwd, name, next, FM_MAX_PATH);
    }
    file_stat_t st;
    if (file_stat(next, &st) < 0 || !st.exists || !st.is_dir) {
        char s[FM_STATUS_LEN];
        snprintf(s, sizeof(s), "Folder not available: %s", next);
        fm_set_status(s);
        return;
    }
    strncpy(g_cwd, next, FM_MAX_PATH - 1);
    g_cwd[FM_MAX_PATH - 1] = '\0';
    fm_refresh();
}

static const char *fm_file_type_str(const char *name)
{
    const char *ext = fm_extension(name);
    if (ext[0] == '\0') return "FILE";
    if (strcasecmp(ext, "txt") == 0 || strcasecmp(ext, "md") == 0 ||
        strcasecmp(ext, "log") == 0 || strcasecmp(ext, "conf") == 0 ||
        strcasecmp(ext, "cfg") == 0 || strcasecmp(ext, "ini") == 0 ||
        strcasecmp(ext, "json") == 0 || strcasecmp(ext, "xml") == 0 ||
        strcasecmp(ext, "yaml") == 0 || strcasecmp(ext, "yml") == 0 ||
        strcasecmp(ext, "toml") == 0) return "TEXT";
    if (strcasecmp(ext, "c") == 0 || strcasecmp(ext, "h") == 0 ||
        strcasecmp(ext, "cpp") == 0 || strcasecmp(ext, "hpp") == 0 ||
        strcasecmp(ext, "asm") == 0 || strcasecmp(ext, "s") == 0 ||
        strcasecmp(ext, "py") == 0 || strcasecmp(ext, "sh") == 0 ||
        strcasecmp(ext, "ld") == 0 || strcasecmp(ext, "mk") == 0)
        return "CODE";
    if (strcasecmp(ext, "png") == 0 || strcasecmp(ext, "jpg") == 0 ||
        strcasecmp(ext, "jpeg") == 0 || strcasecmp(ext, "gif") == 0 ||
        strcasecmp(ext, "bmp") == 0 || strcasecmp(ext, "ico") == 0 ||
        strcasecmp(ext, "svg") == 0) return "IMAGE";
    if (strcasecmp(ext, "wav") == 0 || strcasecmp(ext, "mp3") == 0 ||
        strcasecmp(ext, "ogg") == 0 || strcasecmp(ext, "flac") == 0)
        return "AUDIO";
    if (strcasecmp(ext, "mp4") == 0 || strcasecmp(ext, "avi") == 0 ||
        strcasecmp(ext, "mkv") == 0 || strcasecmp(ext, "mov") == 0)
        return "VIDEO";
    if (strcasecmp(ext, "zip") == 0 || strcasecmp(ext, "tar") == 0 ||
        strcasecmp(ext, "gz") == 0 || strcasecmp(ext, "bz2") == 0 ||
        strcasecmp(ext, "xz") == 0 || strcasecmp(ext, "7z") == 0 ||
        strcasecmp(ext, "rar") == 0) return "ARCHIVE";
    if (strcasecmp(ext, "iso") == 0 || strcasecmp(ext, "img") == 0) return "DISK";
    if (strcasecmp(ext, "elf") == 0 || strcasecmp(ext, "exe") == 0 ||
        strcasecmp(ext, "bin") == 0) return "BINARY";
    return "FILE";
}

static uint32_t fm_file_type_color(const char *name)
{
    const char *ft = fm_file_type_str(name);
    if (strcmp(ft, "TEXT") == 0 || strcmp(ft, "CODE") == 0) return 0xFF666666;
    if (strcmp(ft, "IMAGE") == 0 || strcmp(ft, "AUDIO") == 0) return 0xFF777777;
    if (strcmp(ft, "VIDEO") == 0 || strcmp(ft, "ARCHIVE") == 0) return 0xFF555555;
    if (strcmp(ft, "DISK") == 0) return 0xFF888888;
    if (strcmp(ft, "BINARY") == 0) return 0xFF444444;
    return C_DIM;
}

static void fm_format_size(uint32_t size, char *buf, int buf_len)
{
    if (!buf || buf_len <= 0) return;
    if (size < 1024) snprintf(buf, (size_t)buf_len, "%u B", size);
    else if (size < 1024u * 1024u) snprintf(buf, (size_t)buf_len, "%u KB", size / 1024u);
    else snprintf(buf, (size_t)buf_len, "%u MB", size / (1024u * 1024u));
}

static int fm_est_text_width(const char *s, float font_size)
{
    if (!s) return 0;
    int len = (int)strlen(s);
    int approx = (int)((float)len * font_size * 0.55f);
    return approx > 0 ? approx : len * 8;
}

static void fm_compute_path_segments(void)
{
    g_seg_count = 0;
    int len = (int)strlen(g_cwd);
    if (len == 0) return;

    g_seg_path_end[0] = 1;
    strncpy(g_seg_name[0], "/", FM_MAX_NAME_LEN - 1);
    g_seg_name[0][FM_MAX_NAME_LEN - 1] = '\0';
    g_seg_count = 1;

    if (len == 1) return;

    int start = 1;
    for (int i = 1; i <= len; i++) {
        if (g_cwd[i] == '/' || g_cwd[i] == '\0') {
            if (i > start) {
                int seg = g_seg_count;
                int seg_len = i - start;
                if (seg_len >= FM_MAX_NAME_LEN) seg_len = FM_MAX_NAME_LEN - 1;
                memcpy(g_seg_name[seg], g_cwd + start, (size_t)seg_len);
                g_seg_name[seg][seg_len] = '\0';
                g_seg_path_end[seg] = i;
                g_seg_count++;
            }
            start = i + 1;
        }
    }
}

static void fm_render(void)
{
    int list_x = SIDEBAR_W;
    int list_w = g_ww - SIDEBAR_W;
    int list_top = ADDR_BAR_H + COL_H;

    window_clear(g_win);
    draw_fill_rect(0, 0, (uint32_t)g_ww, (uint32_t)g_wh, C_BG);

    draw_fill_rect(0, 0, (uint32_t)g_ww, (uint32_t)ADDR_BAR_H, C_BG2);
    draw_fill_rect(0, ADDR_BAR_H, (uint32_t)g_ww, 1, C_BORDER);

    int path_x = PAD;
    int path_y = 12;
    int path_max_w = list_w - PAD - 120;

    fm_compute_path_segments();

    int cx = path_x;
    int seg_font = 12;
    for (int i = 0; i < g_seg_count; i++) {
        g_seg_x[i] = cx;
        int w = fm_est_text_width(g_seg_name[i], (float)seg_font);
        if (cx + w + 16 > path_x + path_max_w && i > 0) {
            window_draw_text(g_win, (uint32_t)cx, (uint32_t)path_y, "...", C_DIM, (float)seg_font);
            cx += fm_est_text_width("...", (float)seg_font) + 4;
            break;
        }
        uint32_t col = (i == g_seg_count - 1) ? C_ACCENT : C_DIM;
        window_draw_text(g_win, (uint32_t)cx, (uint32_t)path_y, g_seg_name[i], col, (float)seg_font);
        cx += w + 2;
        if (i < g_seg_count - 1) {
            window_draw_text(g_win, (uint32_t)cx, (uint32_t)path_y, "/", C_BORDER, (float)seg_font);
            cx += fm_est_text_width("/", (float)seg_font) + 2;
        }
    }
    g_path_bar_end = cx;

    int btn_y = 10;
    int btn_x = g_ww - 32;
    draw_fill_rect((uint32_t)btn_x, (uint32_t)btn_y, 20, 20, C_BG);
    draw_fill_rect((uint32_t)(btn_x + 4), (uint32_t)(btn_y + 4), 12, 12, C_ACCENT);

    btn_x -= 28;
    draw_fill_rect((uint32_t)btn_x, (uint32_t)(btn_y + 2), 16, 16, C_BG2);
    draw_fill_rect((uint32_t)(btn_x + 7), (uint32_t)(btn_y + 4), 2, 12, C_ACCENT);

    btn_x -= 28;
    draw_fill_rect((uint32_t)btn_x, (uint32_t)(btn_y + 8), 16, 8, C_ACCENT);
    draw_fill_rect((uint32_t)(btn_x + 4), (uint32_t)(btn_y + 2), 8, 8, C_BG2);

    draw_fill_rect(0, (uint32_t)ADDR_BAR_H, (uint32_t)SIDEBAR_W, (uint32_t)(g_wh - ADDR_BAR_H - STATUS_H), C_SIDEBAR);
    draw_fill_rect(SIDEBAR_W, (uint32_t)ADDR_BAR_H, 1, (uint32_t)(g_wh - ADDR_BAR_H - STATUS_H), C_BORDER);

    int sqy = ADDR_BAR_H;
    int sqh = 26;
    draw_fill_rect(0, (uint32_t)sqy, (uint32_t)SIDEBAR_W, (uint32_t)sqh, C_BG2);
    window_draw_text(g_win, PAD, (uint32_t)(sqy + 6), "Quick Access", C_ACCENT, 12.0f);
    draw_fill_rect(0, (uint32_t)(sqy + sqh - 1), (uint32_t)SIDEBAR_W, 1, C_BORDER);

    int qy = sqy + sqh;
    for (int i = 0; i < g_quick_count; i++) {
        uint32_t qc = g_quick_hover == i ? C_HOVER : C_SIDEBAR;
        draw_fill_rect(0, (uint32_t)qy, (uint32_t)SIDEBAR_W, (uint32_t)FM_ENTRY_H, qc);
        if (g_quick[i].available) {
            draw_fill_rect(PAD, (uint32_t)(qy + 5), 8, 8, C_DIR_ICON);
            draw_fill_rect(PAD, (uint32_t)(qy + 3), 5, 3, C_DIR_ICON);
        }
        uint32_t tc = g_quick[i].available ? C_TEXT : C_DIM;
        int label_x = PAD + 14;
        if (label_x < SIDEBAR_W - 8) {
            char buf[32];
            int slen = (int)strlen(g_quick[i].label);
            int max = SIDEBAR_W - label_x - 4;
            int est = fm_est_text_width(g_quick[i].label, 11.0f);
            if (est > max) {
                int keep = (max * slen) / est;
                if (keep < 2) keep = 2;
                if (keep > (int)sizeof(buf) - 2) keep = (int)sizeof(buf) - 2;
                memcpy(buf, g_quick[i].label, (size_t)keep);
                buf[keep] = '\0';
                window_draw_text(g_win, (uint32_t)label_x, (uint32_t)(qy + 5), buf, tc, 11.0f);
            } else {
                window_draw_text(g_win, (uint32_t)label_x, (uint32_t)(qy + 5), g_quick[i].label, tc, 11.0f);
            }
        }
        qy += FM_ENTRY_H;
    }

    draw_fill_rect(0, (uint32_t)qy, (uint32_t)SIDEBAR_W, 1, C_BORDER);

    draw_fill_rect((uint32_t)list_x, (uint32_t)ADDR_BAR_H, (uint32_t)list_w, (uint32_t)COL_H, C_BG2);
    draw_fill_rect(list_x, ADDR_BAR_H + COL_H - 1, (uint32_t)list_w, 1, C_BORDER);

    int col_name_x = list_x + PAD;
    int col_type_x = g_ww - 170;
    int col_size_x = g_ww - 86;
    window_draw_text(g_win, (uint32_t)col_name_x, (uint32_t)(ADDR_BAR_H + 6), "Name", C_TEXT, 12.0f);
    window_draw_text(g_win, (uint32_t)col_type_x, (uint32_t)(ADDR_BAR_H + 6), "Type", C_TEXT, 12.0f);
    window_draw_text(g_win, (uint32_t)col_size_x, (uint32_t)(ADDR_BAR_H + 6), "Size", C_TEXT, 12.0f);

    g_vis_rows = (g_wh - list_top - STATUS_H) / FM_ENTRY_H;
    if (g_vis_rows < 1) g_vis_rows = 1;

    int icon_x = list_x + PAD;
    int text_x = icon_x + 20;

    if (g_entry_count == 0) {
        uint32_t er_y = (uint32_t)list_top;
        draw_fill_rect((uint32_t)list_x, er_y, (uint32_t)list_w, 40, C_BG2);
        window_draw_text(g_win, (uint32_t)(list_x + 12), er_y + 14, g_status, g_refresh_failed ? C_WARN : C_DIM, 13.0f);
    }

    for (int i = 0; i < g_vis_rows; i++) {
        int idx = g_scroll + i;
        if (idx >= g_entry_count) break;

        int y = list_top + i * FM_ENTRY_H;
        fm_entry_t *e = &g_entries[idx];

        if (idx == g_sel) {
            draw_fill_rect((uint32_t)list_x, (uint32_t)y, (uint32_t)list_w, (uint32_t)FM_ENTRY_H, C_SEL);
            draw_fill_rect((uint32_t)list_x, (uint32_t)y, 4, (uint32_t)FM_ENTRY_H, C_SEL_BAR);
        } else {
            draw_fill_rect((uint32_t)list_x, (uint32_t)y, (uint32_t)list_w, (uint32_t)FM_ENTRY_H, (idx % 2 == 0) ? C_ROW_EVEN : C_ROW_ODD);
        }

        if (i > 0) {
            draw_fill_rect((uint32_t)list_x, (uint32_t)y, (uint32_t)list_w, 1, C_BORDER);
        }

        if (e->is_dir) {
            if (strcmp(e->name, "..") == 0) {
                draw_fill_rect((uint32_t)(icon_x + 2), (uint32_t)(y + 5), 10, 10, C_WARN);
                draw_fill_rect((uint32_t)(icon_x + 5), (uint32_t)(y + 8), 4, 4, C_BG);
            } else {
                draw_fill_rect((uint32_t)(icon_x + 2), (uint32_t)(y + 4), 14, 12, C_DIR_ICON);
                draw_fill_rect((uint32_t)(icon_x + 2), (uint32_t)(y + 2), 8, 4, C_DIR_ICON);
            }
        } else {
            uint32_t fc = fm_file_type_color(e->name);
            draw_fill_rect((uint32_t)(icon_x + 3), (uint32_t)(y + 3), 12, 14, fc);
            draw_fill_rect((uint32_t)(icon_x + 5), (uint32_t)(y + 7), 8, 1, C_BG);
        }

        uint32_t name_col = (idx == g_sel) ? C_TEXT : (e->is_dir ? C_DIR_ICON : C_TEXT);
        window_draw_text(g_win, (uint32_t)text_x, (uint32_t)(y + 4), e->name, name_col, 13.0f);

        if (e->is_dir) {
            window_draw_text(g_win, (uint32_t)col_type_x, (uint32_t)(y + 4), strcmp(e->name, "..") == 0 ? "UP" : "DIR", C_DIR_ICON, 11.0f);
            window_draw_text(g_win, (uint32_t)col_size_x, (uint32_t)(y + 4), "--", C_DIM, 11.0f);
        } else {
            const char *ft = fm_file_type_str(e->name);
            uint32_t tc = fm_file_type_color(e->name);
            char sz[16];
            fm_format_size(e->size, sz, (int)sizeof(sz));
            window_draw_text(g_win, (uint32_t)col_type_x, (uint32_t)(y + 4), ft, tc, 11.0f);
            window_draw_text(g_win, (uint32_t)col_size_x, (uint32_t)(y + 4), sz, C_DIM, 11.0f);
        }
    }

    if (g_entry_count > g_vis_rows) {
        int sa_top = list_top;
        int sa_h = g_vis_rows * FM_ENTRY_H;
        int sb_x = list_x + list_w - SCROLL_W - 2;
        int thumb_h = (sa_h * g_vis_rows) / g_entry_count;
        if (thumb_h < 14) thumb_h = 14;
        int thumb_max = sa_h - thumb_h;
        int thumb_y = sa_top + (thumb_max * g_scroll) / (g_entry_count - g_vis_rows);

        draw_fill_rect((uint32_t)sb_x, (uint32_t)sa_top, (uint32_t)SCROLL_W, (uint32_t)sa_h, C_ROW_ODD);
        draw_fill_rect((uint32_t)sb_x, (uint32_t)sa_top, (uint32_t)SCROLL_W, 1, C_BORDER);
        draw_fill_rect((uint32_t)(sb_x + SCROLL_W - 1), (uint32_t)sa_top, 1, (uint32_t)sa_h, C_BORDER);
        draw_fill_rect((uint32_t)sb_x, (uint32_t)(sa_top + sa_h - 1), (uint32_t)SCROLL_W, 1, C_BORDER);
        draw_fill_rect((uint32_t)sb_x, (uint32_t)thumb_y, (uint32_t)SCROLL_W, (uint32_t)thumb_h, C_SEL_BAR);
    }

    draw_fill_rect(0, (uint32_t)(g_wh - STATUS_H), (uint32_t)g_ww, (uint32_t)STATUS_H, C_BG2);
    draw_fill_rect(0, (uint32_t)(g_wh - STATUS_H - 1), (uint32_t)g_ww, 1, C_BORDER);

    if (g_ren_active) {
        char prompt[FM_STATUS_LEN];
        snprintf(prompt, sizeof(prompt), "Rename to: %s", g_ren_buf);
        window_draw_text(g_win, PAD, (uint32_t)(g_wh - STATUS_H + 8), prompt, C_TEXT, 12.0f);
        window_draw_text(g_win, PAD, (uint32_t)(g_wh - STATUS_H + 28), "Enter: apply   Esc: cancel", C_DIM, 11.0f);
    } else {
        window_draw_text(g_win, PAD, (uint32_t)(g_wh - STATUS_H + 8), g_status, g_refresh_failed ? C_WARN : C_DIM, 12.0f);
        window_draw_text(g_win, PAD, (uint32_t)(g_wh - STATUS_H + 28),
                         "Enter: open  C: copy  D: delete  N: rename  R: refresh  Q: quit", C_DIM, 11.0f);
    }
}

static void fm_ensure_visible(void)
{
    if (g_sel < g_scroll) g_scroll = g_sel;
    if (g_sel >= g_scroll + g_vis_rows) g_scroll = g_sel - g_vis_rows + 1;
}

static int fm_get_entry_at_y(int my)
{
    int list_top = ADDR_BAR_H + COL_H;
    int rel = my - list_top;
    if (rel < 0) return -1;
    int idx = g_scroll + rel / FM_ENTRY_H;
    if (idx >= g_entry_count) return -1;
    return idx;
}

static int fm_get_quick_at_y(int my)
{
    int sqh = 26;
    int qy = ADDR_BAR_H + sqh;
    int rel = my - qy;
    if (rel < 0 || my >= g_wh - STATUS_H) return -1;
    int idx = rel / FM_ENTRY_H;
    if (idx >= g_quick_count) return -1;
    return idx;
}

static int fm_get_segment_at_x(int mx)
{
    for (int i = 0; i < g_seg_count; i++) {
        int seg_w = fm_est_text_width(g_seg_name[i], 12.0f);
        int x_end = g_seg_x[i] + seg_w + 2 + fm_est_text_width("/", 12.0f) + 2;
        if (i == g_seg_count - 1) x_end = g_path_bar_end;
        if (mx >= g_seg_x[i] && mx < x_end) return i;
    }
    return -1;
}

static void fm_handle_mouse_click(int mx, int my)
{
    if (my < ADDR_BAR_H) {
        int btn_home_x = g_ww - 32;
        int btn_up_x = btn_home_x - 28;
        int btn_ref_x = btn_up_x - 28;

        if (mx >= btn_ref_x && mx < btn_ref_x + 20 && my >= 10 && my < 30) {
            fm_refresh();
            return;
        }
        if (mx >= btn_up_x && mx < btn_up_x + 20 && my >= 10 && my < 30) {
            fm_enter_dir("..");
            return;
        }
        if (mx >= btn_home_x && mx < btn_home_x + 20 && my >= 10 && my < 30) {
            strncpy(g_cwd, "/", FM_MAX_PATH - 1);
            g_cwd[FM_MAX_PATH - 1] = '\0';
            fm_refresh();
            return;
        }

        int seg_idx = fm_get_segment_at_x(mx);
        if (seg_idx >= 0 && seg_idx < g_seg_count - 1) {
            int end = g_seg_path_end[seg_idx];
            char tmp[FM_MAX_PATH];
            if (end <= 1) {
                strncpy(tmp, "/", FM_MAX_PATH - 1);
            } else {
                memcpy(tmp, g_cwd, (size_t)(end));
                tmp[end] = '\0';
            }
            tmp[FM_MAX_PATH - 1] = '\0';
            strncpy(g_cwd, tmp, FM_MAX_PATH - 1);
            g_cwd[FM_MAX_PATH - 1] = '\0';
            fm_refresh();
        }
        return;
    }

    if (my >= g_wh - STATUS_H) return;

    if (mx < SIDEBAR_W) {
        int qi = fm_get_quick_at_y(my);
        if (qi >= 0 && qi < g_quick_count && g_quick[qi].available) {
            strncpy(g_cwd, g_quick[qi].path, FM_MAX_PATH - 1);
            g_cwd[FM_MAX_PATH - 1] = '\0';
            fm_refresh();
        }
        return;
    }

    int ei = fm_get_entry_at_y(my);
    if (ei >= 0) {
        uint64_t now = get_uptime_ms();
        if (ei == g_last_click_idx && now - g_last_click_ms < FM_DCLICK_MS) {
            fm_entry_t *e = &g_entries[ei];
            if (e->is_dir) fm_enter_dir(e->name);
            else fm_open_selected_file(e);
            g_last_click_ms = 0;
            g_last_click_idx = -1;
            return;
        }
        g_sel = ei;
        g_last_click_ms = now;
        g_last_click_idx = ei;
        fm_ensure_visible();
    }
}

void _start(void)
{
    g_win = window_create_ex(80, 40, (uint32_t)g_ww, (uint32_t)g_wh, C_BG, "File Manager");
    if (g_win == 0) { while (1) process_yield(); }

    window_subscribe_keyboard(g_win);
    window_subscribe_mouse(g_win);
    graphics_init(g_win);

    uint32_t wx, wy, ww, wh;
    if (window_get_rect(g_win, &wx, &wy, &ww, &wh) == 0) {
        g_ww = (int)ww;
        g_wh = (int)wh;
    }

    fm_refresh();
    fm_render();

    while (1) {
        input_keyboard_event_t kbd;
        input_mouse_event_t mev;
        int had_event = 0;

        while (window_input_keyboard_poll(&kbd) > 0) {
            had_event = 1;
            if (!kbd.pressed) continue;

            if (g_ren_active) {
                if (kbd.ascii == '\n' || kbd.keycode == 0x1C) { fm_finish_rename(); }
                else if (kbd.keycode == 0x01) { g_ren_active = 0; fm_set_status("Rename cancelled."); }
                else if (kbd.ascii == '\b' || kbd.keycode == 0x0E) { if (g_ren_len > 0) g_ren_buf[--g_ren_len] = '\0'; }
                else if (kbd.ascii >= 0x20 && kbd.ascii <= 0x7Eu && g_ren_len < FM_MAX_NAME_LEN - 1) {
                    g_ren_buf[g_ren_len++] = (char)kbd.ascii;
                    g_ren_buf[g_ren_len] = '\0';
                }
                fm_render();
                continue;
            }

            if (kbd.keycode == 0x48) {
                if (g_sel > 0) g_sel--;
                fm_ensure_visible();
                fm_render();
            } else if (kbd.keycode == 0x50) {
                if (g_sel < g_entry_count - 1) g_sel++;
                fm_ensure_visible();
                fm_render();
            } else if (kbd.keycode == 0x4B) {
                fm_enter_dir("..");
                fm_render();
            } else if (kbd.keycode == 0x4D) {
                if (g_sel >= 0 && g_sel < g_entry_count) {
                    fm_entry_t *e = &g_entries[g_sel];
                    if (e->is_dir) { fm_enter_dir(e->name); fm_render(); }
                }
            } else if (kbd.ascii == '\n' || kbd.keycode == 0x1C) {
                if (g_sel >= 0 && g_sel < g_entry_count) {
                    fm_entry_t *e = &g_entries[g_sel];
                    if (e->is_dir) { fm_enter_dir(e->name); fm_render(); }
                    else { fm_open_selected_file(e); fm_render(); }
                }
            } else if (kbd.ascii == '\b' || kbd.keycode == 0x0E) {
                fm_enter_dir("..");
                fm_render();
            } else if (kbd.ascii == 'r' || kbd.ascii == 'R') {
                fm_refresh();
                fm_render();
            } else if (kbd.ascii == 'c' || kbd.ascii == 'C') {
                if (g_sel >= 0 && g_sel < g_entry_count) fm_copy_selected(&g_entries[g_sel]);
                fm_render();
            } else if (kbd.ascii == 'd' || kbd.ascii == 'D') {
                if (g_sel >= 0 && g_sel < g_entry_count) fm_delete_selected(&g_entries[g_sel]);
                fm_render();
            } else if (kbd.ascii == 'n' || kbd.ascii == 'N') {
                if (g_sel >= 0 && g_sel < g_entry_count) fm_begin_rename(&g_entries[g_sel]);
                fm_render();
            } else if (kbd.ascii == 'q' || kbd.ascii == 'Q') {
                process_exit(0);
            }
        }

        g_prev_mouse_btn = g_mouse_btn;
        g_wheel = 0;
        while (window_input_mouse_poll(&mev) > 0) {
            had_event = 1;
            g_mx = (int)(int16_t)mev.x;
            g_my = (int)(int16_t)mev.y;
            g_mouse_btn = mev.buttons;
            g_wheel = mev.wheel;

            if (g_wheel != 0 && !g_ren_active) {
                int step = (g_wheel > 0) ? 3 : -3;
                g_scroll += step;
                if (g_scroll < 0) g_scroll = 0;
                if (g_scroll > g_entry_count - g_vis_rows) g_scroll = g_entry_count - g_vis_rows;
                if (g_scroll < 0) g_scroll = 0;
                fm_render();
                continue;
            }

            int new_hover = fm_get_quick_at_y(g_my);
            if (new_hover != g_quick_hover) {
                g_quick_hover = new_hover;
                fm_render();
            }

            if (mouse_pressed(INPUT_MOUSE_BTN_LEFT)) {
                fm_handle_mouse_click(g_mx, g_my);
                fm_render();
            }
        }

        if (!had_event) process_yield();
    }
}
