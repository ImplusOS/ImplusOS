#include <stdint.h>
#include <string.h>
#include "implus_drm.h"
#include "implus_evdev.h"
#include "implus_unix_socket.h"
#include "implus_clock.h"

#define FB_WIDTH  1024
#define FB_HEIGHT 768
#define FB_BPP    32

static int g_drm_fd;
static uint32_t g_fb_handle, g_fb_pitch, g_fb_id;
static uint64_t g_fb_size;
static uint32_t *g_fb;
static int g_keyboard_fd, g_mouse_fd;
static int g_wl_server_fd;

static void pixel(uint32_t x, uint32_t y, uint32_t color) {
    if (x < FB_WIDTH && y < FB_HEIGHT)
        g_fb[y * (g_fb_pitch / 4) + x] = color;
}

static void fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) {
    for (uint32_t j = y; j < y + h && j < FB_HEIGHT; j++)
        for (uint32_t i = x; i < x + w && i < FB_WIDTH; i++)
            pixel(i, j, color);
}

static void draw_gradient_bg(void) {
    for (uint32_t y = 0; y < FB_HEIGHT; y++) {
        uint32_t r = 20 + (y * 40 / FB_HEIGHT);
        uint32_t g = 30 + (y * 60 / FB_HEIGHT);
        uint32_t b = 80 + (y * 100 / FB_HEIGHT);
        for (uint32_t x = 0; x < FB_WIDTH; x++)
            pixel(x, y, (r << 16) | (g << 8) | b);
    }
}

static int init_drm(void) {
    g_drm_fd = implus_drm_open();
    if (g_drm_fd < 0) return -1;

    struct { uint32_t handle, width, height, bpp, pitch; uint64_t size; } cd;
    memset(&cd, 0, sizeof(cd));
    cd.width = FB_WIDTH; cd.height = FB_HEIGHT; cd.bpp = FB_BPP;
    if (implus_drm_ioctl(g_drm_fd, 0xB2, &cd) != 0) return -1;
    g_fb_handle = cd.handle; g_fb_pitch = cd.pitch; g_fb_size = cd.size;

    struct { uint32_t handle, pad; uint64_t offset; } md;
    md.handle = g_fb_handle; md.pad = 0; md.offset = 0;
    if (implus_drm_ioctl(g_drm_fd, 0xB3, &md) != 0) return -1;
    g_fb = (uint32_t*)implus_drm_mmap(g_drm_fd, md.offset, g_fb_size);
    if (!g_fb) return -1;

    struct { uint32_t fb_id, width, height, pitch, bpp, depth, handle; } fb;
    memset(&fb, 0, sizeof(fb));
    fb.width = FB_WIDTH; fb.height = FB_HEIGHT; fb.pitch = g_fb_pitch;
    fb.bpp = FB_BPP; fb.depth = 24; fb.handle = g_fb_handle;
    if (implus_drm_ioctl(g_drm_fd, 0xAE, &fb) != 0) return -1;
    g_fb_id = fb.fb_id;

    return 0;
}

static void flip(void) {
    struct { uint32_t crtc_id, fb_id, flags, reserved; uint64_t user_data; } pf;
    pf.crtc_id = 1; pf.fb_id = g_fb_id; pf.flags = 0; pf.reserved = 0; pf.user_data = 0;
    implus_drm_ioctl(g_drm_fd, 0xB0, &pf);
}

static int init_input(void) {
    g_keyboard_fd = implus_evdev_open("/dev/input/event0");
    g_mouse_fd = implus_evdev_open("/dev/input/event1");
    return (g_keyboard_fd >= 0 || g_mouse_fd >= 0) ? 0 : -1;
}

static int init_wayland_server(void) {
    g_wl_server_fd = implus_unix_socket(1);
    if (g_wl_server_fd < 0) return -1;
    implus_unix_bind(g_wl_server_fd, "/run/wayland-0");
    implus_unix_listen(g_wl_server_fd, 4);
    return 0;
}

static uint32_t cursor_x = FB_WIDTH / 2;
static uint32_t cursor_y = FB_HEIGHT / 2;

static void process_input(void) {
    struct input_event ev[16];
    long n;
    if (g_mouse_fd >= 0) {
        n = implus_evdev_read(g_mouse_fd, ev, sizeof(ev));
        int count = (int)(n / (long)sizeof(struct input_event));
        for (int i = 0; i < count; i++) {
            if (ev[i].type == 2 && ev[i].code == 0) {
                int32_t nx = (int32_t)cursor_x + ev[i].value;
                if (nx < 0) nx = 0; if (nx >= (int32_t)FB_WIDTH) nx = (int32_t)FB_WIDTH - 1;
                cursor_x = (uint32_t)nx;
            }
            if (ev[i].type == 2 && ev[i].code == 1) {
                int32_t ny = (int32_t)cursor_y + ev[i].value;
                if (ny < 0) ny = 0; if (ny >= (int32_t)FB_HEIGHT) ny = (int32_t)FB_HEIGHT - 1;
                cursor_y = (uint32_t)ny;
            }
        }
    }
}

static void draw_cursor(uint32_t x, uint32_t y) {
    fill_rect(x, y, 12, 16, 0xFFFFFFFF);
    fill_rect(x+1, y+1, 10, 14, 0xFF000000);
    fill_rect(x+2, y+2, 8, 12, 0xFFFFFFFF);
}

static void draw_taskbar(void) {
    fill_rect(0, FB_HEIGHT - 32, FB_WIDTH, 32, 0xFF2D2D2D);
    fill_rect(4, FB_HEIGHT - 28, 80, 24, 0xFF4488FF);
}

int main(void) {
    if (init_drm() != 0) return 1;
    init_input();
    init_wayland_server();

    draw_gradient_bg();
    fill_rect(100, 80, 500, 400, 0xFFE8E8E8);
    fill_rect(100, 80, 500, 28, 0xFF3366CC);
    fill_rect(120, 130, 460, 200, 0xFFFFFFFF);
    draw_taskbar();
    draw_cursor(cursor_x, cursor_y);
    flip();

    for (;;) {
        process_input();
        draw_gradient_bg();
        fill_rect(100, 80, 500, 400, 0xFFE8E8E8);
        fill_rect(100, 80, 500, 28, 0xFF3366CC);
        fill_rect(120, 130, 460, 200, 0xFFFFFFFF);
        draw_taskbar();
        draw_cursor(cursor_x, cursor_y);
        flip();
    }

    return 0;
}
