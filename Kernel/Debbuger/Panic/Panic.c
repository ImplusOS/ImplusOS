#include "Panic.h"
#include "../printf/printf.h"
#include "../../Kernel_Main.h"
#include "../../IO/IO_Main.h"
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <math.h>

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#define STB_TRUETYPE_NO_STDIO
#define STBTT_assert(x)

#define STBTT_memcpy  memcpy
#define STBTT_memset  memset
#define STBTT_memmove memmove
#define STBTT_strlen  strlen

#define STBTT_sqrt(x)   sqrt(x)
#define STBTT_pow(x,y)  pow(x,y)
#define STBTT_fmod(x,y) fmod(x,y)
#define STBTT_cos(x)    cos(x)
#define STBTT_acos(x)   acos(x)
#define STBTT_fabs(x)   fabs(x)
#define STBTT_ifloor(x) ((int)floor(x))
#define STBTT_iceil(x)  ((int)ceil(x))

static uint8_t g_char_bitmap[256 * 256];

static BOOT_INFO g_boot_info;
static BOOT_INFO* bi = NULL;

#include "../../../Thirdparty/stb_truetype.h"

extern const uint8_t g_panic_font8x8[128][8];

#define PANIC_BG_COLOR 0x000000AA
#define PANIC_FG_COLOR 0x00FFFFFF

static inline uint32_t alpha_blend(uint32_t dst, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    uint8_t dr = (uint8_t)((dst >> 16) & 0xFF);
    uint8_t dg = (uint8_t)((dst >> 8)  & 0xFF);
    uint8_t db = (uint8_t)((dst >> 0)  & 0xFF);
    uint8_t nr = (uint8_t)((r * a + dr * (255 - a)) / 255);
    uint8_t ng = (uint8_t)((g * a + dg * (255 - a)) / 255);
    uint8_t nb = (uint8_t)((b * a + db * (255 - a)) / 255);
    return (0xFF << 24) | ((uint32_t)nr << 16) | ((uint32_t)ng << 8) | nb;
}

static void panic_init(BOOT_INFO* boot_info) {
    if (boot_info != NULL) {
        memcpy(&g_boot_info, boot_info, sizeof(BOOT_INFO));
        bi = &g_boot_info;
    }
}

static void draw_text_ttf(int x, int y, const char* text, float size, uint32_t color) {
    if (!bi->FontDataAddress || bi->FontDataSize == 0) {
        return;
    }

    stbtt_fontinfo font;
    if (!stbtt_InitFont(&font, (unsigned char*)bi->FontDataAddress, 
            stbtt_GetFontOffsetForIndex((unsigned char*)bi->FontDataAddress, 0))) {
        return;
    }

    float scale = stbtt_ScaleForPixelHeight(&font, size);
    int ascent, descent, lineGap;
    stbtt_GetFontVMetrics(&font, &ascent, &descent, &lineGap);
    ascent = (int)(ascent * scale);

    uint32_t* fb = (uint32_t*)bi->FrameBufferBase;
    uint32_t pitch = bi->PixelsPerScanLine;
    uint8_t r = (uint8_t)((color >> 16) & 0xFF);
    uint8_t g = (uint8_t)((color >> 8) & 0xFF);
    uint8_t b = (uint8_t)(color & 0xFF);

    int cur_x = x;
    for (int i = 0; text[i]; i++) {
        int advance, lsb, x0, y0, x1, y1;
        stbtt_GetCodepointHMetrics(&font, text[i], &advance, &lsb);
        stbtt_GetCodepointBitmapBox(&font, text[i], scale, scale, &x0, &y0, &x1, &y1);

        int char_w = x1 - x0;
        int char_h = y1 - y0;

        if (char_w > 0 && char_h > 0 && char_w <= 256 && char_h <= 256) {
            memset(g_char_bitmap, 0, (size_t)(char_w * char_h));
            stbtt_MakeCodepointBitmap(&font, g_char_bitmap, char_w, char_h, char_w, scale, scale, text[i]);

            for (int cy = 0; cy < char_h; cy++) {
                for (int cx = 0; cx < char_w; cx++) {
                    uint8_t alpha = g_char_bitmap[cy * char_w + cx];
                    if (alpha == 0) continue;

                    int dx = cur_x + x0 + cx;
                    int dy = y + ascent + y0 + cy;

                    if (dx >= 0 && dx < (int)bi->HorizontalResolution && 
                        dy >= 0 && dy < (int)bi->VerticalResolution) {
                        fb[dy * pitch + dx] = alpha_blend(fb[dy * pitch + dx], r, g, b, alpha);
                    }
                }
            }
        }
        cur_x += (int)(advance * scale);
        if (text[i + 1]) {
            cur_x += (int)(stbtt_GetCodepointKernAdvance(&font, text[i], text[i + 1]) * scale);
        }
    }
}

void kernel_panic(const char* module_name, const char* message) {
    __asm__ volatile ("cli");

    if (bi != NULL && bi->FrameBufferBase != 0) {
        uint32_t* fb = (uint32_t*)(uintptr_t)bi->FrameBufferBase;
        uint32_t width = bi->HorizontalResolution;
        uint32_t height = bi->VerticalResolution;
        uint32_t pitch = bi->PixelsPerScanLine;

        uint32_t bg_color = 0x00004488;
        for (uint32_t y = 0; y < height; y++) {
            for (uint32_t x = 0; x < width; x++) {
                fb[y * pitch + x] = bg_color;
            }
        }

        if (bi->FontDataAddress && bi->FontDataSize > 0) {
            int center_x = (int)width / 2;
            
            draw_text_ttf(center_x - 40, 100, ":(", 80.0f, 0xFFFFFF);
            
            draw_text_ttf(center_x - 300, 200, "Your PC ran into a problem", 48.0f, 0xFFFFFF);
            draw_text_ttf(center_x - 300, 260, "ImplusOS has encountered a critical error.", 24.0f, 0xFFFFFF);
            
            char buf[512];
            if (module_name) {
                strcpy(buf, "Module: ");
                strcat(buf, module_name);
                draw_text_ttf(center_x - 300, 320, buf, 20.0f, 0xCCCCCC);
            }
            
            if (message) {
                strcpy(buf, "Error: ");
                strcat(buf, message);
                draw_text_ttf(center_x - 300, 350, buf, 20.0f, 0xFFFFFF);
            }

            draw_text_ttf(center_x - 300, 450, "Please reboot your computer manually.", 18.0f, 0xAAAAAA);
        }
    }

    while (1) {
        __asm__ volatile ("hlt");
    }
}
