#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>

#include "../../../API/Process.h"
#include "../../../API/Serial.h"
#include "../../../API/Window.h"
#include "../../../API/Memory.h"
#include "../../../API/IPC.h"
#include "../../../API/Input.h"
#include "../../../API/WM_Protocol.h"
#include "../../../WaylandPort/adapters/include/implus_drm.h"
#include "../../../WaylandPort/adapters/include/implus_evdev.h"

#include "../../../GTKPort/sysroot/include/cairo/cairo.h"
#include "../../../GTKPort/sysroot/include/pango-1.0/pango.h"
#include "../../../GTKPort/sysroot/include/glib-2.0/glib.h"

#define FB_WIDTH        1024U
#define FB_HEIGHT       768U
#define FB_BPP          32U

#define TITLE_HEIGHT    28U
#define BORDER_WIDTH    1U
#define CORNER_RADIUS   8U
#define SHADOW_SIZE     6U
#define TASKBAR_HEIGHT  36U
#define BTN_SIZE        16U
#define BTN_MARGIN      6U
#define BTN_SPACING     4U

#define MAX_WINDOWS     64
#define TITLE_MAX       64

#define CURSOR_W        14U
#define CURSOR_H        22U

#define COL_BG_TOP      0xFF061A2AU
#define COL_BG_BOT      0xFF1B7B8FU
#define COL_TITLEBAR    0xFF162433U
#define COL_TITLEBAR_UF 0xFF232C35U
#define COL_ACCENT      0xFF47B6D6U
#define COL_BORDER      0xFF43505DU
#define COL_BORDER_FOC  0xFF47B6D6U
#define COL_CLOSE       0xFFC42B1CU
#define COL_WIN_BG      0xFF1F1F1FU
#define COL_TASKBAR     0xDE102130U
#define COL_TEXT        0xFFE0E0E0U

static const char k_cursor[CURSOR_H][CURSOR_W + 1] = {
    "B.............",
    "BB............",
    "BFB...........",
    "BFFB..........",
    "BFFFB.........",
    "BFFFFB........",
    "BFFFFFB.......",
    "BFFFFFFB......",
    "BFFFFFFFB.....",
    "BFFFFFFFFB....",
    "BFFFFFFFFFB...",
    "BFFFFFFFFFFB..",
    "BFFFFFFBBBBBB.",
    "BFFFBFFFB.....",
    "BFFB.BFFB.....",
    "BFB..BFFB.....",
    "BB....BFFB....",
    "B.....BFFB....",
    ".......BFFB...",
    ".......BFFB...",
    "........BB....",
    "..............",
};

typedef struct {
    uint32_t id;
    int32_t  x, y;
    uint32_t w, h;
    char     title[TITLE_MAX];
    bool     visible;
    bool     focused;
    uint32_t bg_color;
} managed_window_t;

typedef struct {
    int              drm_fd;
    uint32_t         fb_handle;
    uint32_t         fb_pitch;
    uint32_t         fb_id;
    uint64_t         fb_size;
    uint32_t        *fb;
    cairo_surface_t *surface;
    cairo_t         *cr;
    int              kbd_fd;
    int              mouse_fd;
    uint32_t         cursor_x;
    uint32_t         cursor_y;
    managed_window_t windows[MAX_WINDOWS];
    uint32_t         win_count;
    int32_t          focused_idx;
    bool             dragging;
    int32_t          drag_idx;
    int32_t          drag_ox, drag_oy;
    bool             dirty;
} wm_state_t;

static wm_state_t g;

static void log_str(const char *s) { serial_write_string(s); }
static void log_u32(const char *l, uint32_t v) {
    serial_write_string(l);
    serial_write_uint32(v);
    serial_write_string("\n");
}

static inline void rgba_from_argb(uint32_t c, double *r, double *g_,
                                  double *b, double *a) {
    *a = (double)((c >> 24) & 0xFF) / 255.0;
    *r = (double)((c >> 16) & 0xFF) / 255.0;
    *g_ = (double)((c >> 8) & 0xFF) / 255.0;
    *b = (double)(c & 0xFF) / 255.0;
}

static void cr_set_argb(cairo_t *cr, uint32_t c) {
    double r, gg, b, a;
    rgba_from_argb(c, &r, &gg, &b, &a);
    cairo_set_source_rgba(cr, r, gg, b, a);
}

static void cr_rounded_rect(cairo_t *cr, double x, double y,
                            double w, double h, double rad) {
    double deg = 3.14159265 / 180.0;
    cairo_new_path(cr);
    cairo_arc(cr, x + w - rad, y + rad, rad, -90 * deg, 0);
    cairo_arc(cr, x + w - rad, y + h - rad, rad, 0, 90 * deg);
    cairo_arc(cr, x + rad, y + h - rad, rad, 90 * deg, 180 * deg);
    cairo_arc(cr, x + rad, y + rad, rad, 180 * deg, 270 * deg);
    cairo_close_path(cr);
}

static bool init_drm(void) {
    g.drm_fd = implus_drm_open();
    if (g.drm_fd < 0) { log_str("[gtk_wm] DRM open failed\n"); return false; }

    struct { uint32_t w, h, bpp; uint32_t handle, pitch, size_lo, size_hi; } req;
    req.w = FB_WIDTH; req.h = FB_HEIGHT; req.bpp = FB_BPP;
    if (implus_drm_ioctl(g.drm_fd, DRM_IOCTL_BASE + 1, &req) < 0) {
        log_str("[gtk_wm] DRM create_dumb failed\n"); return false;
    }
    g.fb_handle = req.handle;
    g.fb_pitch  = req.pitch;
    g.fb_size   = ((uint64_t)req.size_hi << 32) | req.size_lo;

    struct { uint32_t handle, fb_id; uint32_t w, h, bpp, pitch; } fb_req;
    fb_req.handle = g.fb_handle;
    fb_req.w = FB_WIDTH; fb_req.h = FB_HEIGHT;
    fb_req.bpp = FB_BPP; fb_req.pitch = g.fb_pitch;
    if (implus_drm_ioctl(g.drm_fd, DRM_IOCTL_BASE + 2, &fb_req) < 0) {
        log_str("[gtk_wm] DRM add_fb failed\n"); return false;
    }
    g.fb_id = fb_req.fb_id;

    struct { uint32_t fb_id; } set_req;
    set_req.fb_id = g.fb_id;
    implus_drm_ioctl(g.drm_fd, DRM_IOCTL_BASE + 3, &set_req);

    g.fb = (uint32_t *)implus_drm_mmap(g.drm_fd, 0, g.fb_size);
    if (!g.fb) { log_str("[gtk_wm] DRM mmap failed\n"); return false; }

    log_str("[gtk_wm] DRM initialised\n");
    log_u32("  fb_pitch=", g.fb_pitch);
    return true;
}

static void init_input(void) {
    g.kbd_fd   = implus_evdev_open("/dev/input/keyboard");
    g.mouse_fd = implus_evdev_open("/dev/input/mouse");
    g.cursor_x = FB_WIDTH / 2;
    g.cursor_y = FB_HEIGHT / 2;
    log_str("[gtk_wm] input initialised\n");
}

static bool init_cairo_surface(void) {
    g.surface = cairo_image_surface_create_for_data(
        (unsigned char *)g.fb,
        CAIRO_FORMAT_ARGB32,
        (int)FB_WIDTH, (int)FB_HEIGHT,
        (int)g.fb_pitch);
    if (!g.surface) { log_str("[gtk_wm] cairo surface failed\n"); return false; }
    g.cr = cairo_create(g.surface);
    log_str("[gtk_wm] cairo surface ready\n");
    return true;
}

static void draw_background(void) {
    double r1, g1, b1, a1, r2, g2, b2, a2;
    rgba_from_argb(COL_BG_TOP, &r1, &g1, &b1, &a1);
    rgba_from_argb(COL_BG_BOT, &r2, &g2, &b2, &a2);

    for (uint32_t y = 0; y < FB_HEIGHT; y++) {
        double t = (double)y / (double)FB_HEIGHT;
        double r = r1 + (r2 - r1) * t;
        double gg = g1 + (g2 - g1) * t;
        double b = b1 + (b2 - b1) * t;
        cairo_set_source_rgb(g.cr, r, gg, b);
        cairo_rectangle(g.cr, 0, (double)y, (double)FB_WIDTH, 1.0);
        cairo_fill(g.cr);
    }
}

static void draw_taskbar(void) {
    double tb_y = (double)(FB_HEIGHT - TASKBAR_HEIGHT);
    cr_set_argb(g.cr, COL_TASKBAR);
    cairo_rectangle(g.cr, 0, tb_y, (double)FB_WIDTH, (double)TASKBAR_HEIGHT);
    cairo_fill(g.cr);

    cr_set_argb(g.cr, COL_ACCENT);
    cairo_set_line_width(g.cr, 1.0);
    cairo_move_to(g.cr, 0, tb_y);
    cairo_line_to(g.cr, (double)FB_WIDTH, tb_y);
    cairo_stroke(g.cr);

    double bx = 8.0;
    for (uint32_t i = 0; i < g.win_count; i++) {
        managed_window_t *w = &g.windows[i];
        if (!w->visible) continue;
        double bw = 72.0, bh = 24.0;
        double by = tb_y + ((double)TASKBAR_HEIGHT - bh) / 2.0;

        if ((int32_t)i == g.focused_idx)
            cr_set_argb(g.cr, COL_ACCENT);
        else
            cr_set_argb(g.cr, COL_TITLEBAR);

        cr_rounded_rect(g.cr, bx, by, bw, bh, 4.0);
        cairo_fill(g.cr);

        cr_set_argb(g.cr, COL_TEXT);
        cairo_move_to(g.cr, bx + 6.0, by + 16.0);
        cairo_show_text(g.cr, w->title);

        bx += bw + 4.0;
    }
}

static void draw_window(managed_window_t *w, bool focused) {
    double x = (double)w->x;
    double y = (double)w->y;
    double ww = (double)w->w;
    double wh = (double)w->h + (double)TITLE_HEIGHT;

    for (uint32_t s = 1; s <= SHADOW_SIZE; s++) {
        double off = (double)s;
        cairo_set_source_rgba(g.cr, 0, 0, 0, 0.15 / (double)s);
        cr_rounded_rect(g.cr, x - off, y - off,
                        ww + 2 * off, wh + 2 * off,
                        (double)CORNER_RADIUS + off);
        cairo_fill(g.cr);
    }

    cr_set_argb(g.cr, w->bg_color ? w->bg_color : COL_WIN_BG);
    cr_rounded_rect(g.cr, x, y, ww, wh, (double)CORNER_RADIUS);
    cairo_fill(g.cr);

    cr_set_argb(g.cr, focused ? COL_TITLEBAR : COL_TITLEBAR_UF);
    cr_rounded_rect(g.cr, x, y, ww, (double)TITLE_HEIGHT, (double)CORNER_RADIUS);
    cairo_fill(g.cr);
    
    cr_set_argb(g.cr, focused ? COL_TITLEBAR : COL_TITLEBAR_UF);
    cairo_rectangle(g.cr, x, y + (double)TITLE_HEIGHT - (double)CORNER_RADIUS,
                    ww, (double)CORNER_RADIUS);
    cairo_fill(g.cr);

    cr_set_argb(g.cr, COL_TEXT);
    cairo_move_to(g.cr, x + 10.0, y + 18.0);
    cairo_show_text(g.cr, w->title);

    cr_set_argb(g.cr, focused ? COL_BORDER_FOC : COL_BORDER);
    cairo_set_line_width(g.cr, 1.0);
    cr_rounded_rect(g.cr, x, y, ww, wh, (double)CORNER_RADIUS);
    cairo_stroke(g.cr);

    double cbx = x + ww - (double)BTN_MARGIN - (double)BTN_SIZE;
    double cby = y + ((double)TITLE_HEIGHT - (double)BTN_SIZE) / 2.0;
    cr_set_argb(g.cr, COL_CLOSE);
    cairo_arc(g.cr, cbx + (double)BTN_SIZE / 2.0,
              cby + (double)BTN_SIZE / 2.0,
              (double)BTN_SIZE / 2.0, 0, 2.0 * 3.14159265);
    cairo_fill(g.cr);

    double mbx = cbx - (double)BTN_SIZE - (double)BTN_SPACING;
    cr_set_argb(g.cr, COL_ACCENT);
    cairo_arc(g.cr, mbx + (double)BTN_SIZE / 2.0,
              cby + (double)BTN_SIZE / 2.0,
              (double)BTN_SIZE / 2.0, 0, 2.0 * 3.14159265);
    cairo_fill(g.cr);

    double nbx = mbx - (double)BTN_SIZE - (double)BTN_SPACING;
    cr_set_argb(g.cr, COL_ACCENT);
    cairo_arc(g.cr, nbx + (double)BTN_SIZE / 2.0,
              cby + (double)BTN_SIZE / 2.0,
              (double)BTN_SIZE / 2.0, 0, 2.0 * 3.14159265);
    cairo_fill(g.cr);
}

static void draw_cursor(void) {
    for (uint32_t cy = 0; cy < CURSOR_H; cy++) {
        for (uint32_t cx = 0; cx < CURSOR_W; cx++) {
            char ch = k_cursor[cy][cx];
            uint32_t px = g.cursor_x + cx;
            uint32_t py = g.cursor_y + cy;
            if (px >= FB_WIDTH || py >= FB_HEIGHT) continue;
            if (ch == 'B') {
                cairo_set_source_rgb(g.cr, 0, 0, 0);
                cairo_rectangle(g.cr, (double)px, (double)py, 1, 1);
                cairo_fill(g.cr);
            } else if (ch == 'F') {
                cairo_set_source_rgb(g.cr, 1, 1, 1);
                cairo_rectangle(g.cr, (double)px, (double)py, 1, 1);
                cairo_fill(g.cr);
            }
        }
    }
}

static void render_frame(void) {
    draw_background();
    for (uint32_t i = 0; i < g.win_count; i++) {
        if (g.windows[i].visible) {
            draw_window(&g.windows[i], (int32_t)i == g.focused_idx);
        }
    }
    draw_taskbar();
    draw_cursor();
    cairo_surface_flush(g.surface);
    g.dirty = false;
}

static int32_t find_window(uint32_t id) {
    for (uint32_t i = 0; i < g.win_count; i++)
        if (g.windows[i].id == id) return (int32_t)i;
    return -1;
}

static void wm_create_window(uint32_t id, uint32_t w, uint32_t h,
                             const char *title) {
    if (g.win_count >= MAX_WINDOWS) return;
    managed_window_t *mw = &g.windows[g.win_count];
    memset(mw, 0, sizeof(*mw));
    mw->id = id;
    mw->x  = 50 + (int32_t)(g.win_count * 30);
    mw->y  = 50 + (int32_t)(g.win_count * 30);
    mw->w  = w;
    mw->h  = h;
    mw->visible = true;
    mw->focused = true;
    mw->bg_color = COL_WIN_BG;
    if (title) {
        size_t len = strlen(title);
        if (len >= TITLE_MAX) len = TITLE_MAX - 1;
        memcpy(mw->title, title, len);
        mw->title[len] = '\0';
    } else {
        memcpy(mw->title, "Window", 7);
    }
    g.focused_idx = (int32_t)g.win_count;
    g.win_count++;
    g.dirty = true;
    log_str("[gtk_wm] window created: ");
    log_str(mw->title);
    log_str("\n");
}

static void wm_destroy_window(uint32_t id) {
    int32_t idx = find_window(id);
    if (idx < 0) return;
    for (uint32_t i = (uint32_t)idx; i + 1 < g.win_count; i++)
        g.windows[i] = g.windows[i + 1];
    g.win_count--;
    if (g.focused_idx >= (int32_t)g.win_count)
        g.focused_idx = (int32_t)g.win_count - 1;
    g.dirty = true;
}

static int32_t hit_test_title(uint32_t mx, uint32_t my) {
    for (int32_t i = (int32_t)g.win_count - 1; i >= 0; i--) {
        managed_window_t *w = &g.windows[i];
        if (!w->visible) continue;
        if ((int32_t)mx >= w->x && mx < (uint32_t)(w->x + (int32_t)w->w) &&
            (int32_t)my >= w->y && my < (uint32_t)(w->y + (int32_t)TITLE_HEIGHT))
            return i;
    }
    return -1;
}

static int32_t hit_test_close(uint32_t mx, uint32_t my) {
    for (int32_t i = (int32_t)g.win_count - 1; i >= 0; i--) {
        managed_window_t *w = &g.windows[i];
        if (!w->visible) continue;
        double cbx = (double)w->x + (double)w->w - (double)BTN_MARGIN - (double)BTN_SIZE;
        double cby = (double)w->y + ((double)TITLE_HEIGHT - (double)BTN_SIZE) / 2.0;
        if ((double)mx >= cbx && (double)mx <= cbx + (double)BTN_SIZE &&
            (double)my >= cby && (double)my <= cby + (double)BTN_SIZE)
            return i;
    }
    return -1;
}

#define EV_KEY 1U
#define EV_REL 2U
#define REL_X  0U
#define REL_Y  1U
#define BTN_LEFT   0x110U
#define BTN_RIGHT  0x111U

static void process_input(void) {
    struct input_event ev;

    while (implus_evdev_read(g.mouse_fd, &ev, sizeof(ev)) > 0) {
        if (ev.type == EV_REL) {
            if (ev.code == REL_X) {
                int32_t nx = (int32_t)g.cursor_x + ev.value;
                if (nx < 0) nx = 0;
                if (nx >= (int32_t)FB_WIDTH) nx = (int32_t)FB_WIDTH - 1;
                g.cursor_x = (uint32_t)nx;
            } else if (ev.code == REL_Y) {
                int32_t ny = (int32_t)g.cursor_y + ev.value;
                if (ny < 0) ny = 0;
                if (ny >= (int32_t)FB_HEIGHT) ny = (int32_t)FB_HEIGHT - 1;
                g.cursor_y = (uint32_t)ny;
            }
            if (g.dragging && g.drag_idx >= 0) {
                managed_window_t *w = &g.windows[g.drag_idx];
                w->x = (int32_t)g.cursor_x - g.drag_ox;
                w->y = (int32_t)g.cursor_y - g.drag_oy;
            }
            g.dirty = true;
        } else if (ev.type == EV_KEY) {
            if (ev.code == BTN_LEFT) {
                if (ev.value == 1) { 
                    int32_t ci = hit_test_close(g.cursor_x, g.cursor_y);
                    if (ci >= 0) {
                        wm_destroy_window(g.windows[ci].id);
                    } else {
                        int32_t ti = hit_test_title(g.cursor_x, g.cursor_y);
                        if (ti >= 0) {
                            g.focused_idx = ti;
                            g.dragging = true;
                            g.drag_idx = ti;
                            g.drag_ox = (int32_t)g.cursor_x - g.windows[ti].x;
                            g.drag_oy = (int32_t)g.cursor_y - g.windows[ti].y;
                            g.dirty = true;
                        }
                    }
                } else if (ev.value == 0) { 
                    g.dragging = false;
                    g.drag_idx = -1;
                }
            }
        }
    }

    while (implus_evdev_read(g.kbd_fd, &ev, sizeof(ev)) > 0) {
        
        (void)ev;
    }
}

void _start(void) {
    memset(&g, 0, sizeof(g));
    g.focused_idx = -1;
    g.drag_idx = -1;

    log_str("[gtk_wm] starting GTK window manager\n");

    if (!init_drm()) { process_exit(1); return; }
    init_input();
    if (!init_cairo_surface()) { process_exit(1); return; }

    wm_create_window(1, 400, 300, "Terminal");
    wm_create_window(2, 350, 250, "Settings");

    g.dirty = true;

    log_str("[gtk_wm] entering main loop\n");

    for (;;) {
        process_input();
        if (g.dirty)
            render_frame();
        process_yield();
    }
}
