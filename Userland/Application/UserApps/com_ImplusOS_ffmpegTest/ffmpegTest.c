#include <FFmpeg.h>
#include <Audio.h>
#include <File.h>
#include <Graphics.h>
#include <Process.h>
#include <Window.h>
#include <Serial.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define INPUT_PATH "/Userland/UserApps/com_ImplusOS_ffmpegTest/Resource/test.mp4"

#define COLOR_BG      0xFF121A23u
#define COLOR_HEADER  0xFF203040u
#define COLOR_BORDER  0xFF35586Au
#define COLOR_TEXT    0xFFEAF8FFu
#define COLOR_DIM     0xFFA9BFCAu
#define COLOR_PAUSE   0xFF66DD88u

#define HEADER_H 44u
#define FOOTER_H 32u
#define PAD      12u
#define LOCAL_AUDIO_FRAMES 16384u
#define MAX_OUTPUT_W 960u
#define MAX_OUTPUT_H 540u

static window_id_t g_win = 0u;
static int g_paused = 0;
static uint32_t g_frame_count = 0u;
static uint64_t g_play_start_ms = 0u;
static int64_t g_first_pts = -1;
static int g_audio_open = 0;
static char g_info_str[192];

static int16_t g_audio_local[LOCAL_AUDIO_FRAMES * 2];
static uint32_t g_audio_local_count;

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

static void fill_audio(ffmpeg_decoder_t *dec)
{
    if (!g_audio_open || !dec) return;
    while (g_audio_local_count + 2048u <= LOCAL_AUDIO_FRAMES) {
        uint32_t n = ffmpeg_decoder_read_audio(dec,
            g_audio_local + g_audio_local_count * 2u, 2048);
        if (n == 0u) break;
        g_audio_local_count += n;
    }
}

static void drain_audio(void)
{
    if (!g_audio_open || g_audio_local_count < 2048u) return;
    int64_t written = os_audio_write(g_audio_local, 2048u * 2u * sizeof(int16_t));
    if (written <= 0) return;
    uint32_t fw = (uint32_t)((uint64_t)written / (2u * sizeof(int16_t)));
    if (fw == 0u) return;
    uint32_t remain = g_audio_local_count - fw;
    if (remain > 0u) {
        __builtin_memmove(g_audio_local,
                          g_audio_local + fw * 2u,
                          remain * 2u * sizeof(int16_t));
    }
    g_audio_local_count = remain;
}

static void render_frame(const ffmpeg_rgba_image_t *img,
                          uint32_t frame_num)
{
    uint32_t fb_w = 0u, fb_h = 0u;
    uint32_t *fb = window_get_backing_store(g_win, &fb_w, &fb_h);
    char dbg_buf[128];
    snprintf(dbg_buf, sizeof(dbg_buf), "[ffmpegTest] render_frame: fb=%p, fb_w=%u, fb_h=%u\n", fb, fb_w, fb_h);
    serial_write_string(dbg_buf);
    if (!fb || fb_w == 0u || fb_h == 0u) {
        serial_write_string("[ffmpegTest] render_frame: Invalid framebuffer, skipping\n");
        window_clear(g_win);
        window_draw_text(g_win, PAD, 12u, "FFmpeg MP4 Player", COLOR_TEXT, 16.0f);
        window_draw_text(g_win, PAD, 30u, g_info_str, COLOR_DIM, 11.0f);
        return;
    }
    if (img) {
        snprintf(dbg_buf, sizeof(dbg_buf), "[ffmpegTest] render_frame: img->rgba=%p, img->w=%u, img->h=%u, img->stride=%lu\n",
                 img->rgba, img->width, img->height, img->stride);
        serial_write_string(dbg_buf);
    } else {
        serial_write_string("[ffmpegTest] render_frame: img is NULL\n");
    }


    fill_rect(fb, fb_w, fb_h, 0u, 0u, fb_w, fb_h, COLOR_BG);
    fill_rect(fb, fb_w, fb_h, 0u, 0u, fb_w, HEADER_H, COLOR_HEADER);

    uint32_t video_area_y = HEADER_H + PAD;
    uint32_t video_area_h = fb_h > video_area_y + FOOTER_H + PAD
                                ? fb_h - video_area_y - FOOTER_H - PAD : 0u;
    uint32_t video_area_x = PAD;
    uint32_t video_area_w = fb_w > PAD * 2u ? fb_w - PAD * 2u : 0u;

    snprintf(dbg_buf, sizeof(dbg_buf), "[ffmpegTest] render_frame: video_area_x=%u, y=%u, w=%u, h=%u\n",
             video_area_x, video_area_y, video_area_w, video_area_h);
    serial_write_string(dbg_buf);

    if (img && img->rgba && video_area_w > 0u && video_area_h > 0u) {
        snprintf(dbg_buf, sizeof(dbg_buf), "[ffmpegTest] render_frame: src_w=%u, src_h=%u, src_stride=%lu\n",
                 img->width, img->height, img->stride);
        serial_write_string(dbg_buf);
        uint32_t draw_w = video_area_w;
        uint32_t draw_h = video_area_h;

        uint32_t dst_x = video_area_x;
        uint32_t dst_y = video_area_y;

        draw_checker(fb, fb_w, fb_h, dst_x, dst_y, draw_w, draw_h);
        serial_write_string("[ffmpegTest] checker_done\n");

        size_t src_stride = (size_t)img->stride;
        uint32_t src_w = img->width;
        uint32_t src_h = img->height;
        if (src_w == 0u || src_h == 0u) return;
        uint32_t x_frac_step = (src_w << 16u) / draw_w;
        uint32_t y_frac_step = (src_h << 16u) / draw_h;
        uint32_t y_frac = 0u;
        serial_write_string("[ffmpegTest] loop_start\n");
        for (uint32_t y = 0u; y < draw_h && dst_y + y < fb_h; ++y) {
            uint32_t *dst_line = fb + (dst_y + y) * fb_w + dst_x;
            const uint32_t *src_row = (const uint32_t*)(img->rgba + (size_t)(y_frac >> 16u) * src_stride);
            uint32_t x_frac = 0u;
            uint32_t copy_w = draw_w;
            if (dst_x + copy_w > fb_w) copy_w = fb_w - dst_x;
            for (uint32_t x = 0u; x < copy_w; ++x) {
                uint32_t sx = x_frac >> 16u;
                if (sx >= src_w) {
                    dst_line[x] = 0xFF000000; /* Black pixel for error */
                } else {
                    dst_line[x] = (src_row[sx] & 0x00FFFFFFu) | 0xFF000000u;
                }
                x_frac += x_frac_step;
            }
            y_frac += y_frac_step;
        }
        serial_write_string("[ffmpegTest] loop_done\n");

        fill_rect(fb, fb_w, fb_h, dst_x, dst_y, draw_w, 1u, COLOR_BORDER);
        fill_rect(fb, fb_w, fb_h, dst_x, dst_y + draw_h - 1u, draw_w, 1u, COLOR_BORDER);
        fill_rect(fb, fb_w, fb_h, dst_x, dst_y, 1u, draw_h, COLOR_BORDER);
        fill_rect(fb, fb_w, fb_h, dst_x + draw_w - 1u, dst_y, 1u, draw_h, COLOR_BORDER);
        serial_write_string("[ffmpegTest] borders_done\n");
    }

    uint32_t footer_y = fb_h > FOOTER_H ? fb_h - FOOTER_H : 0u;
    fill_rect(fb, fb_w, fb_h, 0u, footer_y, fb_w, FOOTER_H, COLOR_HEADER);

    serial_write_string("[ffmpegTest] before_damage\n");
    window_damage(g_win, 0u, 0u, fb_w, fb_h);
    serial_write_string("[ffmpegTest] before_draw_text\n");
    window_draw_text(g_win, PAD, 12u, "FFmpeg MP4 Player", COLOR_TEXT, 16.0f);
    serial_write_string("[ffmpegTest] title_text_done\n");
    window_draw_text(g_win, PAD, 30u, g_info_str, COLOR_DIM, 11.0f);
    serial_write_string("[ffmpegTest] info_text_done\n");

    char footer[96];
    uint64_t elapsed_ms = get_uptime_ms() - g_play_start_ms;
    uint32_t sec = (uint32_t)(elapsed_ms / 1000u);
    uint32_t min = sec / 60u;
    uint32_t ms = (uint32_t)(elapsed_ms % 1000u);

    if (g_paused) {
        snprintf(footer, sizeof(footer), "Frame: %u  %02u:%02u.%03u  [PAUSED]",
                 (unsigned)frame_num, (unsigned)min,
                 (unsigned)(sec % 60u), (unsigned)(ms / 10u * 10u));
        window_draw_text(g_win, PAD, footer_y + 8u, footer, COLOR_PAUSE, 12.0f);
    } else {
        snprintf(footer, sizeof(footer), "Frame: %u  %02u:%02u.%03u  Audio",
                 (unsigned)frame_num, (unsigned)min,
                 (unsigned)(sec % 60u), (unsigned)(ms / 10u * 10u));
        window_draw_text(g_win, PAD, footer_y + 8u, footer, COLOR_DIM, 12.0f);
    }
    serial_write_string("[ffmpegTest] render_frame_end\n");
}

static ffmpeg_decoder_t *open_audio_decoder(ffmpeg_audio_stream_info_t *ainfo)
{
    char error[192];
    if (ainfo) {
        memset(ainfo, 0, sizeof(*ainfo));
    }

    ffmpeg_decoder_t *audio_dec = ffmpeg_decoder_open(INPUT_PATH, NULL,
                                                      error, sizeof(error));
    if (!audio_dec) {
        serial_write_string("[ffmpegTest] audio decoder open failed: ");
        serial_write_string(error);
        serial_write_string("\n");
        return NULL;
    }

    if (!ainfo || ffmpeg_decoder_get_audio_info(audio_dec, ainfo) < 0 ||
        !ainfo->has_audio) {
        ffmpeg_decoder_close(audio_dec);
        return NULL;
    }

    serial_write_string("[ffmpegTest] audio decoder opened separately\n");
    return audio_dec;
}

int main(void)
{
    serial_write_string("[ffmpegTest] start\n");

    char error[192];
    ffmpeg_video_info_t info;

    serial_write_string("[ffmpegTest] opening...\n");
    ffmpeg_decoder_t *dec = ffmpeg_decoder_open(INPUT_PATH, &info,
                                                   error, sizeof(error));
    serial_write_string("[ffmpegTest] ffmpeg_decoder_open returned\n");
    if (!dec) {
        serial_write_string("[ffmpegTest] open failed: ");
        serial_write_string(error);
        serial_write_string("\n");
        return 1;
    }
    serial_write_string("[ffmpegTest] opened\n");

    ffmpeg_audio_stream_info_t ainfo;
    memset(&ainfo, 0, sizeof(ainfo));
    ffmpeg_decoder_t *audio_dec = open_audio_decoder(&ainfo);

    double fps = 0.0;
    if (info.fps_den != 0u) {
        fps = (double)info.fps_num / (double)info.fps_den;
    }
    if (ainfo.has_audio) {
        snprintf(g_info_str, sizeof(g_info_str),
                 "%u x %u  %.2f fps  %.1f sec  %u Hz %u ch",
                 (unsigned)info.width, (unsigned)info.height,
                 fps,
                 (double)info.duration_us / 1000000.0,
                 (unsigned)ainfo.sample_rate,
                 (unsigned)ainfo.channels);
    } else {
        snprintf(g_info_str, sizeof(g_info_str),
                 "%u x %u  %.2f fps  %.1f sec",
                 (unsigned)info.width, (unsigned)info.height,
                 fps,
                 (double)info.duration_us / 1000000.0);
    }

    if (ainfo.has_audio) {
        int rc = os_audio_open();
        if (rc >= 0) {
            g_audio_open = 1;
            serial_write_string("[ffmpegTest] audio device opened\n");
        } else {
            char dbg[48];
            snprintf(dbg, sizeof(dbg), "[ffmpegTest] audio unavailable %d\n", rc);
            serial_write_string(dbg);
            ffmpeg_decoder_close(audio_dec);
            audio_dec = NULL;
        }
    }

    uint32_t disp_w = get_display_width();
    uint32_t disp_h = get_display_height();
    if (disp_w == 0u) disp_w = 1920u;
    if (disp_h == 0u) disp_h = 1080u;

    uint32_t win_w = info.width + PAD * 4u;
    uint32_t win_h = info.height + HEADER_H + FOOTER_H + PAD * 3u;
    if (win_w > (disp_w * 2u / 3u)) win_w = disp_w * 2u / 3u;
    if (win_h > (disp_h * 2u / 3u)) win_h = disp_h * 2u / 3u;
    if (win_w < 400u) win_w = 400u;
    if (win_h < 300u) win_h = 300u;

    g_win = window_create_ex(120u, 80u, win_w, win_h,
                              COLOR_BG, "FFmpeg MP4 Player");
    if (g_win == 0u) {
        printf("ffmpegTest: failed to create window\n");
        if (g_audio_open) os_audio_close();
        ffmpeg_decoder_close(audio_dec);
        ffmpeg_decoder_close(dec);
        return 1;
    }
    window_subscribe_keyboard(g_win);
    graphics_init(g_win);

    uint32_t video_area_y = HEADER_H + PAD;
    uint32_t video_area_h = win_h > video_area_y + FOOTER_H + PAD
                                ? win_h - video_area_y - FOOTER_H - PAD : 0u;
    uint32_t video_area_w = win_w > PAD * 2u ? win_w - PAD * 2u : 0u;

    uint32_t out_w = info.width;
    uint32_t out_h = info.height;
    if (out_w > video_area_w || out_h > video_area_h) {
        if ((uint64_t)video_area_w * (uint64_t)info.height <=
            (uint64_t)video_area_h * (uint64_t)info.width) {
            out_w = video_area_w;
            out_h = (uint32_t)(((uint64_t)info.height * video_area_w) / info.width);
        } else {
            out_h = video_area_h;
            out_w = (uint32_t)(((uint64_t)info.width * video_area_h) / info.height);
        }
    }
    if (out_w > MAX_OUTPUT_W || out_h > MAX_OUTPUT_H) {
        if ((uint64_t)MAX_OUTPUT_W * (uint64_t)out_h <=
            (uint64_t)MAX_OUTPUT_H * (uint64_t)out_w) {
            out_h = (uint32_t)(((uint64_t)out_h * MAX_OUTPUT_W) / out_w);
            out_w = MAX_OUTPUT_W;
        } else {
            out_w = (uint32_t)(((uint64_t)out_w * MAX_OUTPUT_H) / out_h);
            out_h = MAX_OUTPUT_H;
        }
    }
    if (out_w == 0u) out_w = 1u;
    if (out_h == 0u) out_h = 1u;

    printf("ffmpegTest: window %ux%u, display %ux%u, output %ux%u\n",
           (unsigned)win_w, (unsigned)win_h,
           (unsigned)disp_w, (unsigned)disp_h,
           (unsigned)out_w, (unsigned)out_h);

    ffmpeg_decoder_set_output_size(dec, out_w, out_h);

    ffmpeg_rgba_image_t img;
    memset(&img, 0, sizeof(img));
    int64_t pts_us = 0;
    g_frame_count = 0u;

    int rc = ffmpeg_decoder_read_frame(dec, &img, &pts_us);
    if (rc != 0) {
        serial_write_string("[ffmpegTest] initial decode failed\n");
        goto done;
    }
    serial_write_string("[ffmpegTest] first frame decoded\n");
    ++g_frame_count;
    fill_audio(audio_dec);
    drain_audio();

    g_play_start_ms = get_uptime_ms();
    g_paused = 0;

    for (;;) {
        input_keyboard_event_t ev;
        while (window_input_keyboard_poll(&ev) > 0) {
            if (ev.pressed) {
                if (ev.ascii == 'q' || ev.ascii == 'Q' || ev.keycode == 41u) {
                    goto done;
                }
                if (ev.ascii == ' ' || ev.ascii == 'p' || ev.ascii == 'P') {
                    g_paused = !g_paused;
                }
            }
        }

        if (g_paused) {
            render_frame(&img, g_frame_count);
            sleep_ms(16);
            continue;
        }

        if (g_first_pts < 0) {
            g_first_pts = pts_us;
            g_play_start_ms = get_uptime_ms();
        }
        int64_t elapsed_ms = (int64_t)(get_uptime_ms() - g_play_start_ms);
        int64_t target_ms = (pts_us - g_first_pts) / 1000;
        if (target_ms > elapsed_ms) {
            sleep_ms((uint32_t)(target_ms - elapsed_ms));
        }

        render_frame(&img, g_frame_count);

        serial_write_string("[ffmpegTest] calling_read_frame\n");
        if (!dec) {
            serial_write_string("[ffmpegTest] dec is NULL before call!\n");
            return 1;
        }
        char frame_dbg[128];
        snprintf(frame_dbg, sizeof(frame_dbg), "[ffmpegTest] dec=%p, img=%p, img->rgba=%p\n",
                 dec, &img, img.rgba);
        serial_write_string(frame_dbg);
        rc = ffmpeg_decoder_read_frame(dec, &img, &pts_us);
        if (g_frame_count < 4u) {
            char frame_rc_dbg[48];
            snprintf(frame_rc_dbg, sizeof(frame_rc_dbg), "[ffmpegTest] frame %u rc=%d\n",
                     (unsigned)g_frame_count, rc);
            serial_write_string(frame_rc_dbg);
        }

        if (rc == 1) {
            if (g_audio_open) {
                os_audio_drain(500);
            }
            
            // 完全にコンテキストをクリアし、メモリリークや不正ポインタを回避
            ffmpeg_decoder_close(dec);
            dec = NULL;
            if (audio_dec) {
                ffmpeg_decoder_close(audio_dec);
                audio_dec = NULL;
            }
            
            // 少し待機してデコーダの状態を確実にクリーンアップする
            sleep_ms(10);
            
            /* Free the RGBA pixel buffer that belongs to the caller (us) and
             * reset the image struct. */
            free(img.rgba);
            img.rgba = NULL;
            img.width = 0u;
            img.height = 0u;
            
            dec = ffmpeg_decoder_open(INPUT_PATH, &info, error, sizeof(error));
            if (!dec) {
                printf("ffmpegTest: failed to reopen: %s\n", error);
                break;
            }
            ffmpeg_decoder_set_output_size(dec, out_w, out_h);
            if (g_audio_open) {
                audio_dec = open_audio_decoder(&ainfo);
                if (!audio_dec) {
                    os_audio_close();
                    g_audio_open = 0;
                }
            }
            g_first_pts = -1;
            g_play_start_ms = get_uptime_ms();
            g_audio_local_count = 0u;

            rc = ffmpeg_decoder_read_frame(dec, &img, &pts_us);
            if (rc != 0) break;
            g_frame_count = 1u;
            fill_audio(audio_dec);
            drain_audio();
            continue;
        }

        if (rc < 0) {
            sleep_ms(1);
            continue;
        }

        ++g_frame_count;
        fill_audio(audio_dec);
        drain_audio();
    }

done:
    if (g_audio_open) {
        os_audio_drain(500);
        os_audio_close();
    }
    ffmpeg_decoder_close(audio_dec);
    ffmpeg_decoder_close(dec);
    /* Free the RGBA pixel buffer owned by the main loop. */
    free(img.rgba);
    img.rgba = NULL;
    window_destroy(g_win);
    return 0;
}

void _start(void)
{
    int32_t status = (int32_t)main();
    process_exit(status);
    for (;;) {
        process_yield();
    }
}