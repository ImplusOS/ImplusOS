#include "../../../API/Graphics.h"
#include "../../../API/Window.h"
#include "../../../API/Process.h"
#include <string.h>

#define WATERMARK_TEXT \
    "OS Version 0.2 Beta Version\n" \
    "Build 2026.06.20\n" \
    "For Testing Only. Please do not redistribute."

#define FONT_SIZE      16.0f
#define LINE_HEIGHT    (FONT_SIZE * 1.4f)

#define BOTTOM_GAP     40
#define SIDE_GAP       10
#define PADDING_X      12
#define PADDING_Y      6

void _start(void)
{
    uint32_t sw = get_display_width();
    uint32_t sh = get_display_height();

    /* 行解析 */
    const char* lines[16];
    int line_count = 0;

    static char text[] = WATERMARK_TEXT;

    char* p = text;
    lines[line_count++] = p;

    while (*p) {
        if (*p == '\n') {
            *p = '\0';
            if (line_count < 16)
                lines[line_count++] = p + 1;
        }
        p++;
    }
    
    size_t max_len = 0;
    for (int i = 0; i < line_count; i++) {
        size_t len = strlen(lines[i]);
        if (len > max_len)
            max_len = len;
    }

    uint32_t text_w = (uint32_t)((float)max_len * FONT_SIZE * 0.55f);
    uint32_t text_h = (uint32_t)(line_count * LINE_HEIGHT);

    uint32_t win_w = text_w + PADDING_X * 2;
    uint32_t win_h = text_h + PADDING_Y * 2;

    uint32_t win_x = sw - win_w - SIDE_GAP;
    uint32_t win_y = sh - BOTTOM_GAP - win_h;

    window_id_t win = window_create_ex(
        win_x, win_y,
        win_w, win_h,
        0x00000000,
        ""
    );

    if (!win)
        return;

    window_set_system(win, 1);
    graphics_init(win);

    while (1) {
        window_clear(win);

        for (int i = 0; i < line_count; i++) {
            window_draw_text(
                win,
                PADDING_X,
                PADDING_Y + (uint32_t)(i * LINE_HEIGHT),
                lines[i],
                0x50FFFFFF,
                FONT_SIZE
            );
        }

        window_raise(win);
        process_yield();
    }
}