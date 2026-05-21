#include "Panic.h"
#include "Debug/serial/Serial.h"
#include "Debug/printf/printf.h"
#include "kernel/boot_info.h"
#include "Boot/LoadBar.h"
#include "Platform/io/IO_Main.h"
#include "Unicode/UTF8/UTF8.h"
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

static stbtt_fontinfo g_font;
static int g_font_valid = 0;

#define PANIC_BG_COLOR 0x000000AA
#define PANIC_FG_COLOR 0x00FFFFFF

static inline uint32_t alpha_blend(uint32_t dst, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    uint8_t dr = (uint8_t)((dst >> 16) & 0xFF);
    uint8_t dg = (uint8_t)((dst >> 8)  & 0xFF);
    uint8_t db = (uint8_t)((dst >> 0)  & 0xFF);
    uint8_t nr = (uint8_t)((r * a + dr * (255 - a)) / 255);
    uint8_t ng = (uint8_t)((g * a + dg * (255 - a)) / 255);
    uint8_t nb = (uint8_t)((b * a + db * (255 - a)) / 255);
    return ((uint32_t)0xFF << 24) | ((uint32_t)nr << 16) | ((uint32_t)ng << 8) | (uint32_t)nb;
}

void kernel_panic_init(BOOT_INFO* boot_info) {
    if (boot_info != NULL) {
        memcpy(&g_boot_info, boot_info, sizeof(BOOT_INFO));
        bi = &g_boot_info;
        if (bi->FontDataAddress != 0) {
            const unsigned char* data = (const unsigned char*)(uintptr_t)bi->FontDataAddress;
            int offset = stbtt_GetFontOffsetForIndex(data, 0);
            g_font_valid = stbtt_InitFont(&g_font, data, offset);
            if (g_font_valid) {
                serial_write_string("Panic font loaded successfully\n");
            } else {
                serial_write_string("Panic font initialization failed\n");
            }
        }
    }
}

static void draw_text_ttf(int x, int y, const char* text, float size, uint32_t color) {
    if (!g_font_valid || !text) return;

    float scale = stbtt_ScaleForPixelHeight(&g_font, size);

    int ascent, descent, lineGap;
    stbtt_GetFontVMetrics(&g_font, &ascent, &descent, &lineGap);
    ascent = (int)(ascent * scale);

    uint32_t* fb = (uint32_t*)bi->FrameBufferBase;
    uint32_t pitch = bi->PixelsPerScanLine;

    uint8_t r = (uint8_t)((color >> 16) & 0xFF);
    uint8_t g = (uint8_t)((color >> 8) & 0xFF);
    uint8_t b = (uint8_t)(color & 0xFF);

    int cur_x = x;

    const char* p = text;

    while (*p) {
        utf8_codepoint_t cp;
        const char* old_p = p;

        if (utf8_next(&p, NULL, &cp) != UTF8_OK) {
            cp = UTF8_REPLACEMENT_CHAR;
            p = old_p + 1;
        }

        int advance, lsb;
        int x0, y0, x1, y1;

        stbtt_GetCodepointHMetrics(&g_font, (int)cp, &advance, &lsb);

        stbtt_GetCodepointBitmapBox(
            &g_font,
            (int)cp,
            scale,
            scale,
            &x0, &y0, &x1, &y1
        );

        int char_w = x1 - x0;
        int char_h = y1 - y0;

        if (char_w > 0 &&
            char_h > 0 &&
            char_w <= 256 &&
            char_h <= 256)
        {
            memset(g_char_bitmap, 0, (size_t)(char_w * char_h));

            stbtt_MakeCodepointBitmap(
                &g_font,
                g_char_bitmap,
                char_w,
                char_h,
                char_w,
                scale,
                scale,
                (int)cp
            );

            for (int cy = 0; cy < char_h; cy++) {
                for (int cx = 0; cx < char_w; cx++) {

                    uint8_t alpha = g_char_bitmap[cy * char_w + cx];
                    if (!alpha) continue;

                    int dx = cur_x + x0 + cx;
                    int dy = y + ascent + y0 + cy;

                    if (dx >= 0 &&
                        dx < (int)bi->HorizontalResolution &&
                        dy >= 0 &&
                        dy < (int)bi->VerticalResolution)
                    {
                        fb[dy * pitch + dx] =
                            alpha_blend(
                                fb[dy * pitch + dx],
                                r, g, b,
                                alpha
                            );
                    }
                }
            }
        }

        cur_x += (int)(advance * scale);
        
        if (*p) {
            const char* tmp = p;
            utf8_codepoint_t next_cp;

            if (utf8_next(&tmp, NULL, &next_cp) == UTF8_OK) {
                cur_x += (int)(
                    stbtt_GetCodepointKernAdvance(
                        &g_font,
                        (int)cp,
                        (int)next_cp
                    ) * scale
                );
            }
        }
    }
}

void kernel_panic(const char* module_name, const char* message) {
    __asm__ volatile ("cli");
    load_bar_finish();

    if (!g_font_valid && bi != NULL && bi->FontDataAddress != 0) {
        const unsigned char* data = (const unsigned char*)(uintptr_t)bi->FontDataAddress;
        int offset = stbtt_GetFontOffsetForIndex(data, 0);
        g_font_valid = stbtt_InitFont(&g_font, data, offset);
    }

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

        int center_x = (int)width / 2;
        
        draw_text_ttf(center_x - 300, 100, ":(", 80.0f, 0xFFFFFF);
        
        draw_text_ttf(center_x - 300, 200, "コンピューターに問題が発生しました", 48.0f, 0xFFFFFF);
        draw_text_ttf(center_x - 300, 260, "OSで重大なエラーが発生しました。", 24.0f, 0xFFFFFF);
        
        char buf[512];
        if (module_name) {
            strcpy(buf, "モジュール名: ");
            strcat(buf, module_name);
            draw_text_ttf(center_x - 300, 320, buf, 20.0f, 0xCCCCCC);
        }
        
        if (message) {
            strcpy(buf, "エラー: ");
            strcat(buf, message);
            draw_text_ttf(center_x - 300, 350, buf, 20.0f, 0xFFFFFF);
        }

        draw_text_ttf(center_x - 300, 450, "ACPIが使用できないため、お使いのコンピューターを再起動してください。", 18.0f, 0xAAAAAA);
        draw_text_ttf(center_x - 300, 480, "未知の問題であり、かつあなたが開発者であれば、GithubにIssueを出してみてください。", 18.0f, 0xAAAAAA);
        draw_text_ttf(center_x - 300, 510, "URL : https://github.com/ImplusOS/ImplusOS/issues", 18.0f, 0xAAAAAA);
    }

    while (1) {
        __asm__ volatile ("hlt");
    }
}
