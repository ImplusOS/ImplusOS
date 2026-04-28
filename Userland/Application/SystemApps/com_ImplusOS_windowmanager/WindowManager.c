#include "WindowManager.h"
#include "../../../../Userland/Syscalls.h"
#include "../../../../Userland/API/Memory.h"
#include "../../../../Userland/API/Serial.h"
#include "../../../../Userland/API/Process.h"
#include "../../../../Userland/API/Window.h"
#include "../../../../Userland/API/Input.h"
#include "../../../../Userland/API/File.h"
#include "../../../../libc/include/math.h"
#include "../../../../libc/include/string.h"
#include <stdlib.h>
#include "../../../../Userland/API/XMLParser.h"

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#define STBTT_malloc(x,u)  ((void)(u),malloc(x))
#define STBTT_free(x,u)    ((void)(u),free(x))
#define STBTT_fmod(x,y)    fmod(x,y)
#include "../../../../Thirdparty/stb_truetype.h"

static void* my_realloc_sized(void* p, size_t oldsz, size_t newsz) {
    if (newsz == 0) { if (p) free(p); return NULL; }
    void* result = malloc(newsz);
    if (result && p) {
        memcpy(result, p, oldsz < newsz ? oldsz : newsz);
        free(p);
    }
    return result;
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-conversion"
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wtype-limits"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wshadow"
#pragma GCC diagnostic ignored "-Wextra"
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#define STBI_NO_STDIO
#define STBI_NO_SIMD
#define STBI_NO_THREAD_LOCALS
#define STBI_ONLY_PNG
#define STBI_MALLOC(sz)           malloc(sz)
#define STBI_REALLOC(p,newsz) my_realloc_sized(p, 0, newsz)
#define STBI_REALLOC_SIZED(p,oldsz,newsz) my_realloc_sized(p,oldsz,newsz)
#define STBI_FREE(p)              free(p)
#include "../../../../Thirdparty/stb_image.h"
#pragma GCC diagnostic pop

static uint32_t parse_hex_color(const char *str) {
    if (!str) return 0;
    if (str[0] == '0' && (str[1] == 'x' || str[1] == 'X')) str += 2;
    uint32_t val = 0;
    while (*str) {
        char c = *str++;
        val <<= 4;
        if (c >= '0' && c <= '9') val |= (c - '0');
        else if (c >= 'a' && c <= 'f') val |= (c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') val |= (c - 'A' + 10);
    }
    return val;
}

static const char k_cursor_map[WM_CURSOR_H][WM_CURSOR_W + 1] = {
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

static const uint8_t k_corner_skip[WM_CORNER_RADIUS] = {5, 3, 2, 1, 1, 0, 0, 0};

static wm_state_t g_state;
static stbtt_fontinfo g_font_info;

static char g_clock_str[32] = "";
static bool g_clock_dirty = false;

static int utf8_decode(const char **s) {
    const unsigned char *str = (const unsigned char *)*s;
    if (!str) return 0;
    int c = *str++;
    if (c == 0) return 0;
    if (c < 0x80) { *s = (const char *)str; return c; }
    if ((c & 0xE0) == 0xC0) {
        int c2 = *str++;
        *s = (const char *)str;
        return ((c & 0x1F) << 6) | (c2 & 0x3F);
    }
    if ((c & 0xF0) == 0xE0) {
        int c2 = *str++; int c3 = *str++;
        *s = (const char *)str;
        return ((c & 0x0F) << 12) | ((c2 & 0x3F) << 6) | (c3 & 0x3F);
    }
    if ((c & 0xF8) == 0xF0) {
        int c2 = *str++; int c3 = *str++; int c4 = *str++;
        *s = (const char *)str;
        return ((c & 0x07) << 18) | ((c2 & 0x3F) << 12) | ((c3 & 0x3F) << 6) | (c4 & 0x3F);
    }
    *s = (const char *)str;
    return '?';
}

static inline uint32_t alpha_blend(uint32_t bg, uint32_t fg) {
    uint32_t a = (fg >> 24) & 0xFF;
    if (a == 0xFF) return fg;
    if (a == 0x00) return bg;
    uint32_t inv = 255u - a;
    uint32_t rb = ((fg & 0xFF00FF) * a + (bg & 0xFF00FF) * inv + 0x800080) >> 8;
    uint32_t g  = ((fg & 0x00FF00) * a + (bg & 0x00FF00) * inv + 0x008000) >> 8;
    return 0xFF000000u | (rb & 0xFF00FF) | (g & 0x00FF00);
}

static inline void shadow_put(wm_compositor_t *c, uint32_t idx, uint32_t color) {
    if (idx < c->shadow_bytes / sizeof(uint32_t)) c->shadow[idx] = color;
}

static inline void shadow_put_blend(wm_compositor_t *c, uint32_t idx, uint32_t color) {
    uint32_t max = c->shadow_bytes / sizeof(uint32_t);
    if (idx >= max) return;
    c->shadow[idx] = alpha_blend(c->shadow[idx], color);
}

static inline void shadow_put_alpha(wm_compositor_t *c, uint32_t idx, uint32_t color, float alpha) {
    if (alpha >= 0.999f) {
        uint32_t a = (color >> 24);
        if (a == 0xFF) {
            if (idx < c->shadow_bytes / sizeof(uint32_t)) c->shadow[idx] = color;
        } else if (a > 0) {
            shadow_put_blend(c, idx, color);
        }
    } else if (alpha > 0.001f) {
        uint32_t a = (color >> 24);
        a = (uint32_t)(a * alpha);
        if (a > 0) {
            color = (color & 0x00FFFFFF) | (a << 24);
            shadow_put_blend(c, idx, color);
        }
    }
}

static inline void shadow_put_black_alpha(wm_compositor_t *c, uint32_t idx, uint32_t alpha) {
    uint32_t max = c->shadow_bytes / sizeof(uint32_t);
    if (idx >= max) return;
    uint32_t bg = c->shadow[idx];
    uint32_t inv = 255u - alpha;
    uint32_t rb = ((bg & 0xFF00FF) * inv + 0x800080) >> 8;
    uint32_t gg = ((bg & 0x00FF00) * inv + 0x008000) >> 8;
    c->shadow[idx] = 0xFF000000u | (rb & 0xFF00FF) | (gg & 0x00FF00);
}

static inline void shadow_put_blend_clip(wm_compositor_t *c, uint32_t x, uint32_t y, uint32_t cx0, uint32_t cy0, uint32_t cx1, uint32_t cy1, uint32_t color) {
    if (x >= cx0 && x < cx1 && y >= cy0 && y < cy1) {
        shadow_put_blend(c, y * c->fb_width + x, color);
    }
}

static inline void shadow_put_clip(wm_compositor_t *c, uint32_t x, uint32_t y, uint32_t cx0, uint32_t cy0, uint32_t cx1, uint32_t cy1, uint32_t color) {
    uint32_t max = c->shadow_bytes / sizeof(uint32_t);
    uint32_t idx = y * c->fb_width + x;
    if (x >= cx0 && x < cx1 && y >= cy0 && y < cy1 && idx < max) {
        c->shadow[idx] = color;
    }
}

static inline uint32_t color_lerp(uint32_t c0, uint32_t c1, uint32_t t, uint32_t denom) {
    if (denom == 0) return c0;
    int32_t r0 = (int32_t)((c0 >> 16) & 0xFF), g0 = (int32_t)((c0 >> 8) & 0xFF), b0 = (int32_t)(c0 & 0xFF);
    int32_t r1 = (int32_t)((c1 >> 16) & 0xFF), g1 = (int32_t)((c1 >> 8) & 0xFF), b1 = (int32_t)(c1 & 0xFF);
    int32_t r = r0 + (r1 - r0) * (int32_t)t / (int32_t)denom;
    int32_t g = g0 + (g1 - g0) * (int32_t)t / (int32_t)denom;
    int32_t b = b0 + (b1 - b0) * (int32_t)t / (int32_t)denom;
    return 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}



#define GLYPH_CACHE_SIZE 512

typedef struct {
    int codepoint;
    float font_size;
    uint32_t last_used;
    int w, h, cx0, cy0, advance;
    bool valid;
    uint8_t *bitmap; 
} glyph_cache_entry_t;

static glyph_cache_entry_t g_glyph_cache[GLYPH_CACHE_SIZE];
static uint32_t g_glyph_clock = 0;

static glyph_cache_entry_t* get_glyph(int codepoint, float font_size, float scale) {
    g_glyph_clock++;
    uint32_t hash = ((uint32_t)codepoint * 73856093) ^ ((uint32_t)(font_size * 100.0f) * 19349663);
    uint32_t base_idx = hash % GLYPH_CACHE_SIZE;
    
    glyph_cache_entry_t *entry = NULL;
    int oldest_idx = -1;
    uint32_t oldest_time = 0xFFFFFFFF;
    
    for (int i = 0; i < 4; i++) {
        uint32_t idx = (base_idx + i) % GLYPH_CACHE_SIZE;
        if (g_glyph_cache[idx].valid && g_glyph_cache[idx].codepoint == codepoint && g_glyph_cache[idx].font_size == font_size) {
            g_glyph_cache[idx].last_used = g_glyph_clock;
            return &g_glyph_cache[idx];
        }
        if (!g_glyph_cache[idx].valid) {
            entry = &g_glyph_cache[idx];
            break;
        }
        if (g_glyph_cache[idx].last_used < oldest_time) {
            oldest_time = g_glyph_cache[idx].last_used;
            oldest_idx = idx;
        }
    }
    
    if (!entry) {
        entry = &g_glyph_cache[oldest_idx];
    }

    int advance, lsb;
    stbtt_GetCodepointHMetrics(&g_font_info, codepoint, &advance, &lsb);
    int cx0, cy0, cx1, cy1;
    stbtt_GetCodepointBitmapBox(&g_font_info, codepoint, scale, scale, &cx0, &cy0, &cx1, &cy1);
    
    entry->w = cx1 - cx0;
    entry->h = cy1 - cy0;
    entry->cx0 = cx0;
    entry->cy0 = cy0;
    entry->advance = advance;

    if (entry->w > 64 || entry->h > 64) {
        return NULL;
    }

    if (entry->valid && entry->bitmap) {
        free(entry->bitmap);
        entry->bitmap = NULL;
    }

    if (entry->w > 0 && entry->h > 0) {
        entry->bitmap = (uint8_t *)malloc((size_t)(entry->w * entry->h));
        if (entry->bitmap) {
            stbtt_MakeCodepointBitmap(&g_font_info, entry->bitmap, entry->w, entry->h, entry->w, scale, scale, codepoint);
        }
    }
    
    entry->codepoint = codepoint;
    entry->font_size = font_size;
    entry->valid = true;
    entry->last_used = g_glyph_clock;
    return entry;
}

static void comp_draw_text(wm_compositor_t *comp, uint32_t x0, uint32_t y0, const char *text, uint32_t color, float font_size, uint32_t max_w, float alpha) {
    if (!g_state.font_loaded || !comp->shadow) return;
    float scale = stbtt_ScaleForPixelHeight(&g_font_info, font_size);
    int ascent, descent, lineGap;
    stbtt_GetFontVMetrics(&g_font_info, &ascent, &descent, &lineGap);
    ascent = (int)(ascent * scale);
    int x = (int)x0;
    int y = (int)y0 + ascent;
    const char *ptr = text;
    while (*ptr) {
        int codepoint = utf8_decode(&ptr);
        if (codepoint == 0) break;
        
        glyph_cache_entry_t *entry = get_glyph(codepoint, font_size, scale);
        if (!entry) {
            int advance, lsb;
            stbtt_GetCodepointHMetrics(&g_font_info, codepoint, &advance, &lsb);
            x += (int)(advance * scale);
            continue;
        }

        int w = entry->w;
        int h = entry->h;
        int cx0 = entry->cx0;
        int cy0 = entry->cy0;
        
        if (x + cx0 + w >= (int)(x0 + max_w)) {
           if (x + cx0 > (int)(x0 + max_w)) break; 
        }
        
        if (w > 0 && h > 0) {
            uint32_t cr = (color >> 16) & 0xFF;
            uint32_t cg = (color >> 8) & 0xFF;
            uint32_t cb = color & 0xFF;
            for (int row = 0; row < h; row++) {
                uint32_t py = (uint32_t)(y + cy0 + row);
                if (py >= comp->fb_height) continue;
                uint32_t row_off = py * comp->fb_width;
                for (int col = 0; col < w; col++) {
                    uint32_t px = (uint32_t)(x + cx0 + col);
                    if (px >= comp->fb_width || px >= x0 + max_w) continue;
                    uint8_t a = entry->bitmap[row * w + col];
                    if (a == 0) continue;
                    a = (uint8_t)(a * alpha);
                    uint32_t c_out = (a << 24) | (cr << 16) | (cg << 8) | cb;
                    shadow_put_blend(comp, row_off + px, c_out);
                }
            }
        }
        x += (int)(entry->advance * scale);
    }
}

static uint32_t measure_text_width(const char *text, float font_size) {
    if (!g_state.font_loaded) return 0;
    float scale = stbtt_ScaleForPixelHeight(&g_font_info, font_size);
    int x = 0;
    const char *ptr = text;
    while (*ptr) {
        int codepoint = utf8_decode(&ptr);
        if (codepoint == 0) break;
        glyph_cache_entry_t *entry = get_glyph(codepoint, font_size, scale);
        if (entry) {
            x += (int)(entry->advance * scale);
        } else {
            int advance, lsb;
            stbtt_GetCodepointHMetrics(&g_font_info, codepoint, &advance, &lsb);
            x += (int)(advance * scale);
        }
    }
    return (uint32_t)x;
}

typedef struct {
    uint32_t type;
    uint32_t request_id;
    uint32_t window_id;
} wm_msg_hdr_t;

static wm_window_t *g_id_table[WM_MAX_WINDOWS + 1];

static wm_window_t *slot_find_by_id(wm_server_t *srv, uint32_t id) {
    if (!srv || id == 0 || id > WM_MAX_WINDOWS) return NULL;
    return g_id_table[id];
}

static inline void wm_compositor_mark_dirty_bounds(wm_compositor_t *comp, uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1) {
    if (!comp) return;
    if (x1 > comp->fb_width) x1 = comp->fb_width;
    if (y1 > comp->fb_height) y1 = comp->fb_height;
    if (x0 >= x1 || y0 >= y1) return;
    if (!comp->dirty.dirty) {
        comp->dirty.x0 = x0;
        comp->dirty.y0 = y0;
        comp->dirty.x1 = x1;
        comp->dirty.y1 = y1;
        comp->dirty.dirty = true;
    } else {
        if (x0 < comp->dirty.x0) comp->dirty.x0 = x0;
        if (y0 < comp->dirty.y0) comp->dirty.y0 = y0;
        if (x1 > comp->dirty.x1) comp->dirty.x1 = x1;
        if (y1 > comp->dirty.y1) comp->dirty.y1 = y1;
    }
}

static inline void wm_mark_window_dirty(wm_compositor_t *comp, wm_window_t *w) {
    if (!comp || !w) return;
    uint32_t x0 = (w->x > WM_DIRTY_MARGIN_RIGHT) ? w->x - WM_DIRTY_MARGIN_RIGHT : 0;
    uint32_t y0 = (w->y > WM_DIRTY_MARGIN_TOP) ? w->y - WM_DIRTY_MARGIN_TOP : 0;
    uint32_t x1 = w->x + w->w + WM_DIRTY_MARGIN_RIGHT;
    uint32_t y1 = w->y + w->h + WM_TITLE_HEIGHT + WM_DIRTY_MARGIN_BOTTOM;
    wm_compositor_mark_dirty_bounds(comp, x0, y0, x1, y1);
}

static inline void wm_mark_window_title_dirty(wm_compositor_t *comp, wm_window_t *w) {
    if (!comp || !w) return;
    uint32_t x0 = (w->x > WM_DIRTY_MARGIN_RIGHT) ? w->x - WM_DIRTY_MARGIN_RIGHT : 0;
    uint32_t y0 = (w->y > WM_DIRTY_MARGIN_TOP) ? w->y - WM_DIRTY_MARGIN_TOP : 0;
    uint32_t x1 = w->x + w->w + WM_DIRTY_MARGIN_RIGHT;
    uint32_t y1 = w->y + WM_TITLE_HEIGHT + WM_DIRTY_MARGIN_BOTTOM;
    wm_compositor_mark_dirty_bounds(comp, x0, y0, x1, y1);
}

static void zlist_remove(wm_server_t *srv, wm_window_t *w) {
    if (!srv || !w) return;
    if (w->z_prev) w->z_prev->z_next = w->z_next;
    else           srv->z_top        = w->z_next;
    if (w->z_next) w->z_next->z_prev = w->z_prev;
    else           srv->z_bottom     = w->z_prev;
    w->z_prev = w->z_next = NULL;
}

static void zlist_push_top(wm_server_t *srv, wm_window_t *w) {
    if (!srv || !w) return;
    w->z_prev = NULL;
    w->z_next = srv->z_top;
    if (srv->z_top) srv->z_top->z_prev = w;
    srv->z_top = w;
    if (!srv->z_bottom) srv->z_bottom = w;
}

static void zlist_push_bottom(wm_server_t *srv, wm_window_t *w) {
    if (!srv || !w) return;
    w->z_next = NULL;
    w->z_prev = srv->z_bottom;
    if (srv->z_bottom) srv->z_bottom->z_next = w;
    srv->z_bottom = w;
    if (!srv->z_top) srv->z_top = w;
}

void wm_server_init(wm_server_t *srv) {
    if (!srv) return;
    memset(srv, 0, sizeof(*srv));
    memset(g_id_table, 0, sizeof(g_id_table));
    srv->next_id = 1;
}

static void wm_give_focus_to_next(wm_server_t *srv, uint32_t except_id) {
    for (wm_window_t *w = srv->z_top; w; w = w->z_next) {
        if (w->id != except_id && w->visible && !w->is_system && !w->is_closing && !w->minimized) {
            wm_server_set_focus(srv, w->id);
            return;
        }
    }
    wm_window_t *old = slot_find_by_id(srv, srv->focused_id);
    if (old) {
        old->has_focus = false;
        wm_mark_window_dirty(&g_state.compositor, old);
    }
    srv->focused_id = 0;
}

wm_window_t *wm_server_find_window(wm_server_t *srv, uint32_t id) {
    return slot_find_by_id(srv, id);
}

int32_t wm_server_create_window(wm_server_t *srv, int32_t owner, uint32_t w, uint32_t h, uint32_t x, uint32_t y, uint32_t bg, const char *title) {
    if (!srv || w == 0 || h == 0) return -1;
    if (srv->window_count >= WM_MAX_WINDOWS) return -1;
    uint32_t id = 0;
    for (uint32_t i = 1; i <= WM_MAX_WINDOWS; i++) {
        if (!g_id_table[i]) {
            id = i;
            break;
        }
    }
    if (id == 0) return -1;

    wm_window_t *win = (wm_window_t *)malloc(sizeof(wm_window_t));
    if (!win) return -1;
    memset(win, 0, sizeof(*win));
    win->id        = id;
    win->owner_pid = owner;
    win->x = x;  win->y = y;
    win->w = w;  win->h = h;
    win->bg_color  = bg;
    win->visible   = true;
    win->anim_alpha = 0.0f;
    win->is_closing = false;
    win->minimized  = false;
    win->maximized  = false;
    if (title) {
        uint32_t i = 0;
        while (i < WM_TITLE_MAX - 1 && title[i]) { win->title[i] = title[i]; ++i; }
        win->title[i] = '\0';
    }
    g_id_table[id] = win;
    srv->windows[srv->window_count++] = win;
    zlist_push_top(srv, win);
    wm_server_set_focus(srv, win->id);
    return (int32_t)win->id;
}

static void wm_server_destroy_window_real(wm_server_t *srv, uint32_t id) {
    if (!srv) return;
    for (uint32_t i = 0; i < srv->window_count; ++i) {
        if (!srv->windows[i] || srv->windows[i]->id != id) continue;
        wm_window_t *w = srv->windows[i];
        wm_mark_window_dirty(&g_state.compositor, w);
        zlist_remove(srv, w);
        if (srv->focused_id == id) {
            srv->focused_id = srv->z_top ? srv->z_top->id : 0;
            if (srv->z_top) srv->z_top->has_focus = true;
        }
        g_id_table[w->id] = NULL;
        
        if (w->xml_buffer) free(w->xml_buffer);
        
        for (uint32_t j = 0; j < srv->input_sub_count; ++j) {
            if (srv->input_subs[j].window_id == id) {
                srv->input_subs[j] = srv->input_subs[--srv->input_sub_count];
                j--;
            }
        }

        if (w->owner_pid > 0) {
            struct { wm_msg_header_t hdr; } msg;
            memset(&msg, 0, sizeof(msg));
            msg.hdr.type = WM_WINDOW_DESTROYED;
            msg.hdr.window_id = id;
            ipc_send_message(w->owner_pid, &msg, sizeof(msg));
        }

        free(w);
        srv->windows[i] = srv->windows[--srv->window_count];
        srv->windows[srv->window_count] = NULL;
        return;
    }
}

void wm_server_destroy_window(wm_server_t *srv, uint32_t id) {
    wm_window_t *win = slot_find_by_id(srv, id);
    if (win) {
        win->is_closing = true;
        wm_mark_window_dirty(&g_state.compositor, win);
        if (srv->focused_id == id) {
            wm_give_focus_to_next(srv, id);
        }
    }
}

void wm_server_update_cursor(wm_server_t *srv, uint32_t x, uint32_t y) {
    if (!srv) return;
    srv->cursor_x       = x;
    srv->cursor_y       = y;
    srv->cursor_visible = true;
}

void wm_server_set_rect(wm_server_t *srv, uint32_t id, uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    if (!srv) return;
    wm_window_t *win = slot_find_by_id(srv, id);
    if (!win) return;
    wm_mark_window_dirty(&g_state.compositor, win);
    win->x = x; win->y = y; win->w = w; win->h = h;
    wm_mark_window_dirty(&g_state.compositor, win);
}

void wm_server_show(wm_server_t *srv, uint32_t id) {
    if (!srv) return;
    wm_window_t *win = slot_find_by_id(srv, id);
    if (win && !win->visible) {
        win->visible = true;
        win->anim_alpha = 0.0f;
        win->is_closing = false;
        wm_mark_window_dirty(&g_state.compositor, win);
    }
}

void wm_server_hide(wm_server_t *srv, uint32_t id) {
    if (!srv) return;
    wm_window_t *win = slot_find_by_id(srv, id);
    if (win && win->visible) {
        win->is_closing = true;
        wm_mark_window_dirty(&g_state.compositor, win);
    }
};

void wm_server_raise(wm_server_t *srv, uint32_t id) {
    if (!srv) return;
    wm_window_t *win = slot_find_by_id(srv, id);
    if (!win || win == srv->z_top) return;
    zlist_remove(srv, win);
    zlist_push_top(srv, win);
    wm_mark_window_dirty(&g_state.compositor, win);
}

void wm_server_lower(wm_server_t *srv, uint32_t id) {
    if (!srv) return;
    wm_window_t *win = slot_find_by_id(srv, id);
    if (!win || win == srv->z_bottom) return;
    zlist_remove(srv, win);
    zlist_push_bottom(srv, win);
    wm_mark_window_dirty(&g_state.compositor, win);
}

void wm_server_set_focus(wm_server_t *srv, uint32_t id) {
    if (!srv) return;
    wm_window_t *old = slot_find_by_id(srv, srv->focused_id);
    if (old) {
        old->has_focus = false;
        wm_mark_window_dirty(&g_state.compositor, old);
    }
    wm_window_t *nw = slot_find_by_id(srv, id);
    if (nw) {
        nw->has_focus   = true;
        srv->focused_id = id;
        wm_mark_window_dirty(&g_state.compositor, nw);
    }
}

uint32_t wm_server_hit_test(wm_server_t *srv, uint32_t px, uint32_t py) {
    wm_window_t *cur = srv->z_top;
    while (cur) {
        if (cur->visible && !cur->is_closing && !cur->minimized) {
            float alpha = cur->anim_alpha;
            float scale = 0.94f + 0.06f * alpha;
            uint32_t win_w = (uint32_t)(cur->w * scale);
            uint32_t win_h = (uint32_t)(cur->h * scale);
            uint32_t title_h = (uint32_t)(WM_TITLE_HEIGHT * scale);
            if (title_h < 2) title_h = 2;

            int anim_offset_y = (int)((1.0f - alpha) * 15.0f);
            uint32_t wx = cur->x + (cur->w - win_w) / 2;
            uint32_t wy = cur->y + anim_offset_y + (WM_TITLE_HEIGHT - title_h) / 2;

            if (px >= wx && px < wx + win_w && py >= wy && py < wy + title_h + win_h)
                return cur->id;
        }
        cur = cur->z_next;
    }
    return 0;
}

void wm_server_route_keyboard(wm_server_t *srv, ipc_message_t *msg) {
    if (!srv || !msg || !srv->focused_id) return;
    wm_window_t *win = slot_find_by_id(srv, srv->focused_id);
    if (win && win->owner_pid > 0 && !win->is_closing)
        ipc_send_message(win->owner_pid, msg->data, msg->size);
}

void wm_server_route_mouse(wm_server_t *srv, ipc_message_t *msg) {
    if (!srv || !msg || !srv->focused_id) return;
    wm_window_t *win = slot_find_by_id(srv, srv->focused_id);
    if (win && win->owner_pid > 0 && !win->is_closing)
        ipc_send_message(win->owner_pid, msg->data, msg->size);
}

bool wm_compositor_init(wm_compositor_t *comp, uint32_t width, uint32_t height) {
    comp->fb_width  = width;
    comp->fb_height = height;
    uint32_t pixels = width * height;
    uint32_t bytes  = pixels * sizeof(uint32_t);
    if (comp->shadow)     { free(comp->shadow);     comp->shadow     = NULL; }
    if (comp->background) { free(comp->background); comp->background = NULL; }
    comp->shadow = (uint32_t *)malloc(bytes);
    if (!comp->shadow) return false;
    comp->shadow_bytes = bytes;
    comp->background = (uint32_t *)malloc(bytes);
    if (!comp->background) { free(comp->shadow); comp->shadow = NULL; return false; }
    comp->bg_bytes = bytes;
    comp->dirty.dirty        = false;
    comp->prev_cursor_drawn  = false;
    comp->prev_cx = comp->prev_cy = 0;
    return true;
}


static void generate_background(wm_compositor_t *comp) {
    if (!comp->background) return;
    uint32_t w = comp->fb_width;
    uint32_t h = comp->fb_height;
    uint32_t desk_h = (h > WM_TASKBAR_HEIGHT) ? h - WM_TASKBAR_HEIGHT : h;

    for (uint32_t y = 0; y < h; ++y) {
        uint32_t base = color_lerp(COLOR_BG_TOP, COLOR_BG_MID, y, h > 1 ? h - 1 : 1);
        if (y >= desk_h) {
            base = color_lerp(COLOR_BG_MID, COLOR_BG_BOT, y - desk_h, (h - desk_h) > 1 ? (h - desk_h) - 1 : 1);
        }
        for (uint32_t x = 0; x < w; ++x) {
            uint32_t row_mix = (x * 96u) / (w > 0 ? w : 1u);
            comp->background[y * w + x] = color_lerp(base, COLOR_BG_BOT, row_mix, 96u);
        }
    }

    int32_t fd = file_open("/Userland/SystemApps/com_ImplusOS_windowmanager/Resource/Background.png", 0);
    uint8_t *img_data = NULL;
    int img_w = 0, img_h = 0, img_channels = 0;

    if (fd >= 0) {
        file_stat_t st;
        if (file_stat("/Userland/SystemApps/com_ImplusOS_windowmanager/Resource/Background.png", &st) == 0 && st.size > 0) {
            uint8_t *file_buf = (uint8_t *)malloc(st.size);
            if (file_buf) {
                int64_t total_read = 0;
                while (total_read < st.size) {
                    int64_t r = file_read(fd, file_buf + total_read, st.size - total_read);
                    if (r <= 0) break;
                    total_read += r;
                }
                
                if (total_read == st.size) {
                    img_data = stbi_load_from_memory(file_buf, (int)st.size, &img_w, &img_h, &img_channels, 4);
                }
                
                free(file_buf);
            }
        }
        file_close(fd);
    }
    if (img_data && img_w > 0 && img_h > 0) {
        uint32_t scale_num, scale_den;
        if ((uint64_t)w * img_h > (uint64_t)desk_h * img_w) {
            scale_num = w;
            scale_den = img_w;
        } else {
            scale_num = desk_h;
            scale_den = img_h;
        }
        
        uint32_t scaled_w = (uint32_t)img_w * scale_num / scale_den;
        uint32_t scaled_h = (uint32_t)img_h * scale_num / scale_den;

        int32_t off_x = (int32_t)(scaled_w > w      ? (scaled_w - w)      / 2 : 0);
        int32_t off_y = (int32_t)(scaled_h > desk_h ? (scaled_h - desk_h) / 2 : 0);

        for (uint32_t y = 0; y < h; ++y) {
            uint32_t row_off = y * w;
            uint32_t src_y = (uint32_t)(((int32_t)y + off_y) * (int32_t)scale_den / (int32_t)scale_num);
            if (src_y >= (uint32_t)img_h) src_y = (uint32_t)img_h - 1;
            uint32_t src_row_off = src_y * (uint32_t)img_w * 4; 

            for (uint32_t x = 0; x < w; ++x) {
                uint32_t src_x = (uint32_t)(((int32_t)x + off_x) * (int32_t)scale_den / (int32_t)scale_num);
                if (src_x >= (uint32_t)img_w) src_x = (uint32_t)img_w - 1;

                uint32_t px_off = src_row_off + src_x * 4;
                uint8_t r = img_data[px_off + 0];
                uint8_t g = img_data[px_off + 1];
                uint8_t b = img_data[px_off + 2];
                uint32_t img_color = 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
                comp->background[row_off + x] = color_lerp(comp->background[row_off + x], img_color, 9u, 10u);
            }
        }
        STBI_FREE(img_data);
    }
}


static void composite_background(wm_compositor_t *comp, uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1) {
    if (!comp->shadow || !comp->background) return;
    uint32_t dw = comp->fb_width;
    for (uint32_t y = y0; y < y1; ++y) {
        uint32_t off = y * dw;
        memcpy(&comp->shadow[off + x0], &comp->background[off + x0], (x1 - x0) * sizeof(uint32_t));
    }
}

void wm_compositor_mark_dirty(wm_compositor_t *comp, uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    if (w == 0 || h == 0) return;
    
    uint32_t margin = WM_SHADOW_SIZE + 10;
    uint32_t ex = (x > margin) ? x - margin : 0;
    uint32_t ey = (y > margin) ? y - margin : 0;
    uint32_t x1 = x + w + margin;
    uint32_t y1 = y + h + margin;
    if (x1 > comp->fb_width)  x1 = comp->fb_width;
    if (y1 > comp->fb_height) y1 = comp->fb_height;
    if (x1 <= ex || y1 <= ey) return;
    if (!comp->dirty.dirty) {
        comp->dirty.x0 = ex;  comp->dirty.y0 = ey;
        comp->dirty.x1 = x1; comp->dirty.y1 = y1;
        comp->dirty.dirty = true;
    } else {
        if (ex < comp->dirty.x0) comp->dirty.x0 = ex;
        if (ey < comp->dirty.y0) comp->dirty.y0 = ey;
        if (x1 > comp->dirty.x1) comp->dirty.x1 = x1;
        if (y1 > comp->dirty.y1) comp->dirty.y1 = y1;
    }

    uint32_t tb_threshold = comp->fb_height - WM_TASKBAR_HEIGHT - 10;
    if (comp->dirty.y1 > tb_threshold) {
        comp->dirty.x0 = 0;
        comp->dirty.x1 = comp->fb_width;
    }
}

static inline bool corner_skip(uint32_t row, uint32_t col) {
    if (row >= WM_CORNER_RADIUS || col >= WM_CORNER_RADIUS) return false;
    return col < k_corner_skip[row];
}

static inline bool rounded_rect_skip(uint32_t row, uint32_t col, uint32_t w, uint32_t h, uint32_t radius) {
    if (w == 0 || h == 0 || radius == 0) return false;
    if (radius * 2 > w) radius = w / 2;
    if (radius * 2 > h) radius = h / 2;
    if (radius == 0) return false;

    uint32_t rc = w - 1 - col;
    uint32_t br = h - 1 - row;
    if (row < radius && col < radius) {
        uint32_t dx = radius - col - 1;
        uint32_t dy = radius - row - 1;
        return dx * dx + dy * dy > radius * radius;
    }
    if (row < radius && rc < radius) {
        uint32_t dx = radius - rc - 1;
        uint32_t dy = radius - row - 1;
        return dx * dx + dy * dy > radius * radius;
    }
    if (br < radius && col < radius) {
        uint32_t dx = radius - col - 1;
        uint32_t dy = radius - br - 1;
        return dx * dx + dy * dy > radius * radius;
    }
    if (br < radius && rc < radius) {
        uint32_t dx = radius - rc - 1;
        uint32_t dy = radius - br - 1;
        return dx * dx + dy * dy > radius * radius;
    }
    return false;
}

static void shadow_fill_rounded_rect_clip(wm_compositor_t *comp, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t radius, uint32_t cx0, uint32_t cy0, uint32_t cx1, uint32_t cy1, uint32_t color, float alpha) {
    if (!comp || !comp->shadow || w == 0 || h == 0) return;
    uint32_t dw = comp->fb_width;
    uint32_t start_r = (y > cy0) ? y : cy0;
    uint32_t end_r = (y + h < cy1) ? y + h : cy1;
    if (end_r > comp->fb_height) end_r = comp->fb_height;
    uint32_t start_c = (x > cx0) ? x : cx0;
    uint32_t end_c = (x + w < cx1) ? x + w : cx1;
    if (end_c > dw) end_c = dw;

    for (uint32_t r = start_r; r < end_r; ++r) {
        for (uint32_t c = start_c; c < end_c; ++c) {
            if (rounded_rect_skip(r - y, c - x, w, h, radius)) continue;
            shadow_put_alpha(comp, r * dw + c, color, alpha);
        }
    }
}


static void comp_draw_window_shadow(wm_compositor_t *comp, wm_window_t *win, uint32_t wx, uint32_t wy, float alpha, uint32_t cx0, uint32_t cy0, uint32_t cx1, uint32_t cy1) {
    if (!win || !win->visible || win->is_system) return;
    uint32_t dw = comp->fb_width;
    uint32_t dh = comp->fb_height;
    uint32_t sw = win->w;
    uint32_t sh = win->h + WM_TITLE_HEIGHT;
    
    for (uint32_t layer = 0; layer < WM_SHADOW_SIZE; ++layer) {
        uint32_t a_val = (0x20u * (WM_SHADOW_SIZE - layer) / WM_SHADOW_SIZE);
        a_val = (uint32_t)(a_val * alpha);
        uint32_t sx = (wx > layer) ? wx - layer / 2 : 0;
        uint32_t sy = wy + layer;
        uint32_t lw = sw + layer;
        uint32_t lh = sh + layer;
        
        uint32_t by = sy + lh;
        if (by < dh && by >= cy0 && by < cy1) {
            uint32_t start_c = (sx > cx0) ? sx : cx0;
            uint32_t end_c = (sx + lw < cx1) ? sx + lw : cx1;
            if (end_c > dw) end_c = dw;
            for (uint32_t c = start_c; c < end_c; ++c)
                shadow_put_black_alpha(comp, by * dw + c, a_val);
        }
        
        uint32_t rx = sx + lw;
        if (rx < dw && rx >= cx0 && rx < cx1) {
            uint32_t start_r = (sy > cy0) ? sy : cy0;
            uint32_t end_r = (sy + lh < cy1) ? sy + lh : cy1;
            if (end_r > dh) end_r = dh;
            for (uint32_t r = start_r; r < end_r; ++r)
                shadow_put_black_alpha(comp, r * dw + rx, a_val);
        }
    }
}


static void comp_draw_window(wm_compositor_t *comp, wm_window_t *win, uint32_t cx0, uint32_t cy0, uint32_t cx1, uint32_t cy1) {
    if (!win || !win->visible || win->minimized || !comp->shadow) return;
    uint32_t dw = comp->fb_width;
    bool foc = win->has_focus;
    float alpha = win->anim_alpha;
    
    float scale = 0.94f + 0.06f * alpha;
    uint32_t win_w = (uint32_t)(win->w * scale);
    uint32_t win_h = (uint32_t)(win->h * scale);
    uint32_t title_h = (uint32_t)(WM_TITLE_HEIGHT * scale);
    if (title_h < 2) title_h = 2;

    int anim_offset_y = (int)((1.0f - alpha) * 15.0f);
    uint32_t wx = win->x + (win->w - win_w) / 2;
    uint32_t wy = win->y + anim_offset_y + (WM_TITLE_HEIGHT - title_h) / 2;

    if (win->is_system) {
        uint32_t start_r = (wy > cy0) ? wy : cy0;
        uint32_t end_r = (wy + win_h < cy1) ? wy + win_h : cy1;
        if (end_r > comp->fb_height) end_r = comp->fb_height;
        uint32_t start_c = (wx > cx0) ? wx : cx0;
        uint32_t end_c = (wx + win_w < cx1) ? wx + win_w : cx1;
        if (end_c > dw) end_c = dw;
        
        for (uint32_t r = start_r; r < end_r; ++r)
            for (uint32_t c = start_c; c < end_c; ++c)
                shadow_put_alpha(comp, r * dw + c, win->bg_color, alpha);
        return;
    }

    comp_draw_window_shadow(comp, win, wx, wy, alpha, cx0, cy0, cx1, cy1);

    uint32_t tb_color_top = foc ? COLOR_TITLEBAR_TOP : COLOR_TITLEBAR_UNFOCUS;
    uint32_t tb_color_bot = foc ? COLOR_TITLEBAR_BOT : COLOR_TITLEBAR_UNFOCUS_BOT;

    uint32_t title_start_r = (wy > cy0) ? wy : cy0;
    uint32_t title_end_r = (wy + title_h < cy1) ? wy + title_h : cy1;
    if (title_end_r > comp->fb_height) title_end_r = comp->fb_height;
    uint32_t content_start_c = (wx > cx0) ? wx : cx0;
    uint32_t content_end_c = (wx + win_w < cx1) ? wx + win_w : cx1;
    if (content_end_c > dw) content_end_c = dw;

    for (uint32_t r = title_start_r; r < title_end_r; ++r) {
        uint32_t lr = r - wy;
        uint32_t row_color = color_lerp(tb_color_top, tb_color_bot, lr, title_h > 1 ? title_h - 1 : 1);
        uint32_t base = r * dw;
        if (alpha >= 0.999f && lr >= WM_CORNER_RADIUS) {
            for (uint32_t c = content_start_c; c < content_end_c; ++c)
                comp->shadow[base + c] = row_color;
        } else {
            for (uint32_t c = content_start_c; c < content_end_c; ++c) {
                uint32_t lc_l = c - wx;
                uint32_t lc_r = (wx + win_w - 1) - c;
                if (corner_skip(lr, lc_l) || corner_skip(lr, lc_r)) continue;
                shadow_put_alpha(comp, base + c, row_color, alpha);
            }
        }
    }

    if (foc && win_w > (uint32_t)(WM_TITLE_BTN_W * 3 + 28)) {
        uint32_t acc_x = (uint32_t)(12 * scale);
        uint32_t acc_y = (uint32_t)(10 * scale);
        uint32_t acc_w = (uint32_t)(6 * scale);
        uint32_t acc_h = (uint32_t)(12 * scale);
        shadow_fill_rounded_rect_clip(comp, wx + acc_x, wy + acc_y, acc_w, acc_h, (uint32_t)(3 * scale), cx0, cy0, cx1, cy1, COLOR_TITLEBAR_ACCENT, alpha);
    }
    if (wy + title_h - 1 < comp->fb_height) {
        uint32_t line_y = wy + title_h - 1;
        uint32_t line_color = foc ? 0x3047B6D6u : 0x182A3E4Du;
        for (uint32_t c = content_start_c; c < content_end_c; ++c) {
            uint32_t lc_l = c - wx;
            uint32_t lc_r = (wx + win_w - 1) - c;
            if (corner_skip(0, lc_l) || corner_skip(0, lc_r)) continue;
            shadow_put_alpha(comp, line_y * dw + c, line_color, alpha);
        }
    }

    if (win_w > (uint32_t)(WM_TITLE_BTN_W * 3) && win_h >= title_h) {
        uint32_t btn_h = title_h;
        uint32_t btn_w = (uint32_t)(WM_TITLE_BTN_W * scale);
        uint32_t close_x = wx + win_w - btn_w;
        if (win->hover_close && foc) {
            shadow_fill_rounded_rect_clip(comp, close_x + 4, wy + 4, btn_w - 8, btn_h - 8, 6, cx0, cy0, cx1, cy1, COLOR_CLOSE_BTN_HOVER_BG, alpha);
        }
        {
            uint32_t icon_sz = 10;
            uint32_t ic_x = close_x + (btn_w - icon_sz) / 2;
            uint32_t ic_y = wy + (btn_h - icon_sz) / 2;
            uint32_t icon_col = (win->hover_close && foc) ? 0xFFFFFFFF : (foc ? COLOR_BTN_ICON : COLOR_TEXT_DIM);
            for (uint32_t i = 0; i < icon_sz; ++i) {
                uint32_t py1 = ic_y + i;
                uint32_t px1 = ic_x + i;
                uint32_t px2 = ic_x + icon_sz - 1 - i;
                if (py1 < comp->fb_height) {
                    if (px1 < dw) shadow_put_alpha(comp, py1 * dw + px1, icon_col, alpha);
                    if (px2 < dw && px2 != px1) shadow_put_alpha(comp, py1 * dw + px2, icon_col, alpha);
                    
                    if (px1 + 1 < dw) shadow_put_alpha(comp, py1 * dw + px1 + 1, icon_col, alpha);
                    if (px2 > 0 && px2 - 1 < dw && px2 - 1 != px1) shadow_put_alpha(comp, py1 * dw + px2 - 1, icon_col, alpha);
                }
            }
        }

        uint32_t max_x = wx + win->w - btn_w * 2;
        if (win->hover_max && foc) {
            shadow_fill_rounded_rect_clip(comp, max_x + 4, wy + 4, btn_w - 8, btn_h - 8, 6, cx0, cy0, cx1, cy1, COLOR_MAX_GREEN_HOVER, alpha);
        }
        {
            uint32_t icon_sz = 10;
            uint32_t ic_x = max_x + (btn_w - icon_sz) / 2;
            uint32_t ic_y = wy + (btn_h - icon_sz) / 2;
            uint32_t icon_col = foc ? COLOR_BTN_ICON : COLOR_TEXT_DIM;
            if (win->maximized) {
                uint32_t back_x = ic_x + 2;
                uint32_t back_y = ic_y;
                uint32_t front_x = ic_x;
                uint32_t front_y = ic_y + 2;
                for (uint32_t i = 0; i < icon_sz - 2; ++i) {
                    if (back_y < comp->fb_height && back_x + i < dw)
                        shadow_put_alpha(comp, back_y * dw + back_x + i, icon_col, alpha);
                    if (back_y + icon_sz - 3 < comp->fb_height && back_x + i < dw)
                        shadow_put_alpha(comp, (back_y + icon_sz - 3) * dw + back_x + i, icon_col, alpha);
                    if (front_y < comp->fb_height && front_x + i < dw)
                        shadow_put_alpha(comp, front_y * dw + front_x + i, icon_col, alpha);
                    if (front_y + icon_sz - 3 < comp->fb_height && front_x + i < dw)
                        shadow_put_alpha(comp, (front_y + icon_sz - 3) * dw + front_x + i, icon_col, alpha);
                }
                for (uint32_t i = 1; i < icon_sz - 3; ++i) {
                    if (back_y + i < comp->fb_height) {
                        if (back_x < dw) shadow_put_alpha(comp, (back_y + i) * dw + back_x, icon_col, alpha);
                        if (back_x + icon_sz - 3 < dw)
                            shadow_put_alpha(comp, (back_y + i) * dw + back_x + icon_sz - 3, icon_col, alpha);
                    }
                    if (front_y + i < comp->fb_height) {
                        if (front_x < dw) shadow_put_alpha(comp, (front_y + i) * dw + front_x, icon_col, alpha);
                        if (front_x + icon_sz - 3 < dw)
                            shadow_put_alpha(comp, (front_y + i) * dw + front_x + icon_sz - 3, icon_col, alpha);
                    }
                }
            } else {
                for (uint32_t i = 0; i < icon_sz; ++i) {
                    if (ic_y < comp->fb_height && ic_x + i < dw)
                        shadow_put_alpha(comp, ic_y * dw + ic_x + i, icon_col, alpha);
                    if (ic_y + icon_sz - 1 < comp->fb_height && ic_x + i < dw)
                        shadow_put_alpha(comp, (ic_y + icon_sz - 1) * dw + ic_x + i, icon_col, alpha);
                }
                for (uint32_t i = 1; i < icon_sz - 1; ++i) {
                    if (ic_y + i < comp->fb_height) {
                        if (ic_x < dw) shadow_put_alpha(comp, (ic_y + i) * dw + ic_x, icon_col, alpha);
                        if (ic_x + icon_sz - 1 < dw)
                            shadow_put_alpha(comp, (ic_y + i) * dw + ic_x + icon_sz - 1, icon_col, alpha);
                    }
                }
            }
        }

        uint32_t min_x = wx + win->w - btn_w * 3;
        if (win->hover_min && foc) {
            shadow_fill_rounded_rect_clip(comp, min_x + 4, wy + 4, btn_w - 8, btn_h - 8, 6, cx0, cy0, cx1, cy1, COLOR_MIN_YELLOW_HOVER, alpha);
        }
        {
            uint32_t icon_w = 10;
            uint32_t ic_x = min_x + (btn_w - icon_w) / 2;
            uint32_t ic_y = wy + btn_h / 2;
            uint32_t icon_col = foc ? COLOR_BTN_ICON : COLOR_TEXT_DIM;
            if (ic_y < comp->fb_height) {
                for (uint32_t i = 0; i < icon_w; ++i) {
                    if (ic_x + i < dw) shadow_put_alpha(comp, ic_y * dw + ic_x + i, icon_col, alpha);
                }
            }
        }
    }

    if (win->title[0] != '\0' && win_w > (uint32_t)(WM_TITLE_BTN_W * 3 + 10)) {
        uint32_t txt_x = wx + (uint32_t)(26 * scale);
        uint32_t max_text_w = win_w - (uint32_t)(WM_TITLE_BTN_W * 3 * scale) - (txt_x - wx) - (uint32_t)(12 * scale);
        float font_sz = 13.0f * scale;
        uint32_t txt_y = wy + (title_h) / 2 - (uint32_t)(7 * scale);
        comp_draw_text(comp, txt_x, txt_y, win->title, foc ? COLOR_TEXT : COLOR_TEXT_DIM, font_sz, max_text_w, alpha);
    }

    uint32_t cl_y = wy + title_h;
    uint32_t cl_h = win_h;
    uint32_t c_start_r = (cl_y > cy0) ? cl_y : cy0;
    uint32_t c_end_r = (cl_y + cl_h < cy1) ? cl_y + cl_h : cy1;
    if (c_end_r > comp->fb_height) c_end_r = comp->fb_height;
    
    for (uint32_t r = c_start_r; r < c_end_r; ++r) {
        uint32_t br = (wy + title_h + cl_h - 1) - r;
        uint32_t base = r * dw;
        if (alpha >= 0.999f && (win->bg_color >> 24) == 0xFF && br >= WM_CORNER_RADIUS) {
            for (uint32_t c = content_start_c; c < content_end_c; ++c)
                comp->shadow[base + c] = win->bg_color;
        } else {
            for (uint32_t c = content_start_c; c < content_end_c; ++c) {
                uint32_t lc_l = c - wx;
                uint32_t lc_r = (wx + win_w - 1) - c;
                if (corner_skip(br, lc_l) || corner_skip(br, lc_r)) continue;
                shadow_put_alpha(comp, base + c, win->bg_color, alpha);
            }
        }
    }

    for (uint32_t ui = 0; ui < win->ui_element_count; ui++) {
        wm_ui_element_t *e = &win->ui_elements[ui];
        uint32_t ex = wx + (uint32_t)(e->x * scale);
        uint32_t ey = cl_y + (uint32_t)(e->y * scale);
        uint32_t ew = (uint32_t)(e->w * scale);
        uint32_t eh = (uint32_t)(e->h * scale);
        
        if (e->type == WM_UI_TYPE_RECT || e->type == WM_UI_TYPE_PANEL || e->type == WM_UI_TYPE_BUTTON) {
            uint32_t elem_bg = e->bg_color;
            if (e->type == WM_UI_TYPE_PANEL) {
                uint32_t a = (elem_bg >> 24) & 0xFF;
                if (a == 0xFF) elem_bg = (elem_bg & 0x00FFFFFF) | (0xD0u << 24);
            }
            uint32_t e_start_r = (ey > cy0) ? ey : cy0;
            uint32_t e_end_r = (ey + eh < cy1) ? ey + eh : cy1;
            if (e_end_r > comp->fb_height) e_end_r = comp->fb_height;
            uint32_t e_start_c = (ex > cx0) ? ex : cx0;
            uint32_t e_end_c = (ex + ew < cx1) ? ex + ew : cx1;
            if (e_end_c > dw) e_end_c = dw;
            for (uint32_t r = e_start_r; r < e_end_r; ++r) {
                for (uint32_t c = e_start_c; c < e_end_c; ++c) {
                    shadow_put_alpha(comp, r * dw + c, elem_bg, alpha);
                }
            }
        }
        
        if (e->type == WM_UI_TYPE_LABEL || e->type == WM_UI_TYPE_BUTTON) {
            float fsz = (e->font_size > 0 ? e->font_size : 14.0f) * scale;
            uint32_t text_col = e->color;
            if (e->text[0] != '\0') {
                uint32_t tx = ex;
                uint32_t ty = ey;
                if (e->type == WM_UI_TYPE_BUTTON) {
                    uint32_t tw = measure_text_width(e->text, fsz);
                    if (tw < ew) tx = ex + (ew - tw) / 2;
                    ty = ey + (eh) / 2 - (uint32_t)(fsz / 2.0f);
                }
                comp_draw_text(comp, tx, ty, e->text, text_col, fsz, ew, alpha);
            }
        }
    }

    uint32_t bdr = foc ? COLOR_BORDER_FOCUS : COLOR_BORDER;

    if (wy < comp->fb_height) {
        for (uint32_t c = wx; c < wx + win_w && c < dw; ++c) {
            uint32_t lc_l = c - wx;
            uint32_t lc_r = (wx + win_w - 1) - c;
            if (corner_skip(0, lc_l) || corner_skip(0, lc_r)) continue;
            shadow_put_alpha(comp, wy * dw + c, bdr, alpha);
        }
    }

    for (uint32_t r = wy; r < wy + title_h + win_h && r < comp->fb_height; ++r) {
        uint32_t lr = r - wy;
        uint32_t br = (wy + title_h + win_h - 1) - r;
        if (lr < WM_CORNER_RADIUS && k_corner_skip[lr] > 0) continue;
        if (br < WM_CORNER_RADIUS && k_corner_skip[br] > 0) continue;
        if (wx < dw) shadow_put_alpha(comp, r * dw + wx, bdr, alpha);
        if (win_w > 0 && wx + win_w - 1 < dw) shadow_put_alpha(comp, r * dw + wx + win_w - 1, bdr, alpha);
    }

    if (wy + title_h + win_h - 1 < comp->fb_height) {
        for (uint32_t c = wx; c < wx + win_w && c < dw; ++c) {
            uint32_t lc_l = c - wx;
            uint32_t lc_r = (wx + win_w - 1) - c;
            if (corner_skip(0, lc_l) || corner_skip(0, lc_r)) continue;
            shadow_put_alpha(comp, (wy + title_h + win_h - 1) * dw + c, bdr, alpha);
        }
    }
}


static void comp_apply_blur_rect(wm_compositor_t *comp, uint32_t bx0, uint32_t by0, uint32_t bx1, uint32_t by1, uint32_t radius) {
    if (bx0 >= bx1 || by0 >= by1 || radius == 0) return;
    uint32_t w = bx1 - bx0;
    uint32_t h = by1 - by0;
    uint32_t dw = comp->fb_width;
    uint32_t *pixels = comp->shadow;
    
    uint32_t max_dim = w > h ? w : h;
    uint32_t *temp = (uint32_t*)malloc(max_dim * sizeof(uint32_t));
    if (!temp) return;
    
    for (uint32_t y = by0; y < by1; ++y) {
        uint32_t sum_r = 0, sum_g = 0, sum_b = 0, count = 0;
        int32_t r_start = (int32_t)bx0 - (int32_t)radius;
        int32_t r_end = (int32_t)bx0 + (int32_t)radius;
        if (r_start < 0) r_start = 0;
        if (r_end >= (int32_t)dw) r_end = (int32_t)dw - 1;
        
        for (int32_t i = r_start; i <= r_end; ++i) {
            uint32_t c = pixels[y * dw + i];
            sum_r += (c >> 16) & 0xFF; sum_g += (c >> 8) & 0xFF; sum_b += c & 0xFF; count++;
        }
        
        for (uint32_t x = bx0; x < bx1; ++x) {
            temp[x - bx0] = 0xFF000000u | ((sum_r / count) << 16) | ((sum_g / count) << 8) | (sum_b / count);
            if (x == bx1 - 1) break;
            
            int32_t right_add = (int32_t)x + 1 + (int32_t)radius;
            if (right_add < (int32_t)dw) {
                uint32_t c = pixels[y * dw + right_add];
                sum_r += (c >> 16) & 0xFF; sum_g += (c >> 8) & 0xFF; sum_b += c & 0xFF; count++;
            }
            int32_t left_sub = (int32_t)x - (int32_t)radius;
            if (left_sub >= 0) {
                uint32_t c = pixels[y * dw + left_sub];
                sum_r -= (c >> 16) & 0xFF; sum_g -= (c >> 8) & 0xFF; sum_b -= c & 0xFF; count--;
            }
        }
        for (uint32_t x = bx0; x < bx1; ++x) pixels[y * dw + x] = temp[x - bx0];
    }
    
    for (uint32_t x = bx0; x < bx1; ++x) {
        uint32_t sum_r = 0, sum_g = 0, sum_b = 0, count = 0;
        int32_t r_start = (int32_t)by0 - (int32_t)radius;
        int32_t r_end = (int32_t)by0 + (int32_t)radius;
        if (r_start < 0) r_start = 0;
        if (r_end >= (int32_t)comp->fb_height) r_end = (int32_t)comp->fb_height - 1;
        
        for (int32_t i = r_start; i <= r_end; ++i) {
            uint32_t c = pixels[i * dw + x];
            sum_r += (c >> 16) & 0xFF; sum_g += (c >> 8) & 0xFF; sum_b += c & 0xFF; count++;
        }
        
        for (uint32_t y = by0; y < by1; ++y) {
            temp[y - by0] = 0xFF000000u | ((sum_r / count) << 16) | ((sum_g / count) << 8) | (sum_b / count);
            if (y == by1 - 1) break;
            
            int32_t right_add = (int32_t)y + 1 + (int32_t)radius;
            if (right_add < (int32_t)comp->fb_height) {
                uint32_t c = pixels[right_add * dw + x];
                sum_r += (c >> 16) & 0xFF; sum_g += (c >> 8) & 0xFF; sum_b += c & 0xFF; count++;
            }
            int32_t left_sub = (int32_t)y - (int32_t)radius;
            if (left_sub >= 0) {
                uint32_t c = pixels[left_sub * dw + x];
                sum_r -= (c >> 16) & 0xFF; sum_g -= (c >> 8) & 0xFF; sum_b -= c & 0xFF; count--;
            }
        }
        for (uint32_t y = by0; y < by1; ++y) pixels[y * dw + x] = temp[y - by0];
    }
    free(temp);
}

static void comp_draw_taskbar(wm_compositor_t *comp, wm_server_t *srv, uint32_t dx0, uint32_t dy0, uint32_t dx1, uint32_t dy1) {
    if (!comp->shadow) return;
    uint32_t dw = comp->fb_width;
    uint32_t tb_margin_x = 12;
    uint32_t tb_margin_y = 6;
    uint32_t tb_h = WM_TASKBAR_HEIGHT;
    uint32_t tb_w = dw - tb_margin_x * 2;
    uint32_t tb_x = tb_margin_x;
    uint32_t tb_y = comp->fb_height - tb_h - tb_margin_y;

    if (dy1 <= tb_y) return;

    uint32_t bx0 = (dx0 > tb_x) ? dx0 : tb_x;
    uint32_t bx1 = (dx1 < tb_x + tb_w) ? dx1 : tb_x + tb_w;
    uint32_t by0 = (dy0 > tb_y) ? dy0 : tb_y;
    uint32_t by1 = (dy1 < tb_y + tb_h) ? dy1 : tb_y + tb_h;

    if (bx1 > bx0 && by1 > by0) {
        comp_apply_blur_rect(comp, bx0, by0, bx1, by1, 5);
        shadow_fill_rounded_rect_clip(comp, tb_x, tb_y, tb_w, tb_h, 12, bx0, by0, bx1, by1, COLOR_TASKBAR_BG_GLASS, 1.0f);
    }

    if (by0 == tb_y) {
        for (uint32_t c = bx0; c < bx1; ++c) {
            uint32_t lc = c - tb_x;
            uint32_t rc = tb_w - 1 - lc;
            if (lc >= 10 && rc >= 10)
                shadow_put_alpha(comp, tb_y * dw + c, COLOR_TASKBAR_HIGHLIGHT, 1.0f);
        }
    }

    uint32_t start_w = 48;
    uint32_t start_h = tb_h - 10;
    uint32_t si_x = tb_x + 8;
    uint32_t si_y = tb_y + (tb_h - start_h) / 2;
    uint32_t si_w = start_w;
    uint32_t si_h = start_h;
    shadow_fill_rounded_rect_clip(comp, si_x, si_y, si_w, si_h, 10, bx0, by0, bx1, by1, COLOR_TASKBAR_BTN_ACT, 1.0f);

    uint32_t logo_col = 0xFFFFFFFFu;
    uint32_t quad_sz = 6;
    uint32_t gap = 2;
    uint32_t logo_cx = si_x + si_w / 2;
    uint32_t logo_cy = si_y + si_h / 2;
    
    for (uint32_t ry = logo_cy - gap - quad_sz; ry < logo_cy - gap; ++ry)
        for (uint32_t rx = logo_cx - gap - quad_sz; rx < logo_cx - gap; ++rx)
            shadow_put_blend_clip(comp, rx, ry, bx0, by0, bx1, by1, logo_col);
    
    for (uint32_t ry = logo_cy - gap - quad_sz; ry < logo_cy - gap; ++ry)
        for (uint32_t rx = logo_cx + gap; rx < logo_cx + gap + quad_sz; ++rx)
            shadow_put_blend_clip(comp, rx, ry, bx0, by0, bx1, by1, logo_col);
    
    for (uint32_t ry = logo_cy + gap; ry < logo_cy + gap + quad_sz; ++ry)
        for (uint32_t rx = logo_cx - gap - quad_sz; rx < logo_cx - gap; ++rx)
            shadow_put_blend_clip(comp, rx, ry, bx0, by0, bx1, by1, logo_col);
    
    for (uint32_t ry = logo_cy + gap; ry < logo_cy + gap + quad_sz; ++ry)
        for (uint32_t rx = logo_cx + gap; rx < logo_cx + gap + quad_sz; ++rx)
            shadow_put_blend_clip(comp, rx, ry, bx0, by0, bx1, by1, logo_col);

    uint32_t btn_w = WM_TASKBAR_BTN_W;
    uint32_t btn_h = WM_TASKBAR_BTN_H;
    uint32_t btn_gap = WM_TASKBAR_BTN_GAP;
    uint32_t vis_count = 0;
    for (uint32_t i = 0; i < srv->window_count; ++i)
        if (srv->windows[i] && srv->windows[i]->visible && !srv->windows[i]->is_system && !srv->windows[i]->is_closing)
            vis_count++;

    if (vis_count > 0) {
        uint32_t total_w = vis_count * btn_w + (vis_count - 1) * btn_gap;
        uint32_t centered_x = (dw > total_w) ? (dw - total_w) / 2 : tb_x + start_w + 16;
        uint32_t min_x = si_x + si_w + 16;
        if (centered_x < min_x) centered_x = min_x;
        uint32_t btn_y = tb_y + (WM_TASKBAR_HEIGHT - btn_h) / 2;
        uint32_t bx = centered_x;
        for (uint32_t i = srv->window_count; i > 0; --i) {
            wm_window_t *w = srv->windows[i - 1];
            if (!w || !w->visible || w->is_system || w->is_closing) continue;

            uint32_t btn_color = w->has_focus ? COLOR_TASKBAR_BTN_ACT : (w->minimized ? COLOR_TASKBAR_BTN_MIN : COLOR_TASKBAR_BTN_IDLE);
            shadow_fill_rounded_rect_clip(comp, bx, btn_y, btn_w, btn_h, 8, bx0, by0, bx1, by1, btn_color, 1.0f);

            uint32_t ind_y = btn_y + btn_h - 2;
            uint32_t pill_w = w->has_focus ? 24 : 12;
            uint32_t ind_x = bx + (btn_w - pill_w) / 2;
            uint32_t ind_color = w->has_focus ? COLOR_TASKBAR_INDICATOR : (w->minimized ? 0x665A6B74u : 0x55A4BEC6u);
            for (uint32_t c = ind_x; c < ind_x + pill_w; ++c) {
                shadow_put_clip(comp, c, ind_y, bx0, by0, bx1, by1, ind_color);
            }

            {
                const char *label = w->title[0] ? w->title : "App";
                uint32_t tx = bx + 10;
                uint32_t ty = btn_y + 6;
                uint32_t text_col = w->has_focus ? COLOR_TASKBAR_TEXT : COLOR_TASKBAR_TEXT_DIM;
                comp_draw_text(comp, tx, ty, label, text_col, 10.0f, btn_w - 20, 1.0f);
            }

            bx += btn_w + btn_gap;
        }
    }

    if (g_clock_str[0]) {
        uint32_t tx = tb_x + tb_w - 200;
        uint32_t ty = tb_y + (tb_h - 18) / 2;
        comp_draw_text(comp, tx, ty, g_clock_str, COLOR_TASKBAR_TEXT, 18.0f, 190, 1.0f);
    }
}

static void comp_draw_cursor_to_shadow(wm_compositor_t *comp, uint32_t cx, uint32_t cy) {
    uint32_t dw = comp->fb_width;
    for (uint32_t row = 0; row < WM_CURSOR_H; ++row) {
        for (uint32_t col = 0; col < WM_CURSOR_W; ++col) {
            char ch = k_cursor_map[row][col];
            if (ch == '.') continue;
            uint32_t px = cx + col + 2;
            uint32_t py = cy + row + 2;
            if (px < dw && py < comp->fb_height) shadow_put_blend(comp, py * dw + px, COLOR_CURSOR_SHADOW);
        }
    }
    for (uint32_t row = 0; row < WM_CURSOR_H; ++row) {
        for (uint32_t col = 0; col < WM_CURSOR_W; ++col) {
            char ch = k_cursor_map[row][col];
            if (ch == '.') continue;
            uint32_t color = (ch == 'F') ? COLOR_CURSOR_FILL : COLOR_CURSOR_BORDER;
            uint32_t px = cx + col;
            uint32_t py = cy + row;
            if (px < dw && py < comp->fb_height) comp->shadow[py * dw + px] = color;
        }
    }
}


static void flush_rect(wm_compositor_t *comp, uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1) {
    if (!comp->shadow) return;
    if (x1 > comp->fb_width)  x1 = comp->fb_width;
    if (y1 > comp->fb_height) y1 = comp->fb_height;
    if (x0 >= x1 || y0 >= y1) return;
    uint32_t dw  = comp->fb_width;
    uint32_t w   = x1 - x0;

    uint32_t *fb = (uint32_t *)sys_get_display_framebuffer();
    if (fb) {
        static int first_flush = 1;
        if (first_flush) {
            first_flush = 0;
        }

        for (uint32_t row = y0; row < y1; ++row) {
            uint32_t base = row * dw + x0;
            uint32_t *dst = fb + base;
            uint32_t *src = comp->shadow + base;
            uint64_t count = w;
            __asm__ volatile (
                "cld\n\t"
                "rep movsl"
                : "+D"(dst), "+S"(src), "+c"(count)
                :: "memory"
            );
        }
        return;
    }

    for (uint32_t row = y0; row < y1; ++row) {
        uint32_t base = row * dw + x0;
        uint32_t col = 0;
        while (col < w) {
            uint32_t color = comp->shadow[base + col];
            uint32_t run = 1;
            while (col + run < w && comp->shadow[base + col + run] == color)
                ++run;
            draw_fill_rect(x0 + col, row, run, 1, color);
            col += run;
        }
    }
}

void wm_compositor_render(wm_state_t *st) {
    wm_compositor_t *comp = &st->compositor;
    wm_server_t     *srv  = &st->server;
    if (!comp->shadow || !comp->background) return;

    for (uint32_t i = 0; i < srv->window_count; ++i) {
        wm_window_t *w = srv->windows[i];
        if (!w) continue;
        if (w->is_closing) {
            if (w->anim_alpha > 0.0f) {
                w->anim_alpha -= 0.1f;
                if (w->anim_alpha < 0.0f) w->anim_alpha = 0.0f;
                wm_mark_window_dirty(comp, w);
            } else {
                wm_server_destroy_window_real(srv, w->id);
            }
        } else if (w->visible) {
            if (w->anim_alpha < 1.0f) {
                w->anim_alpha += 0.08f;
                if (w->anim_alpha > 1.0f) w->anim_alpha = 1.0f;
                wm_mark_window_dirty(comp, w);
            }
        }
    }

    uint32_t ncx = srv->cursor_x;
    uint32_t ncy = srv->cursor_y;
    bool     cursor = srv->cursor_visible;

    bool cursor_moved = comp->prev_cursor_drawn && (ncx != comp->prev_cx || ncy != comp->prev_cy);
    bool cursor_hidden = comp->prev_cursor_drawn && !cursor;

    if (cursor_moved || cursor_hidden) {
        wm_compositor_mark_dirty(comp, comp->prev_cx, comp->prev_cy, WM_CURSOR_W + 4, WM_CURSOR_H + 4);
    }
    if (cursor) {
        wm_compositor_mark_dirty(comp, ncx, ncy, WM_CURSOR_W + 4, WM_CURSOR_H + 4);
    }

    if (!comp->dirty.dirty) return;

    uint32_t dx0 = comp->dirty.x0;
    uint32_t dy0 = comp->dirty.y0;
    uint32_t dx1 = comp->dirty.x1;
    uint32_t dy1 = comp->dirty.y1;

    composite_background(comp, dx0, dy0, dx1, dy1);

    for (wm_window_t *w = srv->z_bottom; w; w = w->z_prev)
        if (!w->is_system) comp_draw_window(comp, w, dx0, dy0, dx1, dy1);
    for (wm_window_t *w = srv->z_bottom; w; w = w->z_prev)
        if (w->is_system) comp_draw_window(comp, w, dx0, dy0, dx1, dy1);

    comp_draw_taskbar(comp, srv, dx0, dy0, dx1, dy1);

    if (cursor) {
        comp_draw_cursor_to_shadow(comp, ncx, ncy);
        comp->prev_cx = ncx;
        comp->prev_cy = ncy;
        comp->prev_cursor_drawn = true;
    } else {
        comp->prev_cursor_drawn = false;
    }

    flush_rect(comp, dx0, dy0, dx1, dy1);
    draw_present();
    comp->dirty.dirty = false;
}

void wm_server_handle_message(wm_state_t *st, ipc_message_t *msg) {
    if (!st || !msg || msg->size < sizeof(wm_msg_hdr_t)) return;
    wm_msg_hdr_t *hdr = (wm_msg_hdr_t *)msg->data;
    wm_server_t  *srv = &st->server;

    switch (hdr->type) {
    case WM_CREATE_WINDOW: {
        struct {
            wm_msg_hdr_t hdr;
            uint32_t width, height, x, y, bg_color;
            char title[64];
        } *cmd = (void *)msg->data;
        int32_t wid = wm_server_create_window(srv, msg->sender_pid, cmd->width, cmd->height, cmd->x, cmd->y, cmd->bg_color, cmd->title);
        wm_window_t *nw = slot_find_by_id(srv, wid);
        if (nw) wm_mark_window_dirty(&st->compositor, nw);
        struct { wm_msg_hdr_t hdr; int32_t window_id; } resp;
        resp.hdr.type       = WM_WINDOW_CREATED;
        resp.hdr.request_id = hdr->request_id;
        resp.hdr.window_id  = (uint32_t)wid;
        resp.window_id      = wid;
        ipc_send_message(msg->sender_pid, &resp, sizeof(resp));
        break;
    }
    case WM_DESTROY_WINDOW:
        wm_server_destroy_window(srv, hdr->window_id);
        break;
    case WM_SET_WINDOW_RECT: {
        struct { wm_msg_hdr_t hdr; uint32_t x, y, w, h; } *cmd = (void *)msg->data;
        wm_server_set_rect(srv, hdr->window_id, cmd->x, cmd->y, cmd->w, cmd->h);
        break;
    }
    case WM_SHOW_WINDOW:  wm_server_show(srv, hdr->window_id);  break;
    case WM_HIDE_WINDOW:  wm_server_hide(srv, hdr->window_id);  break;
    case WM_RAISE_WINDOW: wm_server_raise(srv, hdr->window_id); break;
    case WM_LOWER_WINDOW: wm_server_lower(srv, hdr->window_id); break;
    case WM_SET_FOCUS:    wm_server_set_focus(srv, hdr->window_id); break;
    case WM_GET_WINDOW_RECT: {
        struct { wm_msg_hdr_t hdr; int32_t status; uint32_t x,y,w,h; } resp;
        memset(&resp, 0, sizeof(resp));
        resp.hdr.type       = WM_GET_WINDOW_RECT;
        resp.hdr.request_id = hdr->request_id;
        resp.hdr.window_id  = hdr->window_id;
        wm_window_t *win = slot_find_by_id(srv, hdr->window_id);
        if (!win || win->owner_pid != msg->sender_pid) {
            resp.status = WM_STATUS_NOT_FOUND;
        } else {
            resp.status = WM_STATUS_OK;
            resp.x = win->x; resp.y = win->y;
            resp.w = win->w; resp.h = win->h;
        }
        ipc_send_message(msg->sender_pid, &resp, sizeof(resp));
        break;
    }
    case WM_GET_DISPLAY_INFO: {
        struct { wm_msg_hdr_t hdr; int32_t status; uint32_t width,height; } resp;
        memset(&resp, 0, sizeof(resp));
        resp.hdr.type       = WM_GET_DISPLAY_INFO;
        resp.hdr.request_id = hdr->request_id;
        resp.status         = WM_STATUS_OK;
        resp.width          = st->compositor.fb_width;
        resp.height         = st->compositor.fb_height;
        ipc_send_message(msg->sender_pid, &resp, sizeof(resp));
        break;
    }
    case WM_GET_FOCUS: {
        struct { wm_msg_hdr_t hdr; int32_t status; uint32_t focused_window_id; } resp;
        memset(&resp, 0, sizeof(resp));
        resp.hdr.type          = WM_GET_FOCUS;
        resp.hdr.request_id    = hdr->request_id;
        resp.hdr.window_id     = srv->focused_id;
        resp.status            = WM_STATUS_OK;
        resp.focused_window_id = srv->focused_id;
        ipc_send_message(msg->sender_pid, &resp, sizeof(resp));
        break;
    }
    case WM_DRAW_PIXEL: {
        struct { wm_msg_hdr_t hdr; uint32_t x, y, color; } *cmd = (void *)msg->data;
        wm_window_t *win = slot_find_by_id(srv, hdr->window_id);
        if (win && msg->size >= sizeof(*cmd)) {
            int anim_offset_y = (int)((1.0f - win->anim_alpha) * 20.0f);
            wm_compositor_mark_dirty(&st->compositor, win->x + cmd->x, win->y + anim_offset_y + WM_TITLE_HEIGHT + cmd->y, 1, 1);
        }
        break;
    }
    case WM_DRAW_RECT: {
        struct { wm_msg_hdr_t hdr; uint32_t x, y, w, h, color; } *cmd = (void *)msg->data;
        wm_window_t *win = slot_find_by_id(srv, hdr->window_id);
        if (win && msg->size >= sizeof(*cmd)) {
            if (cmd->x == 0 && cmd->y == 0 && cmd->w >= win->w && cmd->h >= win->h)
                win->bg_color = cmd->color;
            int anim_offset_y = (int)((1.0f - win->anim_alpha) * 20.0f);
            wm_compositor_mark_dirty(&st->compositor, win->x + cmd->x, win->y + anim_offset_y + WM_TITLE_HEIGHT + cmd->y, cmd->w, cmd->h);
        }
        break;
    }
    case WM_DRAW_TEXT: {
        struct { wm_msg_hdr_t hdr; uint32_t x, y; uint32_t color; float font_size; char text[WM_UI_TEXT_MAX]; } *cmd = (void *)msg->data;
        wm_window_t *win = slot_find_by_id(srv, hdr->window_id);
        if (win && msg->size >= sizeof(*cmd) && win->ui_element_count < WM_UI_MAX_ELEMENTS) {
            wm_ui_element_t *e = &win->ui_elements[win->ui_element_count++];
            e->type = WM_UI_TYPE_LABEL;
            e->x = cmd->x; e->y = cmd->y; e->w = win->w - cmd->x; e->h = (uint32_t)(cmd->font_size * 1.5f);
            e->color = cmd->color;
            e->font_size = cmd->font_size;
            uint32_t i = 0;
            while(i < WM_UI_TEXT_MAX - 1 && cmd->text[i]) { e->text[i] = cmd->text[i]; i++; }
            e->text[i] = '\0';
            int anim_offset_y = (int)((1.0f - win->anim_alpha) * 20.0f);
            wm_compositor_mark_dirty(&st->compositor, win->x + cmd->x, win->y + anim_offset_y + WM_TITLE_HEIGHT + cmd->y, e->w, e->h);
        }
        break;
    }
    case WM_SET_LAYOUT_XML_START: {
        struct { wm_msg_hdr_t hdr; uint32_t total_size; } *cmd = (void *)msg->data;
        wm_window_t *win = slot_find_by_id(srv, hdr->window_id);
        if (win && msg->size >= sizeof(*cmd)) {
            if (win->xml_buffer) free(win->xml_buffer);
            win->xml_capacity = cmd->total_size + 1;
            win->xml_buffer = (char *)malloc(win->xml_capacity);
            win->xml_size = 0;
            if (win->xml_buffer) win->xml_buffer[0] = '\0';
        }
        break;
    }
    case WM_SET_LAYOUT_XML_CHUNK: {
        wm_window_t *win = slot_find_by_id(srv, hdr->window_id);
        if (win && win->xml_buffer && msg->size > sizeof(wm_msg_hdr_t)) {
            uint32_t chunk_size = msg->size - sizeof(wm_msg_hdr_t);
            if (win->xml_size + chunk_size < win->xml_capacity) {
                memcpy(win->xml_buffer + win->xml_size, msg->data + sizeof(wm_msg_hdr_t), chunk_size);
                win->xml_size += chunk_size;
                win->xml_buffer[win->xml_size] = '\0';
            }
        }
        break;
    }
    case WM_SET_LAYOUT_XML_END: {
        wm_window_t *win = slot_find_by_id(srv, hdr->window_id);
        if (win && win->xml_buffer) {
            xml_node_t *root = xml_parse(win->xml_buffer);
            if (root) {
                win->ui_element_count = 0;
                for (uint32_t i = 0; i < root->child_count && win->ui_element_count < WM_UI_MAX_ELEMENTS; i++) {
                    xml_node_t *c = root->children[i];
                    wm_ui_element_t *e = &win->ui_elements[win->ui_element_count];

                    if (strcmp(c->tag, "Label") == 0) e->type = WM_UI_TYPE_LABEL;
                    else if (strcmp(c->tag, "Button") == 0) e->type = WM_UI_TYPE_BUTTON;
                    else if (strcmp(c->tag, "Rect") == 0) e->type = WM_UI_TYPE_RECT;
                    else if (strcmp(c->tag, "Panel") == 0) e->type = WM_UI_TYPE_PANEL;
                    else continue;

                    const char *n = xml_get_attr(c, "name");
                    const char *x = xml_get_attr(c, "x");
                    const char *y = xml_get_attr(c, "y");
                    const char *w = xml_get_attr(c, "width");
                    const char *h = xml_get_attr(c, "height");
                    const char *col = xml_get_attr(c, "color");
                    const char *bgcol = xml_get_attr(c, "bgColor");
                    const char *fsz = xml_get_attr(c, "fontSize");

                    if (n) { uint32_t idx=0; while(idx<31 && n[idx]) { e->name[idx] = n[idx]; idx++; } e->name[idx] = '\0'; }
                    
                    uint32_t x_val = 0, y_val = 0, w_val = 0, h_val = 0;
                    if (x) { const char *p=x; while(*p>='0' && *p<='9') { x_val = x_val*10 + (uint32_t)(*p-'0'); p++; } }
                    if (y) { const char *p=y; while(*p>='0' && *p<='9') { y_val = y_val*10 + (uint32_t)(*p-'0'); p++; } }
                    if (w) { const char *p=w; while(*p>='0' && *p<='9') { w_val = w_val*10 + (uint32_t)(*p-'0'); p++; } }
                    if (h) { const char *p=h; while(*p>='0' && *p<='9') { h_val = h_val*10 + (uint32_t)(*p-'0'); p++; } }
                    
                    e->x = x_val;
                    e->y = y_val;
                    e->w = w_val;
                    e->h = h_val;
                    
                    if (col) e->color = parse_hex_color(col); else e->color = 0xFFFFFFFF;
                    if (bgcol) e->bg_color = parse_hex_color(bgcol); else e->bg_color = 0x00000000;
                    
                    float fs_val = 14.0f;
                    if (fsz) {
                        uint32_t val = 0; const char *p=fsz; while(*p>='0' && *p<='9') { val = val*10 + (uint32_t)(*p-'0'); p++; }
                        fs_val = (float)val;
                    }
                    e->font_size = fs_val;
                    
                    uint32_t tidx = 0;
                    while(tidx < WM_UI_TEXT_MAX - 1 && c->text[tidx]) { e->text[tidx] = c->text[tidx]; tidx++; }
                    e->text[tidx] = '\0';
                    
                    win->ui_element_count++;
                }
                xml_free(root);
                int anim_offset_y = (int)((1.0f - win->anim_alpha) * 20.0f);
                wm_compositor_mark_dirty(&st->compositor, win->x, win->y + anim_offset_y, win->w, win->h + WM_TITLE_HEIGHT);
            }
            free(win->xml_buffer);
            win->xml_buffer = NULL;
            win->xml_size = 0;
            win->xml_capacity = 0;
        }
        break;
    }
    case WM_CLEAR_WINDOW: {
        wm_window_t *win = slot_find_by_id(srv, hdr->window_id);
        if (win) {
            win->ui_element_count = 0;
            int anim_offset_y = (int)((1.0f - win->anim_alpha) * 20.0f);
            wm_compositor_mark_dirty(&st->compositor, win->x, win->y + anim_offset_y, win->w, win->h + WM_TITLE_HEIGHT);
        }
        break;
    }
    case WM_UPDATE_COMPLETE: {
        wm_window_t *win = slot_find_by_id(srv, hdr->window_id);
        if (win) {
            int anim_offset_y = (int)((1.0f - win->anim_alpha) * 20.0f);
            wm_compositor_mark_dirty(&st->compositor, win->x, win->y + anim_offset_y, win->w, win->h + WM_TITLE_HEIGHT);
        }
        break;
    }
    case WM_SUBSCRIBE_INPUT: {
        if (msg->size >= sizeof(wm_msg_hdr_t) + sizeof(uint32_t) && srv->input_sub_count < WM_MAX_INPUT_SUBSCRIPTIONS) {
            uint32_t types;
            memcpy(&types, msg->data + sizeof(wm_msg_hdr_t), sizeof(types));
            srv->input_subs[srv->input_sub_count].subscriber_pid = msg->sender_pid;
            srv->input_subs[srv->input_sub_count].window_id      = hdr->window_id;
            srv->input_subs[srv->input_sub_count].input_types    = types;
            ++srv->input_sub_count;
        }
        break;
    }
    case WM_UNSUBSCRIBE_INPUT: {
        for (uint32_t i = 0; i < srv->input_sub_count; ++i) {
            if (srv->input_subs[i].subscriber_pid == msg->sender_pid && srv->input_subs[i].window_id == hdr->window_id) {
                srv->input_subs[i] = srv->input_subs[--srv->input_sub_count];
                break;
            }
        }
        break;
    }
    case WM_KEYBOARD_EVENT: {
        if (msg->size >= sizeof(wm_msg_hdr_t) + 8) {
            uint8_t *kd = msg->data + sizeof(wm_msg_hdr_t);
            uint16_t keycode = (uint16_t)(kd[0] | ((uint16_t)kd[1] << 8));
            uint8_t pressed = kd[2];
            uint8_t modifiers = kd[4];
            if (pressed && keycode == 0x0F && (modifiers & 0x04u)) {
                wm_window_t *cur = NULL;
                if (srv->focused_id) {
                    cur = slot_find_by_id(srv, srv->focused_id);
                }
                wm_window_t *next = NULL;
                wm_window_t *start = cur ? cur->z_next : srv->z_top;
                for (wm_window_t *w = start; w; w = w->z_next) {
                    if (w->visible && !w->is_closing && !w->is_system && !w->minimized) {
                        next = w;
                        break;
                    }
                }
                if (!next && cur) {
                    for (wm_window_t *w = srv->z_top; w && w != cur; w = w->z_next) {
                        if (w->visible && !w->is_closing && !w->is_system && !w->minimized) {
                            next = w;
                            break;
                        }
                    }
                }
                if (next && next != cur) {
                    wm_server_raise(srv, next->id);
                    wm_server_set_focus(srv, next->id);
                }
                break;
            }
        }
        wm_server_route_keyboard(srv, msg);
        break;
    }
    case WM_MOUSE_EVENT: {
        if (msg->size < sizeof(wm_msg_hdr_t) + sizeof(input_mouse_event_t)) break;
        input_mouse_event_t *me = (input_mouse_event_t *)(msg->data + sizeof(wm_msg_hdr_t));
        int16_t dx  = (int16_t)(int32_t)me->x;
        int16_t dy  = (int16_t)(int32_t)me->y;
        uint8_t  btns = me->buttons;
        uint8_t  prev = st->prev_mouse_buttons;
        int32_t new_x = (int32_t)srv->cursor_x + (int32_t)dx;
        int32_t new_y = (int32_t)srv->cursor_y + (int32_t)dy;
        if (new_x < 0) new_x = 0;
        if (new_x >= (int32_t)st->compositor.fb_width) new_x = (int32_t)st->compositor.fb_width - 1;
        if (new_y < 0) new_y = 0;
        if (new_y >= (int32_t)st->compositor.fb_height) new_y = (int32_t)st->compositor.fb_height - 1;
        uint32_t mx   = (uint32_t)new_x;
        uint32_t my   = (uint32_t)new_y;
        bool left_down = (btns & 1u) && !(prev & 1u);
        bool left_held = (btns & 1u) != 0;
        bool left_up   = !(btns & 1u) && (prev & 1u);
        wm_server_update_cursor(srv, mx, my);
        st->prev_mouse_buttons = btns;

        bool changed_hover = false;
        wm_window_t *top = srv->z_top;
        if (top && !top->is_system && !top->is_closing && top->anim_alpha >= 1.0f) {
            bool hc = false, hmax = false, hmin = false;
            if (my >= top->y && my < top->y + WM_TITLE_HEIGHT) {
                uint32_t right_off = (top->x + top->w) - mx;
                if (right_off <= WM_TITLE_BTN_W) hc = true;
                else if (right_off <= WM_TITLE_BTN_W * 2) hmax = true;
                else if (right_off <= WM_TITLE_BTN_W * 3) hmin = true;
            }
            if (top->hover_close != hc) { top->hover_close = hc; changed_hover = true; }
            if (top->hover_max != hmax) { top->hover_max = hmax; changed_hover = true; }
            if (top->hover_min != hmin) { top->hover_min = hmin; changed_hover = true; }
            if (changed_hover) wm_mark_window_title_dirty(&st->compositor, top);
        }

        if (st->resizing && left_held) {
            wm_window_t *rw = slot_find_by_id(srv, st->resize_window_id);
            if (rw && rw->anim_alpha >= 1.0f) {
                int32_t ddx = (int32_t)mx - st->resize_origin_x;
                int32_t ddy = (int32_t)my - st->resize_origin_y;
                uint32_t min_w = 120, min_h = 60;
                uint32_t nw = st->resize_orig_w, nh = st->resize_orig_h;
                if (st->resize_edge & 1u) {
                    int32_t new_w = (int32_t)st->resize_orig_w + ddx;
                    nw = (new_w > (int32_t)min_w) ? (uint32_t)new_w : min_w;
                }
                if (st->resize_edge & 2u) {
                    int32_t new_h = (int32_t)st->resize_orig_h + ddy;
                    nh = (new_h > (int32_t)min_h) ? (uint32_t)new_h : min_h;
                }
                wm_server_set_rect(srv, rw->id, rw->x, rw->y, nw, nh);
            }
            wm_server_route_mouse(srv, msg);
            break;
        }
        if (st->resizing && left_up) {
            st->resizing = false;
            st->resize_window_id = 0;
        }

        if (st->dragging && left_held) {
            wm_window_t *dw = slot_find_by_id(srv, st->drag_window_id);
            if (dw && dw->anim_alpha >= 1.0f) {
                int32_t drag_x = (int32_t)mx - st->drag_offset_x;
                int32_t drag_y = (int32_t)my - st->drag_offset_y;
                if (drag_x < 0) drag_x = 0;
                if (drag_y < 0) drag_y = 0;
                wm_server_set_rect(srv, dw->id, (uint32_t)drag_x, (uint32_t)drag_y, dw->w, dw->h);
            }
            wm_server_route_mouse(srv, msg);
            break;
        }
        if (st->dragging && left_up) {
            st->dragging = false;
            st->drag_window_id = 0;
        }
        if (left_down) {
            uint32_t tb_h = WM_TASKBAR_HEIGHT;
            uint32_t tb_margin_x = 12;
            uint32_t tb_margin_y = 6;
            uint32_t tb_y = st->compositor.fb_height - tb_h - tb_margin_y;
            uint32_t tb_x0 = tb_margin_x;
            uint32_t tb_x1 = st->compositor.fb_width - tb_margin_x;

            if (my >= tb_y && my < tb_y + tb_h && mx >= tb_x0 && mx < tb_x1) {
                uint32_t btn_w = WM_TASKBAR_BTN_W;
                uint32_t btn_gap = WM_TASKBAR_BTN_GAP;
                uint32_t vis_count = 0;
                for (uint32_t i = 0; i < srv->window_count; ++i)
                    if (srv->windows[i] && srv->windows[i]->visible && !srv->windows[i]->is_system && !srv->windows[i]->is_closing)
                        vis_count++;
                if (vis_count > 0) {
                    uint32_t total_w = vis_count * btn_w + (vis_count - 1) * btn_gap;
                    uint32_t centered_x = (st->compositor.fb_width > total_w) ? (st->compositor.fb_width - total_w) / 2 : tb_x0 + 72;
                    uint32_t min_x = tb_x0 + 8 + 48 + 16;
                    if (centered_x < min_x) centered_x = min_x;
                    uint32_t bx = centered_x;
                    for (uint32_t i = srv->window_count; i > 0; --i) {
                        wm_window_t *w = srv->windows[i - 1];
                        if (!w || !w->visible || w->is_system || w->is_closing) continue;
                        if (mx >= bx && mx < bx + btn_w) {
                            if (w->minimized) {
                                w->minimized = false;
                                wm_mark_window_dirty(&st->compositor, w);
                            } else if (srv->focused_id == w->id) {
                                w->minimized = true;
                                wm_mark_window_dirty(&st->compositor, w);
                                wm_give_focus_to_next(srv, w->id);
                                break;
                            }
                            wm_server_raise(srv, w->id);
                            wm_server_set_focus(srv, w->id);
                            break;
                        }
                        bx += btn_w + btn_gap;
                    }
                }
                break;
            }
            uint32_t hit_id = wm_server_hit_test(srv, mx, my);
            if (hit_id != 0) {
                wm_window_t *hit_w = slot_find_by_id(srv, hit_id);
                if (hit_w && !hit_w->is_system && hit_w->anim_alpha >= 1.0f) {
                    wm_server_raise(srv, hit_id);
                    wm_server_set_focus(srv, hit_id);
                    
                    uint32_t edge_threshold = 8;
                    uint32_t win_bottom = hit_w->y + WM_TITLE_HEIGHT + hit_w->h;
                    uint32_t win_right = hit_w->x + hit_w->w;
                    bool near_right  = (mx + edge_threshold >= win_right && mx <= win_right + 2);
                    bool near_bottom = (my + edge_threshold >= win_bottom && my <= win_bottom + 2);

                    if (near_right || near_bottom) {
                        st->resizing = true;
                        st->resize_window_id = hit_id;
                        st->resize_edge = (near_right ? 1u : 0u) | (near_bottom ? 2u : 0u);
                        st->resize_origin_x = (int32_t)mx;
                        st->resize_origin_y = (int32_t)my;
                        st->resize_orig_w = hit_w->w;
                        st->resize_orig_h = hit_w->h;
                    } else if (my >= hit_w->y && my < hit_w->y + WM_TITLE_HEIGHT) {
                        uint32_t right_off = (hit_w->x + hit_w->w) - mx;
                        if (right_off <= WM_TITLE_BTN_W && hit_w->w > WM_TITLE_BTN_W * 3) {
                            wm_server_destroy_window(srv, hit_id);
                            break;
                        } else if (right_off <= WM_TITLE_BTN_W * 2 && hit_w->w > WM_TITLE_BTN_W * 3) {
                            if (hit_w->maximized) {
                                hit_w->maximized = false;
                                wm_server_set_rect(srv, hit_id, hit_w->restore_x, hit_w->restore_y, hit_w->restore_w, hit_w->restore_h);
                            } else {
                                hit_w->maximized = true;
                                hit_w->restore_x = hit_w->x;
                                hit_w->restore_y = hit_w->y;
                                hit_w->restore_w = hit_w->w;
                                hit_w->restore_h = hit_w->h;
                                wm_server_set_rect(srv, hit_id, 0, 0, st->compositor.fb_width, st->compositor.fb_height - WM_TASKBAR_HEIGHT - WM_TITLE_HEIGHT);
                            }
                            break;
                        } else if (right_off <= WM_TITLE_BTN_W * 3 && hit_w->w > WM_TITLE_BTN_W * 3) {
                            hit_w->minimized = true;
                            wm_mark_window_dirty(&st->compositor, hit_w);
                            wm_give_focus_to_next(srv, hit_w->id);
                            break;
                        }
                        st->dragging        = true;
                        st->drag_window_id  = hit_id;
                        st->drag_offset_x   = (int32_t)mx - (int32_t)hit_w->x;
                        st->drag_offset_y   = (int32_t)my - (int32_t)hit_w->y;
                    }
                }
            }
        }
        wm_server_route_mouse(srv, msg);
        break;
    }
    case WM_SET_WINDOW_SYSTEM: {
        struct { wm_msg_hdr_t hdr; bool is_system; } *cmd = (void *)msg->data;
        wm_window_t *win = slot_find_by_id(srv, hdr->window_id);
        if (win) {
            win->is_system = cmd->is_system;
            if (win->is_system && srv->focused_id == win->id) wm_give_focus_to_next(srv, win->id);
            wm_mark_window_dirty(&st->compositor, win);
        }
        break;
    }
    case WM_UPDATE_CLOCK: {
        struct {
            wm_msg_header_t hdr;
            char time_str[32];
        } *cmd = (void *)msg->data;
        if (msg->size >= sizeof(wm_msg_header_t) + 8) {
            strncpy(g_clock_str, cmd->time_str, sizeof(g_clock_str) - 1);
            g_clock_str[sizeof(g_clock_str) - 1] = '\0';
            g_clock_dirty = true;
            uint32_t tb_h = WM_TASKBAR_HEIGHT;
            uint32_t tb_margin_y = 6;
            uint32_t tb_y = st->compositor.fb_height - tb_h - tb_margin_y;
            wm_compositor_mark_dirty(&st->compositor, st->compositor.fb_width - 220, tb_y, 210, tb_h);
        }
        break;
    }
    default: break;
    }
}

void wm_service_init(wm_state_t *st) {
    memset(st, 0, sizeof(*st));
    wm_server_init(&st->server);
    int32_t fd = file_open("/Userland/SystemApps/com_ImplusOS_windowmanager/Resource/Fonts/NotoSansJP-VariableFont_wght.ttf", 0);
    if (fd >= 0) {
        uint32_t fsize = 9135128 + 1024;
        st->font_buffer = (uint8_t *)malloc((size_t)fsize);
        if (st->font_buffer) {
            int64_t bytes_read = file_read(fd, st->font_buffer, fsize);

            if (bytes_read > 0) {
                if (stbtt_InitFont(&g_font_info, st->font_buffer, stbtt_GetFontOffsetForIndex(st->font_buffer, 0))) {
                    st->font_loaded = true;
                }
            }
        }
        file_close(fd);
    }
    window_register_service();
    uint32_t dw = get_display_width();
    uint32_t dh = get_display_height();
    st->server.cursor_x = dw / 2;
    st->server.cursor_y = dh / 2;
    st->server.cursor_visible = true;
    wm_compositor_init(&st->compositor, dw, dh);
    generate_background(&st->compositor);
    wm_compositor_mark_dirty(&st->compositor, 0, 0, dw, dh);
}

void wm_service_main_loop(void) {
    wm_service_init(&g_state);
    ipc_message_t msg;
    memset(&msg, 0, sizeof(msg));
    while (1) {
        while (ipc_receive_message(&msg) == 0) {
            wm_server_handle_message(&g_state, &msg);
            memset(&msg, 0, sizeof(msg));
        }
        wm_compositor_render(&g_state);
        process_yield();
    }
}

void _start(void) {
    wm_service_main_loop();
}
