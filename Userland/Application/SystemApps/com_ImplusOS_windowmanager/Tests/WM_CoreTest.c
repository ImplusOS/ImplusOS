#include "../Compositor/WM_Damage.h"
#include "../Compositor/WM_Raster.h"
#include "../Core/WM_EventQueue.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_damage_merge_and_clip(void)
{
    wm_region_t region;
    wm_region_reset(&region);
    wm_rect_t bounds = {0, 0, 100u, 100u};
    wm_region_add(&region, (wm_rect_t){10, 10, 20u, 20u}, bounds);
    wm_region_add(&region, (wm_rect_t){25, 25, 20u, 20u}, bounds);
    assert(region.count == 1u);
    assert(region.rects[0].x == 10);
    assert(region.rects[0].y == 10);
    assert(region.rects[0].w == 35u);
    assert(region.rects[0].h == 35u);

    wm_region_add(&region, (wm_rect_t){-10, -10, 15u, 15u}, bounds);
    assert(region.count == 2u);
    assert(region.rects[1].x == 0);
    assert(region.rects[1].y == 0);
    assert(region.rects[1].w == 5u);
    assert(region.rects[1].h == 5u);
}

static void test_damage_overflow(void)
{
    wm_region_t region;
    wm_region_reset(&region);
    wm_rect_t bounds = {0, 0, 1000u, 1000u};
    for (uint32_t i = 0; i <= WM_MAX_DAMAGE_RECTS; ++i)
        wm_region_add(&region,
            (wm_rect_t){(int32_t)(i * 20u), 10, 2u, 2u}, bounds);
    assert(region.full);
    assert(region.count == 0u);
}

static void test_raster_clip_and_blend(void)
{
    uint32_t pixels[8u * 8u];
    memset(pixels, 0, sizeof(pixels));
    wm_canvas_t canvas;
    wm_canvas_init(&canvas, pixels, 8u, 8u, 8u);
    wm_canvas_set_clip(&canvas, (wm_rect_t){2, 2, 4u, 4u});
    wm_canvas_fill(&canvas, (wm_rect_t){0, 0, 8u, 8u}, 0xFFFF0000u);
    assert(pixels[0] == 0u);
    assert(pixels[2u * 8u + 2u] == 0xFFFF0000u);
    assert(pixels[5u * 8u + 5u] == 0xFFFF0000u);
    assert(pixels[6u * 8u + 6u] == 0u);

    wm_canvas_set_clip(&canvas, (wm_rect_t){0, 0, 8u, 8u});
    pixels[3u * 8u + 3u] = 0xFF0000FFu;
    wm_canvas_put(&canvas, 3, 3, 0x80FF0000u);
    assert(pixels[3u * 8u + 3u] != 0xFF0000FFu);
    assert(pixels[3u * 8u + 3u] != 0x80FF0000u);
}

static void test_rounded_corners(void)
{
    uint32_t pixels[10u * 10u];
    memset(pixels, 0, sizeof(pixels));
    wm_canvas_t canvas;
    wm_canvas_init(&canvas, pixels, 10u, 10u, 10u);
    wm_canvas_fill_rounded(&canvas, (wm_rect_t){1, 1, 8u, 8u}, 3u, 0xFFFFFFFFu);
    assert((pixels[1u * 10u + 1u] >> 24u) <= 96u);
    assert(pixels[4u * 10u + 4u] == 0xFFFFFFFFu);
    assert((pixels[1u * 10u + 3u] >> 24u) > 0u);
}

static void test_blur_and_icon_fallback(void)
{
    uint32_t pixels[12u * 12u];
    for (uint32_t i = 0u; i < 12u * 12u; ++i)
        pixels[i] = (i % 2u) ? 0xFFFFFFFFu : 0xFF000000u;
    wm_canvas_t canvas;
    wm_canvas_init(&canvas, pixels, 12u, 12u, 12u);
    uint32_t before = pixels[5u * 12u + 5u];
    wm_canvas_blur(&canvas, (wm_rect_t){2, 2, 8u, 8u}, 2u);
    assert(pixels[5u * 12u + 5u] != before);

    memset(pixels, 0, sizeof(pixels));
    wm_canvas_draw_icon(&canvas, (wm_rect_t){2, 2, 4u, 4u}, NULL, 255u, 0u, 0xFFFFFFFFu);
    assert(pixels[2u * 12u + 2u] == 0xFFFFFFFFu);
    assert(pixels[5u * 12u + 5u] == 0xFFFFFFFFu);
}

static void test_event_queue(void)
{
    wm_event_queue_t queue;
    wm_event_queue_init(&queue);
    ipc_message_t input;
    memset(&input, 0, sizeof(input));
    input.sender_pid = 42;
    input.size = 4u;
    input.data[0] = 'T';
    assert(wm_event_queue_push(&queue, &input));
    ipc_message_t output;
    assert(wm_event_queue_pop(&queue, &output));
    assert(output.sender_pid == 42);
    assert(output.data[0] == 'T');
    assert(!wm_event_queue_pop(&queue, &output));

    for (uint32_t i = 0u; i < WM_EVENT_QUEUE_SIZE; ++i) {
        input.sender_pid = (int32_t)i;
        assert(wm_event_queue_push(&queue, &input));
    }
    input.sender_pid = 999;
    assert(!wm_event_queue_push(&queue, &input));
    assert(queue.dropped == 1u);
    assert(wm_event_queue_pop(&queue, &output));
    assert(output.sender_pid == 0);
    assert(wm_event_queue_push(&queue, &input));
}

int main(void)
{
    test_damage_merge_and_clip();
    test_damage_overflow();
    test_raster_clip_and_blend();
    test_rounded_corners();
    test_blur_and_icon_fallback();
    test_event_queue();
    return 0;
}
