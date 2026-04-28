#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "../../../API/Process.h"
#include "../../../API/Serial.h"
#include "../../../WaylandPort/adapters/include/implus_drm.h"
#include "../../../WaylandPort/adapters/include/implus_evdev.h"
#include "../../../WaylandPort/adapters/include/implus_unix_socket.h"

#define FB_WIDTH  1024U
#define FB_HEIGHT 768U
#define FB_BPP    32U

#define EV_REL 2U
#define REL_X  0U
#define REL_Y  1U

typedef struct {
    int drm_fd;
    uint32_t fb_handle;
    uint32_t fb_pitch;
    uint32_t fb_id;
    uint64_t fb_size;
    uint32_t *fb;
    int keyboard_fd;
    int mouse_fd;
    int wayland_fd;
    uint32_t cursor_x;
    uint32_t cursor_y;
} tinywl_state_t;

static tinywl_state_t g_state = {
    .drm_fd = -1,
    .keyboard_fd = -1,
    .mouse_fd = -1,
    .wayland_fd = -1,
    .cursor_x = FB_WIDTH / 2U,
    .cursor_y = FB_HEIGHT / 2U,
};

static void log_line(const char *text) {
    serial_write_string(text);
}

static void log_u32(const char *label, uint32_t value) {
    serial_write_string(label);
    serial_write_uint32(value);
    serial_write_string("\n");
}

static void log_u64(const char *label, uint64_t value) {
    serial_write_string(label);
    serial_write_uint64(value);
    serial_write_string("\n");
}

static void log_i32(const char *label, int32_t value) {
    serial_write_string(label);
    if (value < 0) {
        serial_write_string("-");
        serial_write_uint32((uint32_t)(-value));
    } else {
        serial_write_uint32((uint32_t)value);
    }
    serial_write_string("\n");
}

static void log_cursor_position(void) {
    serial_write_string("[tinywl] cursor x=");
    serial_write_uint32(g_state.cursor_x);
    serial_write_string(" y=");
    serial_write_uint32(g_state.cursor_y);
    serial_write_string("\n");
}

static void put_pixel(uint32_t x, uint32_t y, uint32_t color) {
    if (g_state.fb == NULL || x >= FB_WIDTH || y >= FB_HEIGHT) {
        return;
    }
    g_state.fb[y * (g_state.fb_pitch / 4U) + x] = color;
}

static void fill_rect(uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t color) {
    uint32_t max_y = y + height;
    uint32_t max_x = x + width;
    if (max_y > FB_HEIGHT) {
        max_y = FB_HEIGHT;
    }
    if (max_x > FB_WIDTH) {
        max_x = FB_WIDTH;
    }

    for (uint32_t py = y; py < max_y; ++py) {
        for (uint32_t px = x; px < max_x; ++px) {
            put_pixel(px, py, color);
        }
    }
}

static void draw_gradient_background(void) {
    for (uint32_t y = 0; y < FB_HEIGHT; ++y) {
        uint32_t red = 20U + ((y * 40U) / FB_HEIGHT);
        uint32_t green = 30U + ((y * 60U) / FB_HEIGHT);
        uint32_t blue = 80U + ((y * 100U) / FB_HEIGHT);
        uint32_t color = (red << 16) | (green << 8) | blue;
        for (uint32_t x = 0; x < FB_WIDTH; ++x) {
            put_pixel(x, y, color);
        }
    }
}

static int init_drm(void) {
    struct {
        uint32_t handle;
        uint32_t width;
        uint32_t height;
        uint32_t bpp;
        uint32_t pitch;
        uint64_t size;
    } create_dumb;

    struct {
        uint32_t handle;
        uint32_t pad;
        uint64_t offset;
    } map_dumb;

    struct {
        uint32_t fb_id;
        uint32_t width;
        uint32_t height;
        uint32_t pitch;
        uint32_t bpp;
        uint32_t depth;
        uint32_t handle;
    } framebuffer;

    long ioctl_status;

    g_state.drm_fd = implus_drm_open();
    log_i32("[tinywl] drm fd=", g_state.drm_fd);
    if (g_state.drm_fd < 0) {
        log_line("[tinywl] drm open failed\n");
        return -1;
    }

    memset(&create_dumb, 0, sizeof(create_dumb));
    create_dumb.width = FB_WIDTH;
    create_dumb.height = FB_HEIGHT;
    create_dumb.bpp = FB_BPP;
    ioctl_status = implus_drm_ioctl(g_state.drm_fd, 0xB2UL, &create_dumb);
    log_i32("[tinywl] create dumb status=", (int32_t)ioctl_status);
    if (ioctl_status != 0) {
        log_line("[tinywl] DRM_IOCTL_MODE_CREATE_DUMB failed\n");
        return -1;
    }

    g_state.fb_handle = create_dumb.handle;
    g_state.fb_pitch = create_dumb.pitch;
    g_state.fb_size = create_dumb.size;
    log_u32("[tinywl] fb handle=", g_state.fb_handle);
    log_u32("[tinywl] fb pitch=", g_state.fb_pitch);
    log_u64("[tinywl] fb size=", g_state.fb_size);

    memset(&map_dumb, 0, sizeof(map_dumb));
    map_dumb.handle = g_state.fb_handle;
    ioctl_status = implus_drm_ioctl(g_state.drm_fd, 0xB3UL, &map_dumb);
    log_i32("[tinywl] map dumb status=", (int32_t)ioctl_status);
    if (ioctl_status != 0) {
        log_line("[tinywl] DRM_IOCTL_MODE_MAP_DUMB failed\n");
        return -1;
    }
    log_u64("[tinywl] map offset=", map_dumb.offset);

    g_state.fb = (uint32_t *)implus_drm_mmap(g_state.drm_fd, map_dumb.offset, g_state.fb_size);
    log_u64("[tinywl] fb mmap ptr=", (uint64_t)(uintptr_t)g_state.fb);
    if (g_state.fb == NULL) {
        log_line("[tinywl] drm mmap failed\n");
        return -1;
    }

    memset(&framebuffer, 0, sizeof(framebuffer));
    framebuffer.width = FB_WIDTH;
    framebuffer.height = FB_HEIGHT;
    framebuffer.pitch = g_state.fb_pitch;
    framebuffer.bpp = FB_BPP;
    framebuffer.depth = 24U;
    framebuffer.handle = g_state.fb_handle;
    ioctl_status = implus_drm_ioctl(g_state.drm_fd, 0xAEUL, &framebuffer);
    log_i32("[tinywl] addfb status=", (int32_t)ioctl_status);
    if (ioctl_status != 0) {
        log_line("[tinywl] DRM_IOCTL_MODE_ADDFB failed\n");
        return -1;
    }

    g_state.fb_id = framebuffer.fb_id;
    log_u32("[tinywl] fb id=", g_state.fb_id);
    log_line("[tinywl] drm ready\n");
    return 0;
}

static void flip_framebuffer(void) {
    struct {
        uint32_t crtc_id;
        uint32_t fb_id;
        uint32_t flags;
        uint32_t reserved;
        uint64_t user_data;
    } page_flip;

    memset(&page_flip, 0, sizeof(page_flip));
    page_flip.crtc_id = 1U;
    page_flip.fb_id = g_state.fb_id;
    static uint8_t first_flip_logged = 0;
    long page_flip_status = implus_drm_ioctl(g_state.drm_fd, 0xB0UL, &page_flip);
    if (first_flip_logged == 0) {
        log_i32("[tinywl] page flip status=", (int32_t)page_flip_status);
        first_flip_logged = 1;
    }
}

static void init_input(void) {
    g_state.keyboard_fd = implus_evdev_open("/dev/input/event0");
    g_state.mouse_fd = implus_evdev_open("/dev/input/event1");
    log_i32("[tinywl] keyboard fd=", g_state.keyboard_fd);
    log_i32("[tinywl] mouse fd=", g_state.mouse_fd);

    if (g_state.keyboard_fd >= 0 || g_state.mouse_fd >= 0) {
        log_line("[tinywl] input ready\n");
    } else {
        log_line("[tinywl] input unavailable\n");
    }
}

static void init_wayland_socket(void) {
    int status;

    g_state.wayland_fd = implus_unix_socket(1);
    log_i32("[tinywl] wayland fd=", g_state.wayland_fd);
    if (g_state.wayland_fd < 0) {
        log_line("[tinywl] unix socket create failed\n");
        return;
    }
    status = implus_unix_bind(g_state.wayland_fd, "/run/wayland-0");
    log_i32("[tinywl] bind status=", status);
    if (status != 0) {
        log_line("[tinywl] bind /run/wayland-0 failed\n");
        return;
    }
    status = implus_unix_listen(g_state.wayland_fd, 4);
    log_i32("[tinywl] listen status=", status);
    if (status != 0) {
        log_line("[tinywl] listen /run/wayland-0 failed\n");
        return;
    }
    log_line("[tinywl] wayland socket ready: /run/wayland-0\n");
}

static void update_cursor_from_mouse(void) {
    struct input_event events[16];
    long bytes_read;
    int event_count;
    uint8_t moved = 0;

    if (g_state.mouse_fd < 0) {
        return;
    }

    bytes_read = implus_evdev_read(g_state.mouse_fd, events, sizeof(events));
    if (bytes_read <= 0) {
        return;
    }

    log_i32("[tinywl] mouse read bytes=", (int32_t)bytes_read);

    event_count = (int)(bytes_read / (long)sizeof(struct input_event));
    log_i32("[tinywl] mouse event count=", event_count);
    for (int i = 0; i < event_count; ++i) {
        if (events[i].type == EV_REL && events[i].code == REL_X) {
            int32_t next_x = (int32_t)g_state.cursor_x + events[i].value;
            if (next_x < 0) {
                next_x = 0;
            } else if (next_x >= (int32_t)FB_WIDTH) {
                next_x = (int32_t)FB_WIDTH - 1;
            }
            g_state.cursor_x = (uint32_t)next_x;
            moved = 1;
        } else if (events[i].type == EV_REL && events[i].code == REL_Y) {
            int32_t next_y = (int32_t)g_state.cursor_y + events[i].value;
            if (next_y < 0) {
                next_y = 0;
            } else if (next_y >= (int32_t)FB_HEIGHT) {
                next_y = (int32_t)FB_HEIGHT - 1;
            }
            g_state.cursor_y = (uint32_t)next_y;
            moved = 1;
        }
    }

    if (moved != 0) {
        log_cursor_position();
    }
}

static void draw_cursor(void) {
    uint32_t x = g_state.cursor_x;
    uint32_t y = g_state.cursor_y;

    fill_rect(x, y, 12U, 16U, 0xFFFFFFFFU);
    fill_rect(x + 1U, y + 1U, 10U, 14U, 0xFF000000U);
    fill_rect(x + 2U, y + 2U, 8U, 12U, 0xFFFFFFFFU);
}

static void draw_shell(void) {
    draw_gradient_background();
    fill_rect(100U, 80U, 500U, 400U, 0xFFE8E8E8U);
    fill_rect(100U, 80U, 500U, 28U, 0xFF3366CCU);
    fill_rect(120U, 130U, 460U, 200U, 0xFFFFFFFFU);
    fill_rect(0U, FB_HEIGHT - 32U, FB_WIDTH, 32U, 0xFF2D2D2DU);
    fill_rect(4U, FB_HEIGHT - 28U, 80U, 24U, 0xFF4488FFU);
    draw_cursor();
}

void _start(void) {
    uint64_t last_heartbeat_ms;
    serial_write_string("tinywl: a native Wayland compositor stub\n");
    log_line("[tinywl] booting native compositor stub\n");
    log_i32("[tinywl] pid=", process_get_current_pid());

    if (init_drm() != 0) {
        log_line("[tinywl] fatal: compositor could not acquire DRM\n");
        process_exit(1);
    }

    init_input();
    init_wayland_socket();
    draw_shell();
    log_line("[tinywl] first frame rendered in memory\n");
    flip_framebuffer();
    log_line("[tinywl] first frame submitted\n");
    last_heartbeat_ms = get_uptime_ms();

    for (;;) {
        update_cursor_from_mouse();
        draw_shell();
        flip_framebuffer();
        if (get_uptime_ms() - last_heartbeat_ms >= 2000U) {
            log_line("[tinywl] heartbeat\n");
            log_cursor_position();
            last_heartbeat_ms = get_uptime_ms();
        }
        process_yield();
    }
}
