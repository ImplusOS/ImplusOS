#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <File.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#pragma GCC diagnostic ignored "-Wshadow"
#pragma GCC diagnostic ignored "-Wtype-limits"
#pragma GCC diagnostic ignored "-Wunused-function"
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#define STBTT_malloc(size, user) ((void)(user), malloc(size))
#define STBTT_free(pointer, user) ((void)(user), free(pointer))
#include <Header/stb_truetype.h>
#pragma GCC diagnostic pop

#define FB_FONT_WIDTH 8
#define FB_FONT_HEIGHT 16
#define NETSURF_FONT_CACHE_SIZE 1024u
#define NETSURF_FONT_MAX_SCALE 2
#define NETSURF_FONT_MAX_PITCH NETSURF_FONT_MAX_SCALE
#define NETSURF_FONT_MAX_HEIGHT (FB_FONT_HEIGHT * NETSURF_FONT_MAX_SCALE)
#define NETSURF_FONT_MAX_BYTES \
    (NETSURF_FONT_MAX_PITCH * NETSURF_FONT_MAX_HEIGHT)

enum fb_font_style {
    FB_REGULAR = 0,
    FB_ITALIC = (1 << 0),
    FB_BOLD = (1 << 1),
    FB_BOLD_ITALIC = (FB_ITALIC | FB_BOLD)
};

typedef struct {
    uint32_t codepoint;
    uint8_t style;
    uint8_t scale;
    bool valid;
    uint8_t bitmap[NETSURF_FONT_MAX_BYTES];
} netsurf_font_cache_entry_t;

static const char *const netsurf_font_paths[] = {
    "/Userland/SystemApps/com_ImplusOS_windowmanager/Resource/Fonts/NotoSansJP-Regular.ttf",
    "/BootManager/Resource/Fonts/NotoSansJP-Regular.ttf",
    "/Userland/UserApps/NetSurf/res/NotoSansJP-Regular.ttf",
};

static uint8_t *netsurf_font_buffer;
static stbtt_fontinfo netsurf_font_info;
static bool netsurf_font_load_attempted;
static bool netsurf_font_loaded;
static netsurf_font_cache_entry_t netsurf_font_cache[NETSURF_FONT_CACHE_SIZE];

const uint8_t *__real_fb_get_glyph(uint32_t ucs4,
                                   enum fb_font_style style,
                                   int scale);

static bool netsurf_font_read_file(const char *path)
{
    file_stat_t stat;
    int32_t fd;
    uint8_t *buffer;
    int64_t read_bytes;
    int offset;

    if (file_stat(path, &stat) < 0 || stat.size == 0u) {
        return false;
    }

    fd = file_open(path, 0);
    if (fd < 0) {
        return false;
    }

    buffer = (uint8_t *)malloc(stat.size);
    if (!buffer) {
        file_close(fd);
        return false;
    }

    read_bytes = file_read(fd, buffer, stat.size);
    file_close(fd);
    if (read_bytes != (int64_t)stat.size) {
        free(buffer);
        return false;
    }

    offset = stbtt_GetFontOffsetForIndex(buffer, 0);
    if (offset < 0 || !stbtt_InitFont(&netsurf_font_info, buffer, offset)) {
        free(buffer);
        return false;
    }

    netsurf_font_buffer = buffer;
    netsurf_font_loaded = true;
    return true;
}

static bool netsurf_font_ensure_loaded(void)
{
    if (netsurf_font_loaded) {
        return true;
    }
    if (netsurf_font_load_attempted) {
        return false;
    }

    netsurf_font_load_attempted = true;
    for (size_t i = 0; i < sizeof(netsurf_font_paths) / sizeof(netsurf_font_paths[0]); ++i) {
        if (netsurf_font_read_file(netsurf_font_paths[i])) {
            return true;
        }
    }
    return false;
}

static uint32_t netsurf_font_hash(uint32_t codepoint,
                                  enum fb_font_style style,
                                  int scale)
{
    uint32_t value = codepoint * 2654435761u;
    value ^= ((uint32_t)style + 1u) * 2246822519u;
    value ^= ((uint32_t)scale + 1u) * 3266489917u;
    return value;
}

static void netsurf_font_set_bit(uint8_t *bitmap, int pitch,
                                 int x, int y, uint8_t coverage)
{
    uint8_t *row;
    uint8_t mask;

    if (coverage < 96u) {
        return;
    }

    row = bitmap + ((size_t)y * (size_t)pitch);
    mask = (uint8_t)(0x80u >> (uint32_t)(x & 7));
    row[x >> 3] = (uint8_t)(row[x >> 3] | mask);
}

static bool netsurf_font_render(uint32_t codepoint, int scale,
                                uint8_t out[NETSURF_FONT_MAX_BYTES])
{
    int cell_w;
    int cell_h;
    int pitch;
    int x0 = 0;
    int y0 = 0;
    int x1 = 0;
    int y1 = 0;
    int bitmap_w;
    int bitmap_h;
    int dst_x;
    int dst_y;
    float scale_y;
    float scale_x;
    uint8_t coverage[NETSURF_FONT_MAX_HEIGHT * FB_FONT_HEIGHT];

    if (scale < 1 || scale > NETSURF_FONT_MAX_SCALE ||
        !netsurf_font_ensure_loaded() ||
        stbtt_FindGlyphIndex(&netsurf_font_info, (int)codepoint) == 0) {
        return false;
    }

    cell_w = FB_FONT_WIDTH * scale;
    cell_h = FB_FONT_HEIGHT * scale;
    pitch = scale;
    memset(out, 0, (size_t)pitch * (size_t)cell_h);

    scale_y = stbtt_ScaleForPixelHeight(&netsurf_font_info,
                                        (float)(cell_h - 2));
    scale_x = scale_y;

    stbtt_GetCodepointBitmapBoxSubpixel(&netsurf_font_info,
                                        (int)codepoint,
                                        scale_x, scale_y,
                                        0.0f, 0.0f,
                                        &x0, &y0, &x1, &y1);
    bitmap_w = x1 - x0;
    bitmap_h = y1 - y0;
    if (bitmap_w <= 0 || bitmap_h <= 0) {
        return false;
    }
    if (bitmap_w > cell_w) {
        scale_x *= (float)cell_w / (float)bitmap_w;
        stbtt_GetCodepointBitmapBoxSubpixel(&netsurf_font_info,
                                            (int)codepoint,
                                            scale_x, scale_y,
                                            0.0f, 0.0f,
                                            &x0, &y0, &x1, &y1);
        bitmap_w = x1 - x0;
        bitmap_h = y1 - y0;
    }
    if (bitmap_w <= 0 || bitmap_h <= 0 ||
        bitmap_w > FB_FONT_WIDTH * NETSURF_FONT_MAX_SCALE ||
        bitmap_h > NETSURF_FONT_MAX_HEIGHT) {
        return false;
    }

    memset(coverage, 0, sizeof(coverage));
    stbtt_MakeCodepointBitmapSubpixel(&netsurf_font_info,
                                      coverage,
                                      bitmap_w,
                                      bitmap_h,
                                      bitmap_w,
                                      scale_x,
                                      scale_y,
                                      0.0f,
                                      0.0f,
                                      (int)codepoint);

    dst_x = (cell_w - bitmap_w) / 2;
    dst_y = (cell_h - bitmap_h) / 2;
    if (dst_x < 0) {
        dst_x = 0;
    }
    if (dst_y < 0) {
        dst_y = 0;
    }

    for (int y = 0; y < bitmap_h && y + dst_y < cell_h; ++y) {
        for (int x = 0; x < bitmap_w && x + dst_x < cell_w; ++x) {
            uint8_t value = coverage[(size_t)y * (size_t)bitmap_w + (size_t)x];
            netsurf_font_set_bit(out, pitch, x + dst_x, y + dst_y, value);
        }
    }
    return true;
}

const uint8_t *__wrap_fb_get_glyph(uint32_t ucs4,
                                   enum fb_font_style style,
                                   int scale)
{
    uint32_t slot;
    netsurf_font_cache_entry_t *entry;

    if (ucs4 < 0x80u) {
        return __real_fb_get_glyph(ucs4, style, scale);
    }
    if (scale < 1 || scale > NETSURF_FONT_MAX_SCALE) {
        return __real_fb_get_glyph(ucs4, style, scale);
    }

    slot = netsurf_font_hash(ucs4, style, scale) % NETSURF_FONT_CACHE_SIZE;
    entry = &netsurf_font_cache[slot];
    if (entry->valid &&
        entry->codepoint == ucs4 &&
        entry->style == (uint8_t)style &&
        entry->scale == (uint8_t)scale) {
        return entry->bitmap;
    }

    if (!netsurf_font_render(ucs4, scale, entry->bitmap)) {
        return __real_fb_get_glyph(ucs4, style, scale);
    }

    entry->codepoint = ucs4;
    entry->style = (uint8_t)style;
    entry->scale = (uint8_t)scale;
    entry->valid = true;
    return entry->bitmap;
}
