#include <png.h>
#include <File.h>
#include <Graphics.h>
#include <Input.h>
#include <Process.h>
#include <Window.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PNGTEST_WIN_W 720u
#define PNGTEST_WIN_H 520u
#define PNGTEST_DEFAULT_PATH "/Userland/UserApps/com_ImplusOS_pngTest/Resource/Test.png"
#define PNGTEST_MAX_FILE_BYTES (16u * 1024u * 1024u)

#define COLOR_BG      0xFF111820u
#define COLOR_HEADER  0xFF1B3140u
#define COLOR_PANEL   0xFF172431u
#define COLOR_BORDER  0xFF355466u
#define COLOR_TEXT    0xFFEAF8FFu
#define COLOR_DIM     0xFFA8BFCAu
#define COLOR_WARN    0xFFFFC46Bu

typedef struct {
    uint32_t width;
    uint32_t height;
    uint8_t *rgba;
} png_view_image_t;

static window_id_t g_win = 0;
static char g_path[512];
static char g_status[160];
static png_view_image_t g_image;

static void set_status(const char *status)
{
    if (!status) status = "";
    strncpy(g_status, status, sizeof(g_status) - 1u);
    g_status[sizeof(g_status) - 1u] = '\0';
}

static void free_image(png_view_image_t *image)
{
    if (!image) return;
    free(image->rgba);
    image->rgba = NULL;
    image->width = 0u;
    image->height = 0u;
}

static int read_file_to_memory(const char *path, uint8_t **out_data,
                               size_t *out_size)
{
    file_stat_t stat;
    if (!path || !out_data || !out_size) {
        set_status("Invalid file request.");
        return -1;
    }
    *out_data = NULL;
    *out_size = 0u;

    if (file_stat(path, &stat) < 0 || !stat.exists) {
        snprintf(g_status, sizeof(g_status), "File not found: %s", path);
        return -1;
    }
    if (stat.is_dir) {
        snprintf(g_status, sizeof(g_status), "Path is a directory: %s", path);
        return -1;
    }
    if (stat.size == 0u || stat.size > PNGTEST_MAX_FILE_BYTES) {
        snprintf(g_status, sizeof(g_status), "Unsupported file size: %u bytes",
                 (unsigned)stat.size);
        return -1;
    }

    uint8_t *data = (uint8_t *)malloc((size_t)stat.size);
    if (!data) {
        set_status("Unable to allocate file buffer.");
        return -1;
    }

    int32_t fd = file_open(path, 0);
    if (fd < 0) {
        free(data);
        snprintf(g_status, sizeof(g_status), "Unable to open: %s", path);
        return -1;
    }

    size_t total = 0u;
    while (total < (size_t)stat.size) {
        int64_t n = file_read(fd, data + total,
                              (uint64_t)((size_t)stat.size - total));
        if (n <= 0) {
            file_close(fd);
            free(data);
            set_status("Unable to read PNG file.");
            return -1;
        }
        total += (size_t)n;
    }
    file_close(fd);

    *out_data = data;
    *out_size = total;
    return 0;
}

static int decode_png_from_memory(const uint8_t *data, size_t size,
                                  png_view_image_t *out_image)
{
    png_image image;
    memset(&image, 0, sizeof(image));
    image.version = PNG_IMAGE_VERSION;

    if (!png_image_begin_read_from_memory(&image, data, size)) {
        snprintf(g_status, sizeof(g_status), "PNG header error: %s",
                 image.message);
        return -1;
    }

    image.format = PNG_FORMAT_RGBA;
    size_t image_size = PNG_IMAGE_SIZE(image);
    uint8_t *pixels = (uint8_t *)malloc(image_size);
    if (!pixels) {
        png_image_free(&image);
        set_status("Unable to allocate PNG pixel buffer.");
        return -1;
    }

    if (!png_image_finish_read(&image, NULL, pixels, 0, NULL)) {
        snprintf(g_status, sizeof(g_status), "PNG decode error: %s",
                 image.message);
        free(pixels);
        png_image_free(&image);
        return -1;
    }

    free_image(out_image);
    out_image->width = image.width;
    out_image->height = image.height;
    out_image->rgba = pixels;
    png_image_free(&image);

    snprintf(g_status, sizeof(g_status), "Loaded %u x %u PNG from %s",
             (unsigned)out_image->width, (unsigned)out_image->height, g_path);
    return 0;
}

static int load_png(const char *path)
{
    uint8_t *data = NULL;
    size_t size = 0u;

    if (read_file_to_memory(path, &data, &size) < 0) {
        free_image(&g_image);
        return -1;
    }

    int result = decode_png_from_memory(data, size, &g_image);
    free(data);
    return result;
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

static void draw_png_image(uint32_t *fb, uint32_t fb_w, uint32_t fb_h,
                           uint32_t area_x, uint32_t area_y,
                           uint32_t area_w, uint32_t area_h)
{
    if (!fb || !g_image.rgba || g_image.width == 0u || g_image.height == 0u)
        return;

    uint32_t draw_w = g_image.width;
    uint32_t draw_h = g_image.height;
    if (draw_w > area_w || draw_h > area_h) {
        if ((uint64_t)area_w * g_image.height <=
            (uint64_t)area_h * g_image.width) {
            draw_w = area_w;
            draw_h = (uint32_t)(((uint64_t)g_image.height * area_w) /
                                g_image.width);
        } else {
            draw_h = area_h;
            draw_w = (uint32_t)(((uint64_t)g_image.width * area_h) /
                                g_image.height);
        }
        if (draw_w == 0u) draw_w = 1u;
        if (draw_h == 0u) draw_h = 1u;
    }

    uint32_t dst_x = area_x + (area_w - draw_w) / 2u;
    uint32_t dst_y = area_y + (area_h - draw_h) / 2u;

    for (uint32_t y = 0u; y < draw_h; ++y) {
        uint32_t src_y = (uint32_t)(((uint64_t)y * g_image.height) / draw_h);
        if (dst_y + y >= fb_h) break;
        uint32_t *dst = fb + (dst_y + y) * fb_w + dst_x;
        for (uint32_t x = 0u; x < draw_w; ++x) {
            if (dst_x + x >= fb_w) break;
            uint32_t src_x = (uint32_t)(((uint64_t)x * g_image.width) / draw_w);
            const uint8_t *p = g_image.rgba +
                ((size_t)src_y * g_image.width + src_x) * 4u;
            uint32_t bg = dst[x];
            dst[x] = blend_rgba_over(p[0], p[1], p[2], p[3], bg);
        }
    }
}

static void render(void)
{
    uint32_t fb_w = 0u, fb_h = 0u;
    uint32_t *fb = window_get_backing_store(g_win, &fb_w, &fb_h);
    if (!fb || fb_w == 0u || fb_h == 0u) {
        window_clear(g_win);
        window_draw_text(g_win, 18u, 18u, "PNG Viewer", COLOR_TEXT, 20.0f);
        window_draw_text(g_win, 18u, 56u, g_status, COLOR_WARN, 13.0f);
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
        draw_png_image(fb, fb_w, fb_h, area_x, area_y, area_w, area_h);
        fill_rect(fb, fb_w, fb_h, area_x, area_y, area_w, 1u, COLOR_BORDER);
        fill_rect(fb, fb_w, fb_h, area_x, area_y + area_h - 1u, area_w, 1u,
                  COLOR_BORDER);
        fill_rect(fb, fb_w, fb_h, area_x, area_y, 1u, area_h, COLOR_BORDER);
        fill_rect(fb, fb_w, fb_h, area_x + area_w - 1u, area_y, 1u, area_h,
                  COLOR_BORDER);
    }

    uint32_t footer_y = fb_h > 52u ? fb_h - 52u : 0u;
    fill_rect(fb, fb_w, fb_h, 0u, footer_y, fb_w, fb_h - footer_y,
              COLOR_HEADER);
    window_damage(g_win, 0u, 0u, fb_w, fb_h);

    char title[96];
    snprintf(title, sizeof(title), "PNG Viewer  %u x %u",
             (unsigned)g_image.width, (unsigned)g_image.height);
    window_draw_text(g_win, 18u, 16u, title, COLOR_TEXT, 18.0f);
    window_draw_text(g_win, 18u, 40u, g_path, COLOR_DIM, 12.0f);
    if (fb_h > 35u) {
        window_draw_text(g_win, 18u, fb_h - 35u, g_status,
                         g_image.rgba ? COLOR_DIM : COLOR_WARN, 12.0f);
    }
    draw_present();
}

int main(void)
{
    int32_t arg_len = process_get_launch_argument(g_path,
                                                  (uint32_t)sizeof(g_path));
    if (arg_len <= 0) {
        strncpy(g_path, PNGTEST_DEFAULT_PATH, sizeof(g_path) - 1u);
        g_path[sizeof(g_path) - 1u] = '\0';
    }

    g_win = window_create_ex(110u, 80u, PNGTEST_WIN_W, PNGTEST_WIN_H,
                             COLOR_BG, "PNG Viewer");
    if (g_win == 0u) {
        printf("PNG Viewer: failed to create window\n");
        return 1;
    }
    window_subscribe_keyboard(g_win);
    graphics_init(g_win);

    printf("Starting libpng GUI test: %s\n", g_path);
    (void)load_png(g_path);
    render();

    for (;;) {
        input_keyboard_event_t ev;
        if (window_input_keyboard_poll(&ev) > 0 && ev.pressed) {
            if (ev.ascii == 'q' || ev.ascii == 'Q') {
                process_exit(0);
            }
            if (ev.ascii == 'r' || ev.ascii == 'R') {
                (void)load_png(g_path);
                render();
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
