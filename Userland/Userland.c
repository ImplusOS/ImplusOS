#include <stdint.h>
#include <stdlib.h>
#include <math.h>

#include "API/Process.h"
#include "API/Serial.h"
#include "API/Memory.h"
#include "API/Graphics.h"
#include "API/File.h"
#include "API/Serial.h"
#include "Unicode/UTF8/UTF8.h"

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

    static const char *const com_ImplusOS_version[] = {
        "Userland/SystemApps/com_ImplusOS_version/com_ImplusOS_version.ELF",
    };

    static const char *const com_ImplusOS_windowmanager[] = {
        "Userland/SystemApps/com_ImplusOS_windowmanager/com_ImplusOS_windowmanager.ELF",
    };

    static const char *const com_ImplusOS_shell[] = {
        "Userland/SystemApps/com_ImplusOS_shell/com_ImplusOS_shell.ELF",
    };

    static const char *const com_ImplusOS_clock[] = {
        "Userland/UserApps/com_ImplusOS_clock/com_ImplusOS_clock.ELF",
    };

    static const char *const com_ImplusOS_editor[] = {
        "Userland/UserApps/com_ImplusOS_editor/com_ImplusOS_editor.ELF",
    };

    static const char *const com_ImplusOS_exampleApp[] = {
        "Userland/UserApps/com_ImplusOS_exampleApp/com_ImplusOS_exampleApp.ELF",
    };

    static const char *const com_ImplusOS_filemanager[] = {
        "Userland/UserApps/com_ImplusOS_filemanager/com_ImplusOS_filemanager.ELF",
    };

    static const char *const com_ImplusOS_vm[] = {
        "Userland/UserApps/com_ImplusOS_vm/com_ImplusOS_vm.ELF",
    };

    spawn_with_fallbacks(com_ImplusOS_windowmanager, sizeof(com_ImplusOS_windowmanager) / sizeof(com_ImplusOS_windowmanager[0]));
    process_yield();

    spawn_with_fallbacks(com_ImplusOS_shell, sizeof(com_ImplusOS_shell) / sizeof(com_ImplusOS_shell[0]));
    process_yield();

    spawn_with_fallbacks(com_ImplusOS_version, sizeof(com_ImplusOS_version) / sizeof(com_ImplusOS_version[0]));
    process_yield();

    spawn_with_fallbacks(com_ImplusOS_editor, sizeof(com_ImplusOS_editor) / sizeof(com_ImplusOS_editor[0]));
    process_yield();

    spawn_with_fallbacks(com_ImplusOS_exampleApp, sizeof(com_ImplusOS_exampleApp) / sizeof(com_ImplusOS_exampleApp[0]));
    process_yield();

    spawn_with_fallbacks(com_ImplusOS_filemanager, sizeof(com_ImplusOS_filemanager) / sizeof(com_ImplusOS_filemanager[0]));
    process_yield();

    spawn_with_fallbacks(com_ImplusOS_vm, sizeof(com_ImplusOS_vm) / sizeof(com_ImplusOS_vm[0]));
    process_yield();

    while (1) {
        process_yield();
    }
}