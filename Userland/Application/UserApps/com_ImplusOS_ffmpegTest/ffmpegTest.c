#include <FFmpeg.h>
#include <File.h>
#include <Graphics.h>
#include <Process.h>
#include <Window.h>
#include <png.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FFMPEGTEST_DEFAULT_INPUT "/Userland/UserApps/com_ImplusOS_ffmpegTest/Resource/throbber.avi"
#define FFMPEGTEST_OUTPUT_PATH   "/tmp/com_ImplusOS_ffmpegTest_thumbnail.png"
#define FFMPEGTEST_MAX_WIDTH     320u
#define FFMPEGTEST_MAX_HEIGHT    180u
#define FFMPEGTEST_WIN_W         720u
#define FFMPEGTEST_WIN_H         420u

#define COLOR_BG      0xFF121A23u
#define COLOR_HEADER  0xFF203040u
#define COLOR_PANEL   0xFF172331u
#define COLOR_BORDER  0xFF35586Au
#define COLOR_TEXT    0xFFEAF8FFu
#define COLOR_DIM     0xFFA9BFCAu
#define COLOR_WARN    0xFFFFC46Bu

static window_id_t g_win = 0u;
static char g_status[160];

static void set_status(const char *status)
{
    if (!status) status = "";
    strncpy(g_status, status, sizeof(g_status) - 1u);
    g_status[sizeof(g_status) - 1u] = '\0';
}

static uint32_t blend_rgba_over(uint8_t r, uint8_t g, uint8_t b, uint8_t a,
                                uint32_t bg)
{
    uint32_t br = (bg >> 16) & 0xFFu;
    uint32_t bgc = (bg >> 8) & 0xFFu;
    uint32_t bb = bg & 0xFFu;
    uint32_t ia = 255u - (uint32_t)a;
    uint32_t or = (((uint32_t)r * (uint32_t)a) + br * ia) / 255u;
    uint32_t og = (((uint32_t)g * (uint32_t)a) + bgc * ia) / 255u;
    uint32_t ob = (((uint32_t)b * (uint32_t)a) + bb * ia) / 255u;
    return 0xFF000000u | (or << 16) | (og << 8) | ob;
}

static void fill_rect(uint32_t *fb, uint32_t fb_w, uint32_t fb_h,
                      uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                      uint32_t color)
{
    if (!fb || x >= fb_w || y >= fb_h) return;
    if (w > fb_w - x) w = fb_w - x;
    if (h > fb_h - y) h = fb_h - y;
    for (uint32_t row = 0u; row < h; ++row) {
        uint32_t *dst = fb + (y + row) * fb_w + x;
        for (uint32_t col = 0u; col < w; ++col) {
            dst[col] = color;
        }
    }
}

static void draw_checker(uint32_t *fb, uint32_t fb_w, uint32_t fb_h,
                         uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
    const uint32_t c0 = 0xFFE8EEF2u;
    const uint32_t c1 = 0xFFD1DCE3u;
    if (!fb || x >= fb_w || y >= fb_h) return;
    if (w > fb_w - x) w = fb_w - x;
    if (h > fb_h - y) h = fb_h - y;
    for (uint32_t row = 0u; row < h; ++row) {
        uint32_t *dst = fb + (y + row) * fb_w + x;
        for (uint32_t col = 0u; col < w; ++col) {
            dst[col] = (((row >> 4) + (col >> 4)) & 1u) ? c0 : c1;
        }
    }
}

static void draw_thumbnail(uint32_t *fb, uint32_t fb_w, uint32_t fb_h,
                           uint32_t area_x, uint32_t area_y,
                           uint32_t area_w, uint32_t area_h,
                           const ffmpeg_rgba_image_t *image)
{
    if (!fb || !image || !image->rgba || image->width == 0u || image->height == 0u)
        return;

    uint32_t draw_w = image->width;
    uint32_t draw_h = image->height;
    if (draw_w > area_w || draw_h > area_h) {
        if ((uint64_t)area_w * image->height <=
            (uint64_t)area_h * image->width) {
            draw_w = area_w;
            draw_h = (uint32_t)(((uint64_t)image->height * area_w) / image->width);
        } else {
            draw_h = area_h;
            draw_w = (uint32_t)(((uint64_t)image->width * area_h) / image->height);
        }
        if (draw_w == 0u) draw_w = 1u;
        if (draw_h == 0u) draw_h = 1u;
    }

    uint32_t dst_x = area_x + (area_w - draw_w) / 2u;
    uint32_t dst_y = area_y + (area_h - draw_h) / 2u;

    for (uint32_t y = 0u; y < draw_h; ++y) {
        uint32_t src_y = (uint32_t)(((uint64_t)y * image->height) / draw_h);
        if (dst_y + y >= fb_h) break;
        uint32_t *dst = fb + (dst_y + y) * fb_w + dst_x;
        for (uint32_t x = 0u; x < draw_w; ++x) {
            if (dst_x + x >= fb_w) break;
            uint32_t src_x = (uint32_t)(((uint64_t)x * image->width) / draw_w);
            const uint8_t *p = image->rgba +
                ((size_t)src_y * image->width + src_x) * 4u;
            dst[x] = blend_rgba_over(p[0], p[1], p[2], p[3], dst[x]);
        }
    }
}

static void render_status(const ffmpeg_rgba_image_t *image)
{
    uint32_t fb_w = 0u, fb_h = 0u;
    uint32_t *fb = window_get_backing_store(g_win, &fb_w, &fb_h);
    if (!fb || fb_w == 0u || fb_h == 0u) {
        window_clear(g_win);
        window_draw_text(g_win, 18u, 18u, "FFmpeg Test", COLOR_TEXT, 20.0f);
        window_draw_text(g_win, 18u, 50u, g_status, COLOR_WARN, 13.0f);
        return;
    }

    fill_rect(fb, fb_w, fb_h, 0u, 0u, fb_w, fb_h, COLOR_BG);
    fill_rect(fb, fb_w, fb_h, 0u, 0u, fb_w, 58u, COLOR_HEADER);

    uint32_t panel_w = fb_w > 32u ? fb_w - 32u : 0u;
    uint32_t panel_h = fb_h > 126u ? fb_h - 126u : 0u;
    if (panel_w > 0u && panel_h > 0u) {
        fill_rect(fb, fb_w, fb_h, 16u, 74u, panel_w, panel_h, COLOR_PANEL);
    }

    uint32_t area_x = 26u;
    uint32_t area_y = 84u;
    uint32_t area_w = fb_w > 52u ? fb_w - 52u : 0u;
    uint32_t area_h = fb_h > 152u ? fb_h - 152u : 0u;
    if (area_w > 0u && area_h > 0u) {
        draw_checker(fb, fb_w, fb_h, area_x, area_y, area_w, area_h);
        draw_thumbnail(fb, fb_w, fb_h, area_x, area_y, area_w, area_h, image);
        fill_rect(fb, fb_w, fb_h, area_x, area_y, area_w, 1u, COLOR_BORDER);
        fill_rect(fb, fb_w, fb_h, area_x, area_y + area_h - 1u, area_w, 1u,
                  COLOR_BORDER);
        fill_rect(fb, fb_w, fb_h, area_x, area_y, 1u, area_h, COLOR_BORDER);
        fill_rect(fb, fb_w, fb_h, area_x + area_w - 1u, area_y, 1u, area_h,
                  COLOR_BORDER);
    }

    uint32_t footer_y = fb_h > 52u ? fb_h - 52u : 0u;
    fill_rect(fb, fb_w, fb_h, 0u, footer_y, fb_w, fb_h - footer_y, COLOR_HEADER);
    window_damage(g_win, 0u, 0u, fb_w, fb_h);

    window_draw_text(g_win, 18u, 16u, "FFmpeg Test", COLOR_TEXT, 18.0f);
    window_draw_text(g_win, 18u, 40u, g_status, image ? COLOR_DIM : COLOR_WARN, 12.0f);
    if (image && image->width != 0u) {
        char title[96];
        snprintf(title, sizeof(title), "%u x %u  stride=%u",
                 (unsigned)image->width, (unsigned)image->height,
                 (unsigned)image->stride);
        window_draw_text(g_win, 18u, fb_h - 35u, title, COLOR_DIM, 12.0f);
    }
}

static int write_png_file(const char *path, const ffmpeg_rgba_image_t *image)
{
    if (!path || !image || !image->rgba || image->width == 0u || image->height == 0u) {
        return -1;
    }

    png_image png;
    memset(&png, 0, sizeof(png));
    png.version = PNG_IMAGE_VERSION;
    png.width = image->width;
    png.height = image->height;
    png.format = PNG_FORMAT_RGBA;

    size_t png_size = 0u;
    if (!png_image_write_get_memory_size(png, png_size, 0, image->rgba,
                                         (png_int_32)image->stride, NULL)) {
        return -1;
    }

    uint8_t *png_data = (uint8_t *)malloc(png_size);
    if (!png_data) {
        return -1;
    }

    if (!png_image_write_to_memory(&png, png_data, &png_size, 0, image->rgba,
                                   (png_int_32)image->stride, NULL)) {
        free(png_data);
        return -1;
    }

    int32_t fd = file_creat(path);
    if (fd < 0) {
        free(png_data);
        return -1;
    }

    size_t written = 0u;
    while (written < png_size) {
        int64_t rc = file_write(fd, png_data + written,
                                (uint64_t)(png_size - written));
        if (rc <= 0) {
            file_close(fd);
            free(png_data);
            return -1;
        }
        written += (size_t)rc;
        process_yield();
    }

    file_close(fd);
    free(png_data);
    return 0;
}

int main(void)
{
    static char input_path[512];
    static char error[192];

    int32_t launch_len = process_get_launch_argument(input_path,
                                                     (uint32_t)sizeof(input_path));
    if (launch_len <= 0) {
        strncpy(input_path, FFMPEGTEST_DEFAULT_INPUT, sizeof(input_path) - 1u);
        input_path[sizeof(input_path) - 1u] = '\0';
    }

    g_win = window_create_ex(120u, 90u, FFMPEGTEST_WIN_W, FFMPEGTEST_WIN_H,
                             COLOR_BG, "FFmpeg Test");
    if (g_win == 0u) {
        printf("ffmpegTest: failed to create window\n");
        return 1;
    }
    window_subscribe_keyboard(g_win);
    graphics_init(g_win);

    set_status("decoding thumbnail...");
    render_status(NULL);
    printf("ffmpegTest: input=%s\n", input_path);

    ffmpeg_rgba_image_t image;
    memset(&image, 0, sizeof(image));

    int rc = ffmpeg_decode_thumbnail_from_file(input_path,
                                               FFMPEGTEST_MAX_WIDTH,
                                               FFMPEGTEST_MAX_HEIGHT,
                                               1000000u,
                                               &image,
                                               error,
                                               sizeof(error));
    if (rc < 0) {
        set_status(error[0] ? error : "decode failed");
        render_status(NULL);
        printf("ffmpegTest: decode failed: %s\n", error[0] ? error : "unknown error");
        return 1;
    }

    printf("ffmpegTest: decoded thumbnail %u x %u stride=%u\n",
           (unsigned)image.width, (unsigned)image.height, (unsigned)image.stride);

    if (write_png_file(FFMPEGTEST_OUTPUT_PATH, &image) < 0) {
        set_status("failed to write thumbnail PNG");
        printf("ffmpegTest: failed to write %s\n", FFMPEGTEST_OUTPUT_PATH);
        ffmpeg_free_image(&image);
        render_status(NULL);
        return 1;
    }

    set_status("thumbnail generated successfully");
    printf("ffmpegTest: wrote %s\n", FFMPEGTEST_OUTPUT_PATH);
    render_status(&image);

    for (;;) {
        input_keyboard_event_t ev;
        if (window_input_keyboard_poll(&ev) > 0 && ev.pressed) {
            if (ev.ascii == 'q' || ev.ascii == 'Q' || ev.keycode == 41u) {
                process_exit(0);
            }
            if (ev.ascii == 'r' || ev.ascii == 'R') {
                render_status(&image);
            }
        }
        process_yield();
    }
}

void _start(void)
{
    int32_t status = (int32_t)main();
    process_exit(status);
    for (;;) {
        process_yield();
    }
}
