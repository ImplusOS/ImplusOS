#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>

#include "API/Process.h"
#include "API/Serial.h"
#include "API/Memory.h"
#include "API/Graphics.h"
#include "API/File.h"
#include "API/Window.h"
#include "API/Input.h"
#include "API/Serial.h"
#include "Unicode/UTF8/UTF8.h"
#include "Crypto/Crypto.h"

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#define STBTT_malloc(x,u)  ((void)(u),malloc(x))
#define STBTT_free(x,u)    ((void)(u),free(x))
#define STBTT_fmod(x,y)    fmod(x,y)
#include "stb_truetype.h"

static uint32_t *g_fb_snapshot = NULL;
static uint32_t g_fb_snapshot_pixels = 0;

static int32_t spawn_with_fallbacks(const char *const *paths, uint32_t path_count) {
    if (paths == 0 || path_count == 0) {
        return -1;
    }

    for (uint32_t i = 0; i < path_count; ++i) {
        const char *path = paths[i];
        if (path == 0 || path[0] == '\0') {
            continue;
        }

        int32_t pid = process_spawn(path);
        if (pid >= 0) {
            return pid;
        }
    }
    return -1;
}

static uint8_t g_font_buffer[6 * 1024 * 1024];
static stbtt_fontinfo g_font;
static int g_font_loaded = 0;

static int load_font(const char *path)
{
    int32_t fd = file_open(path, 0);
    if (fd < 0) {
        return -1;
    }

    int64_t size = file_read(fd, g_font_buffer, sizeof(g_font_buffer));
    file_close(fd);

    if (size <= 0) {
        return -1;
    }

    int offset = stbtt_GetFontOffsetForIndex(g_font_buffer, 0);

    if (offset < 0) {
        return -1;
    }

    if (!stbtt_InitFont(&g_font, g_font_buffer, offset)) {
        return -1;
    }

    g_font_loaded = 1;
    return 0;
}

static void draw_gradient_background(
    uint32_t top_color,
    uint32_t bottom_color
)
{
    int width = get_display_width();
    int height = get_display_height();

    uint8_t top_r = (top_color >> 16) & 0xFF;
    uint8_t top_g = (top_color >> 8) & 0xFF;
    uint8_t top_b = top_color & 0xFF;

    uint8_t bottom_r = (bottom_color >> 16) & 0xFF;
    uint8_t bottom_g = (bottom_color >> 8) & 0xFF;
    uint8_t bottom_b = bottom_color & 0xFF;

    for (int y = 0; y < height; ++y) {
        float t = (float)y / (float)(height - 1);

        uint8_t r = (uint8_t)(top_r + (bottom_r - top_r) * t);
        uint8_t g = (uint8_t)(top_g + (bottom_g - top_g) * t);
        uint8_t b = (uint8_t)(top_b + (bottom_b - top_b) * t);

        uint32_t color = (r << 16) | (g << 8) | b;

        draw_fill_rect(0, y, width, 1, color);
    }
}

static uint32_t blend(uint32_t src, uint32_t dst, uint8_t alpha)
{
    uint8_t sr = (src >> 16) & 0xFF;
    uint8_t sg = (src >> 8) & 0xFF;
    uint8_t sb = src & 0xFF;

    uint8_t dr = (dst >> 16) & 0xFF;
    uint8_t dg = (dst >> 8) & 0xFF;
    uint8_t db = dst & 0xFF;

    uint8_t r = (sr * alpha + dr * (255 - alpha)) / 255;
    uint8_t g = (sg * alpha + dg * (255 - alpha)) / 255;
    uint8_t b = (sb * alpha + db * (255 - alpha)) / 255;

    return (r << 16) | (g << 8) | b;
}

static void draw_char(int x, int y, utf8_codepoint_t cp, float scale, uint32_t color)
{
    if (!g_font_loaded) {
        return;
    }

    int width, height, xoff, yoff;

    uint8_t *bitmap = stbtt_GetCodepointBitmap(
        &g_font,
        scale,
        scale,
        cp,
        &width,
        &height,
        &xoff,
        &yoff
    );

    if (!bitmap) {
        return;
    }

    uint32_t dst = 0x000000;

    for (int py = 0; py < height; ++py) {
        for (int px = 0; px < width; ++px) {
            uint8_t alpha = bitmap[py * width + px];

            if (alpha == 0) continue;

            uint32_t src = color;

            draw_pixel(
                x + xoff + px,
                y + yoff + py,
                blend(src, dst, alpha)
            );
        }
    }

    stbtt_FreeBitmap(bitmap, 0);
}


static int get_text_width(const char *text, float scale)
{
    const char *p = text;
    const char *end = text + strlen(text);

    int width = 0;

    utf8_codepoint_t prev = 0;
    int has_prev = 0;

    while (p < end) {
        utf8_codepoint_t cp;

        if (utf8_next(&p, end, &cp) != 0) {
            continue;
        }

        if (has_prev) {
            int kern = stbtt_GetCodepointKernAdvance(&g_font, prev, cp);
            width += (int)(kern * scale);
        }

        int advance, lsb;
        stbtt_GetCodepointHMetrics(&g_font, cp, &advance, &lsb);

        width += (int)(advance * scale);

        prev = cp;
        has_prev = 1;
    }

    return width;
}

static void draw_text(int x, int y, const char *text, float scale, uint32_t color)
{
    if (!g_font_loaded || !text) {
        return;
    }

    const char *p = text;
    const char *end = text + strlen(text);

    int pen_x = x;

    utf8_codepoint_t prev = 0;
    int has_prev = 0;

    while (p < end) {
        utf8_codepoint_t cp;

        utf8_status_t st = utf8_next(&p, end, &cp);
        if (st != 0) {
            continue;
        }

        if (cp == ' ') {
            int advance, lsb;
            stbtt_GetCodepointHMetrics(&g_font, ' ', &advance, &lsb);
            pen_x += (int)(advance * scale);
            prev = 0;
            has_prev = 0;
            continue;
        }

        if (has_prev) {
            int kern = stbtt_GetCodepointKernAdvance(&g_font, prev, cp);
            pen_x += (int)(kern * scale);
        }

        draw_char(pen_x, y, cp, scale, color);

        int advance, lsb;
        stbtt_GetCodepointHMetrics(&g_font, cp, &advance, &lsb);

        pen_x += (int)(advance * scale);

        prev = cp;
        has_prev = 1;
    }

    draw_present();
}

static void draw_text_centered(const char *text, int ypos, float pixel_height, uint32_t color)
{
    if (!g_font_loaded || !text) {
        return;
    }

    float scale = stbtt_ScaleForPixelHeight(&g_font, pixel_height);

    int width = get_text_width(text, scale);

    int x = (get_display_width() - width) / 2;
    int y = (get_display_height() / 2) + (int)ypos;

    draw_text(x, y, text, scale, color);
}

static void draw_login_screen(const char *title, const char *prompt, const char *value, bool hidden, const char *status)
{
    draw_gradient_background(0x000000, 0x11223F);
    draw_text_centered(title, -80, 38.0f, 0xFFFFFF);
    draw_text_centered(prompt, -20, 24.0f, 0xC0D8FF);

    char display_value[256];
    if (hidden) {
        size_t len = strlen(value);
        if (len > sizeof(display_value) - 1) {
            len = sizeof(display_value) - 1;
        }
        for (size_t i = 0; i < len; ++i) {
            display_value[i] = '*';
        }
        display_value[len] = '\0';
    } else {
        os_strcpy_s(display_value, sizeof(display_value), value);
    }

    draw_text_centered(display_value, 40, 28.0f, 0xFFFFFF);
    if (status && status[0]) {
        draw_text_centered(status, 100, 20.0f, 0xFF8080);
    }
    draw_present();
}

#define USER_DB_FILE "/Userland/users.db"

static int prompt_user_input(const char *title, const char *prompt, char *out, size_t out_size, bool hidden)
{
    size_t pos = 0;
    out[0] = '\0';
    draw_login_screen(title, prompt, out, hidden, "");

    while (1) {
        input_keyboard_event_t ev;
        if (input_read_keyboard(&ev) < 0) {
            process_yield();
            continue;
        }

        if (!ev.pressed) {
            continue;
        }

        if (ev.ascii == '\r' || ev.ascii == '\n') {
            return 1;
        }

        if (ev.ascii == 8 || ev.ascii == 127) {
            if (pos > 0) {
                pos -= 1;
                out[pos] = '\0';
                draw_login_screen(title, prompt, out, hidden, "");
            }
            continue;
        }

        if (ev.ascii >= 32 && ev.ascii < 127 && pos + 1 < out_size) {
            out[pos++] = (char)ev.ascii;
            out[pos] = '\0';
            draw_login_screen(title, prompt, out, hidden, "");
        }
    }
}

static bool user_db_exists(void)
{
    file_stat_t stat;
    if (file_stat(USER_DB_FILE, &stat) < 0) {
        return false;
    }
    return stat.exists != 0;
}

static bool parse_user_record(const char *line, char *username, size_t username_size,
                              char *salt, size_t salt_size, char *hash, size_t hash_size)
{
    const char *first_colon = strchr(line, ':');
    if (!first_colon) {
        return false;
    }

    const char *second_colon = strchr(first_colon + 1, ':');
    if (!second_colon) {
        return false;
    }

    size_t name_len = (size_t)(first_colon - line);
    size_t salt_len = (size_t)(second_colon - first_colon - 1);
    size_t hash_len = strlen(second_colon + 1);
    if (hash_len > 0 && (second_colon[1 + hash_len - 1] == '\n' || second_colon[1 + hash_len - 1] == '\r')) {
        hash_len -= 1;
    }

    if (name_len >= username_size || salt_len >= salt_size || hash_len >= hash_size) {
        return false;
    }

    memcpy(username, line, name_len);
    username[name_len] = '\0';
    memcpy(salt, first_colon + 1, salt_len);
    salt[salt_len] = '\0';
    memcpy(hash, second_colon + 1, hash_len);
    hash[hash_len] = '\0';
    return true;
}

static bool user_db_lookup(const char *username, char *salt, size_t salt_size, char *hash, size_t hash_size)
{
    int32_t fd = file_open(USER_DB_FILE, 0);
    if (fd < 0) {
        return false;
    }

    char buffer[4096];
    int64_t read_len = file_read(fd, buffer, sizeof(buffer) - 1);
    file_close(fd);
    if (read_len <= 0) {
        return false;
    }
    buffer[read_len] = '\0';

    char *line = buffer;
    while (*line) {
        char *newline = strchr(line, '\n');
        if (newline) {
            *newline = '\0';
        }

        char record_user[33];
        char record_salt[17];
        char record_hash[65];
        if (parse_user_record(line, record_user, sizeof(record_user), record_salt, sizeof(record_salt), record_hash, sizeof(record_hash))) {
            if (strcmp(record_user, username) == 0) {
                os_strcpy_s(salt, salt_size, record_salt);
                os_strcpy_s(hash, hash_size, record_hash);
                return true;
            }
        }

        if (!newline) {
            break;
        }
        line = newline + 1;
    }

    return false;
}

static bool user_db_add(const char *username, const char *salt, const char *hash)
{
    int32_t fd = file_open(USER_DB_FILE, 1);
    if (fd < 0) {
        fd = file_creat(USER_DB_FILE);
        if (fd < 0) {
            return false;
        }
    }

    file_seek(fd, 0, 2);
    char line[128];
    int len = snprintf(line, sizeof(line), "%s:%s:%s\n", username, salt, hash);
    if (len <= 0 || (size_t)len >= sizeof(line)) {
        file_close(fd);
        return false;
    }

    if (file_write(fd, line, (uint64_t)len) != len) {
        file_close(fd);
        return false;
    }

    file_close(fd);
    return true;
}

static bool user_login_create_user(char *username_out, size_t username_out_size)
{
    char username[33];
    char password[129];
    char confirm[129];
    char status[128];

    while (1) {
        status[0] = '\0';
        if (!prompt_user_input("新規ユーザー登録", "ユーザー名を入力してください", username, sizeof(username), false)) {
            return false;
        }

        if (username[0] == '\0' || strchr(username, ':') || strchr(username, '\n') || strchr(username, '\r')) {
            os_strcpy_s(status, sizeof(status), "ユーザー名に無効な文字が含まれています。もう一度入力してください。");
            draw_login_screen("新規ユーザー登録", "ユーザー名を入力してください", username, false, status);
            continue;
        }

        if (user_db_lookup(username, (char[17]){0}, sizeof(char[17]), (char[65]){0}, sizeof(char[65]))) {
            os_strcpy_s(status, sizeof(status), "このユーザー名は既に使われています。別の名前を入力してください。");
            draw_login_screen("新規ユーザー登録", "ユーザー名を入力してください", username, false, status);
            continue;
        }

        if (!prompt_user_input("新規ユーザー登録", "パスワードを入力してください", password, sizeof(password), true)) {
            return false;
        }

        if (!prompt_user_input("新規ユーザー登録", "パスワードを再入力してください", confirm, sizeof(confirm), true)) {
            return false;
        }

        if (strcmp(password, confirm) != 0) {
            os_strcpy_s(status, sizeof(status), "パスワードが一致しません。もう一度入力してください。");
            draw_login_screen("新規ユーザー登録", "パスワードを入力してください", password, true, status);
            continue;
        }

        uint8_t salt_bytes[8];
        char salt_hex[17];
        char hash_hex[65];
        crypto_generate_salt(salt_bytes);
        crypto_hex_encode(salt_bytes, sizeof(salt_bytes), salt_hex);
        crypto_hash_password_hex(password, salt_hex, hash_hex);

        if (!user_db_add(username, salt_hex, hash_hex)) {
            os_strcpy_s(status, sizeof(status), "ユーザー登録に失敗しました。もう一度お試しください。");
            draw_login_screen("新規ユーザー登録", "ユーザー名を入力してください", username, false, status);
            continue;
        }

        os_strcpy_s(username_out, username_out_size, username);
        return true;
    }
}

static bool user_login_authenticate(char *username_out, size_t username_out_size)
{
    char username[33];
    char password[129];
    char salt[17];
    char stored_hash[65];
    char computed_hash[65];
    char status[128];

    while (1) {
        status[0] = '\0';
        if (!prompt_user_input("ログイン", "ユーザー名を入力してください", username, sizeof(username), false)) {
            return false;
        }

        if (!user_db_lookup(username, salt, sizeof(salt), stored_hash, sizeof(stored_hash))) {
            os_strcpy_s(status, sizeof(status), "そのユーザーが見つかりません。もう一度入力してください。");
            draw_login_screen("ログイン", "ユーザー名を入力してください", username, false, status);
            continue;
        }

        if (!prompt_user_input("ログイン", "パスワードを入力してください", password, sizeof(password), true)) {
            return false;
        }

        crypto_hash_password_hex(password, salt, computed_hash);
        if (strcmp(stored_hash, computed_hash) == 0) {
            os_strcpy_s(username_out, username_out_size, username);
            return true;
        }

        os_strcpy_s(status, sizeof(status), "パスワードが違います。もう一度入力してください。");
        draw_login_screen("ログイン", "ユーザー名を入力してください", username, false, status);
    }
}

static bool run_user_login_flow(char *current_username, size_t current_username_size)
{
    if (!user_db_exists()) {
        return user_login_create_user(current_username, current_username_size);
    }
    return user_login_authenticate(current_username, current_username_size);
}

static void fade_in(uint32_t duration_ms, uint32_t steps)
{
    uint32_t width  = get_display_width();
    uint32_t height = get_display_height();

    uint32_t *fb = (uint32_t *)sys_get_display_framebuffer();
    if (!fb) {
        return;
    }

    uint32_t pixels = width * height;

    if (!g_fb_snapshot || g_fb_snapshot_pixels != pixels) {
        if (g_fb_snapshot) {
            free(g_fb_snapshot);
        }

        g_fb_snapshot = (uint32_t *)malloc(pixels * sizeof(uint32_t));

        if (!g_fb_snapshot) {
            return;
        }

        g_fb_snapshot_pixels = pixels;
    }

    memcpy(g_fb_snapshot, fb, pixels * sizeof(uint32_t));

    uint32_t delay = duration_ms / steps;

    for (uint32_t step = 0; step <= steps; ++step) {
        float t = (float)step / (float)steps;

        for (uint32_t i = 0; i < pixels; ++i) {
            uint32_t src = g_fb_snapshot[i];

            uint8_t r = (src >> 16) & 0xFF;
            uint8_t g = (src >> 8) & 0xFF;
            uint8_t b = src & 0xFF;

            r = (uint8_t)(r * t);
            g = (uint8_t)(g * t);
            b = (uint8_t)(b * t);

            fb[i] = (r << 16) | (g << 8) | b;
        }

        draw_present();
        sleep_ms(delay);
    }
}

#define BOOT_COUNT_FILE "/Userland/boot_count.txt"

static int read_boot_count(void)
{
    int32_t fd = file_open(BOOT_COUNT_FILE, 0);
    if (fd < 0) {
        return 0;
    }

    char buf[32];
    memset(buf, 0, sizeof(buf));

    int64_t n = file_read(fd, buf, sizeof(buf) - 1);
    file_close(fd);

    if (n <= 0) {
        return 0;
    }

    int count = 0;

    for (int i = 0; buf[i]; ++i) {
        if (buf[i] >= '0' && buf[i] <= '9') {
            count = (count * 10) + (buf[i] - '0');
        }
    }

    return count;
}

static void write_boot_count(int count)
{
    int32_t fd = file_creat(BOOT_COUNT_FILE);
    if (fd < 0) {
        return;
    }

    char buf[32];
    memset(buf, 0, sizeof(buf));

    int temp = count;
    int len = 0;

    if (temp == 0) {
        buf[len++] = '0';
    } else {
        char rev[32];
        int rev_len = 0;

        while (temp > 0) {
            rev[rev_len++] = '0' + (temp % 10);
            temp /= 10;
        }

        for (int i = rev_len - 1; i >= 0; --i) {
            buf[len++] = rev[i];
        }
    }

    file_write(fd, buf, len);
    file_close(fd);
}

void _start(void)
{
    int boot_count = read_boot_count();

    bool first_boot = (boot_count == 0);

    boot_count++;

    write_boot_count(boot_count);

    draw_gradient_background(0x000000, 0x11223F);

    load_font("/Userland/SystemApps/com_ImplusOS_windowmanager/Resource/Fonts/NotoSansJP-Regular.ttf");

    if (first_boot) {
        draw_text_centered("はじめまして。サービスを開始中です。", 0, 42.0f, 0xFFFFFF);
    } else {
        draw_text_centered("おかえりなさい。サービスを開始中です。", 0, 42.0f, 0xFFFFFF);
    }

    char current_user[33] = {0};
    if (!run_user_login_flow(current_user, sizeof(current_user))) {
        draw_text_centered("ログインに失敗しました。再起動してください。", 80, 20.0f, 0xFF8080);
        draw_present();
        while (1) {
            process_yield();
        }
    }

    char welcome_text[128];
    memset(welcome_text, 0, sizeof(welcome_text));
    os_strcpy_s(welcome_text, sizeof(welcome_text), "ようこそ、");
    os_strcat_s(welcome_text, sizeof(welcome_text), current_user);
    os_strcat_s(welcome_text, sizeof(welcome_text), " さん。システムを起動しています...");
    draw_text_centered(welcome_text, 80, 20.0f, 0xFFFFFF);

    char boot_msg[128];
    memset(boot_msg, 0, sizeof(boot_msg));

    os_strcpy_s(boot_msg, sizeof(boot_msg), "今回は、");

    char num_buf[32];
    memset(num_buf, 0, sizeof(num_buf));

    int temp = boot_count;
    int len = 0;

    if (temp == 0) {
        num_buf[len++] = '0';
    } else {
        char rev[32];
        int rev_len = 0;

        while (temp > 0) {
            rev[rev_len++] = '0' + (temp % 10);
            temp /= 10;
        }

        for (int i = rev_len - 1; i >= 0; --i) {
            num_buf[len++] = rev[i];
        }
    }

    num_buf[len] = '\0';

    os_strcat_s(boot_msg, sizeof(boot_msg), num_buf);
    os_strcat_s(boot_msg, sizeof(boot_msg), "回目の起動です。");

    draw_text_centered(boot_msg, 100, 25.0f, 0xFFFFFF);

    draw_present();

    fade_in(1200, 32);

    static const char *const com_ImplusOS_windowmanager[] = {
        "/Userland/SystemApps/com_ImplusOS_windowmanager/com_ImplusOS_windowmanager.ELF",
    };

    static const char *const com_ImplusOS_procman[] = {
        "/Userland/UserApps/com_ImplusOS_procman/com_ImplusOS_procman.ELF",
    };

    spawn_with_fallbacks(com_ImplusOS_windowmanager, sizeof(com_ImplusOS_windowmanager) / sizeof(com_ImplusOS_windowmanager[0]));
    process_yield();
    spawn_with_fallbacks(com_ImplusOS_procman, sizeof(com_ImplusOS_procman) / sizeof(com_ImplusOS_procman[0]));
    process_yield();
    
    
    int32_t wm_pid = -1;
    for (int i = 0; i < 50; i++) {
        wm_pid = window_get_wm_pid();
        if (wm_pid >= 0) break;
        sleep_ms(100);
    }
    
    if (wm_pid >= 0) {
        sleep_ms(500); 
        window_show_notification("System", "Welcome to ImplusOS!");
    }

    while (1) {
        process_yield();
    }
}