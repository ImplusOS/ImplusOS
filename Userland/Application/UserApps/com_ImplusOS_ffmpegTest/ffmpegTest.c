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
#define MAX_OUTPUT_W 960u
#define MAX_OUTPUT_H 540u

static window_id_t g_win = 0u;
static int g_paused = 0;
static uint32_t g_frame_count = 0u;
static uint64_t g_play_start_ms = 0u;
static int64_t g_first_pts = -1;
static char g_info_str[192];

static bool g_ui_drawn = false;
static char g_last_footer[96] = {0};

static bool g_has_audio = false;
static int64_t g_audio_bytes_written = 0;
static int g_audio_sample_rate = 0;
#define AUDIO_PCM_BUF_SIZE 8192
static uint8_t g_audio_pcm[AUDIO_PCM_BUF_SIZE];

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

static void render_frame(const ffmpeg_rgba_image_t *img,
                          uint32_t frame_num)
{
    uint32_t fb_w = 0u, fb_h = 0u;
    uint32_t *fb = window_get_backing_store(g_win, &fb_w, &fb_h);
    if (!fb || fb_w == 0u || fb_h == 0u) {
        return;
    }

    uint32_t video_area_y = HEADER_H + PAD;
    uint32_t video_area_h = fb_h > video_area_y + FOOTER_H + PAD
                                ? fb_h - video_area_y - FOOTER_H - PAD : 0u;
    uint32_t video_area_x = PAD;
    uint32_t video_area_w = fb_w > PAD * 2u ? fb_w - PAD * 2u : 0u;

    bool full_update = false;

    if (!g_ui_drawn) {
        fill_rect(fb, fb_w, fb_h, 0u, 0u, fb_w, fb_h, COLOR_BG);
        fill_rect(fb, fb_w, fb_h, 0u, 0u, fb_w, HEADER_H, COLOR_HEADER);
        
        fill_rect(fb, fb_w, fb_h, video_area_x - 1u, video_area_y - 1u, video_area_w + 2u, 1u, COLOR_BORDER);
        fill_rect(fb, fb_w, fb_h, video_area_x - 1u, video_area_y + video_area_h, video_area_w + 2u, 1u, COLOR_BORDER);
        fill_rect(fb, fb_w, fb_h, video_area_x - 1u, video_area_y, 1u, video_area_h, COLOR_BORDER);
        fill_rect(fb, fb_w, fb_h, video_area_x + video_area_w, video_area_y, 1u, video_area_h, COLOR_BORDER);
        
        g_ui_drawn = true;
        full_update = true;
    }

    if (img && img->rgba && video_area_w > 0u && video_area_h > 0u) {
        uint32_t dst_x  = video_area_x;
        uint32_t dst_y  = video_area_y;

        uint32_t copy_w = img->width;
        if (copy_w > video_area_w) copy_w = video_area_w;
        if (dst_x + copy_w > fb_w) copy_w = fb_w - dst_x;

        uint32_t copy_h = img->height;
        if (copy_h > video_area_h) copy_h = video_area_h;
        if (dst_y + copy_h > fb_h) copy_h = fb_h - dst_y;

        for (uint32_t y = 0u; y < copy_h; ++y) {
            uint32_t *dst_line = fb + (dst_y + y) * fb_w + dst_x;
            const uint32_t *src_line = (const uint32_t*)(img->rgba + (size_t)y * img->stride);
            memcpy(dst_line, src_line, copy_w * 4u);
        }
        
        window_damage(g_win, dst_x, dst_y, copy_w, copy_h);
    }

    uint32_t footer_y = fb_h > FOOTER_H ? fb_h - FOOTER_H : 0u;

    char footer[96];
    uint64_t elapsed_ms = get_uptime_ms() - g_play_start_ms;
    uint32_t sec = (uint32_t)(elapsed_ms / 1000u);
    uint32_t min = sec / 60u;

    if (g_paused) {
        snprintf(footer, sizeof(footer), "Frame: %u  %02u:%02u  [PAUSED]",
                 (unsigned)frame_num, (unsigned)min, (unsigned)(sec % 60u));
    } else {
        snprintf(footer, sizeof(footer), "Frame: %u  %02u:%02u  Audio",
                 (unsigned)frame_num, (unsigned)min, (unsigned)(sec % 60u));
    }

    if (strcmp(footer, g_last_footer) != 0 || full_update) {
        strcpy(g_last_footer, footer);
        fill_rect(fb, fb_w, fb_h, 0u, footer_y, fb_w, FOOTER_H, COLOR_HEADER);
        window_draw_text(g_win, PAD, footer_y + 8u, footer, g_paused ? COLOR_PAUSE : COLOR_DIM, 12.0f);
        window_damage(g_win, 0u, footer_y, fb_w, FOOTER_H);
    }

    if (full_update) {
        window_draw_text(g_win, PAD, 12u, "FFmpeg MP4 Player", COLOR_TEXT, 16.0f);
        window_draw_text(g_win, PAD, 30u, g_info_str, COLOR_DIM, 11.0f);
        window_damage(g_win, 0u, 0u, fb_w, HEADER_H);
    }
}

static int alloc_rgba_buffer(ffmpeg_rgba_image_t *img, uint32_t out_w, uint32_t out_h)
{
    size_t stride = (((size_t)out_w * 4u) + 63u) & ~63u;
    size_t size   = stride * (size_t)out_h + 4096u;
    uint8_t *buf  = (uint8_t*)malloc(size);
    if (!buf) return -1;
    memset(buf, 0, size);
    img->rgba   = buf;
    img->width  = out_w;
    img->height = out_h;
    img->stride = (uint32_t)stride;
    return 0;
}

int main(void)
{
    serial_write_string("[ffmpegTest] start\n");

    char error[192];
    ffmpeg_video_info_t info;

    ffmpeg_decoder_t *dec = ffmpeg_decoder_open(INPUT_PATH, &info,
                                                   error, sizeof(error));
    if (!dec) {
        serial_write_string("[ffmpegTest] open failed: ");
        serial_write_string(error);
        serial_write_string("\n");
        return 1;
    }

    double fps = (info.fps_den != 0u)
        ? (double)info.fps_num / (double)info.fps_den : 0.0;
    snprintf(g_info_str, sizeof(g_info_str),
             "%u x %u  %.2f fps  %.1f sec",
             (unsigned)info.width, (unsigned)info.height,
             fps, (double)info.duration_us / 1000000.0);

    uint32_t disp_w = get_display_width();
    uint32_t disp_h = get_display_height();
    if (disp_w == 0u) disp_w = 1920u;
    if (disp_h == 0u) disp_h = 1080u;

    uint32_t win_w = info.width  + PAD * 4u;
    uint32_t win_h = info.height + HEADER_H + FOOTER_H + PAD * 3u;
    if (win_w > (disp_w * 2u / 3u)) win_w = disp_w * 2u / 3u;
    if (win_h > (disp_h * 2u / 3u)) win_h = disp_h * 2u / 3u;
    if (win_w < 400u) win_w = 400u;
    if (win_h < 300u) win_h = 300u;

    g_win = window_create_ex(120u, 80u, win_w, win_h,
                              COLOR_BG, "FFmpeg MP4 Player");
    if (g_win == 0u) {
        ffmpeg_decoder_close(dec);
        return 1;
    }
    window_subscribe_keyboard(g_win);

    if (graphics_init(g_win) != 0) {
        ffmpeg_decoder_close(dec);
        window_destroy(g_win);
        return 1;
    }

    /* 出力サイズ計算 */
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

    ffmpeg_decoder_set_output_size(dec, out_w, out_h);

    g_has_audio = (ffmpeg_decoder_has_audio(dec) != 0);
    if (g_has_audio) {
        serial_write_string("[ffmpegTest] audio stream detected\n");
        if (os_audio_open() >= 0) {
            os_audio_info_t ainfo;
            if (os_audio_get_info(&ainfo) == 0 && ainfo.sample_rate != 0u) {
                g_audio_sample_rate = (int)ainfo.sample_rate;
                serial_write_string("[ffmpegTest] audio device: ");
                serial_write_uint64((uint64_t)ainfo.sample_rate);
                serial_write_string(" Hz, ");
                serial_write_uint64((uint64_t)ainfo.channels);
                serial_write_string(" ch\n");
                ffmpeg_decoder_set_audio_target_rate(
                    dec, (int)ainfo.sample_rate);
            }
            ffmpeg_audio_info_t a;
            if (ffmpeg_decoder_get_audio_info(dec, &a) == 0) {
                serial_write_string("[ffmpegTest] file audio: ");
                serial_write_uint64((uint64_t)a.sample_rate);
                serial_write_string(" Hz, ");
                serial_write_uint64((uint64_t)a.channels);
                serial_write_string(" ch\n");
            }
        } else {
            serial_write_string("[ffmpegTest] os_audio_open failed\n");
            g_has_audio = false;
        }
    } else {
        serial_write_string("[ffmpegTest] no audio stream\n");
    }

    ffmpeg_rgba_image_t img;
    memset(&img, 0, sizeof(img));
    int64_t pts_us = 0;
    g_frame_count  = 0u;

    if (alloc_rgba_buffer(&img, out_w, out_h) != 0) {
        serial_write_string("[ffmpegTest] malloc failed\n");
        ffmpeg_decoder_close(dec);
        window_destroy(g_win);
        return 1;
    }

    if (!img.rgba || img.width == 0u || img.height == 0u || img.stride == 0u) {
        serial_write_string("[ffmpegTest] img invalid after alloc\n");
        free(img.rgba);
        ffmpeg_decoder_close(dec);
        window_destroy(g_win);
        return 1;
    }

    /* Pre-buffer a few frames so the audio buffer isn't empty at start */
    {
        int prebuf = 0;
        while (prebuf < 5) {
            int rc = ffmpeg_decoder_read_frame(dec, &img, &pts_us);
            if (rc != 0) break;
            ++prebuf;
        }
        if (prebuf == 0) {
            serial_write_string("[ffmpegTest] initial decode failed\n");
            goto done;
        }
        g_frame_count = (uint32_t)prebuf;
    }

    g_play_start_ms = get_uptime_ms();
    g_paused        = 0;

    serial_write_string("[ffmpegTest] entering main loop\n");

    for (;;) {
        input_keyboard_event_t ev;
        while (window_input_keyboard_poll(&ev) > 0) {
            if (ev.pressed) {
                if (ev.ascii == 'q' || ev.ascii == 'Q' || ev.keycode == 41u)
                    goto done;
                if (ev.ascii == ' ' || ev.ascii == 'p' || ev.ascii == 'P')
                    g_paused = !g_paused;
            }
        }

        if (g_paused) {
            render_frame(&img, g_frame_count);
            sleep_ms(16);
            continue;
        }

        /* Write buffered audio to device */
        if (g_has_audio) {
            for (;;) {
                int n = ffmpeg_decoder_read_audio(dec, g_audio_pcm,
                                                   AUDIO_PCM_BUF_SIZE);
                if (n <= 0) break;
                int64_t written = os_audio_write(g_audio_pcm, (uint64_t)n);
                if (written > 0) g_audio_bytes_written += written;
            }
        }

        /* PTS-driven timing */
        if (g_first_pts < 0) {
            g_first_pts     = pts_us;
            g_play_start_ms = get_uptime_ms();
        }
        int64_t elapsed_ms = (int64_t)(get_uptime_ms() - g_play_start_ms);
        int64_t target_ms  = (pts_us - g_first_pts) / 1000;
        if (target_ms > elapsed_ms)
            sleep_ms((uint32_t)(target_ms - elapsed_ms));

        render_frame(&img, g_frame_count);

        if (!img.rgba || img.stride == 0u) {
            serial_write_string("[ffmpegTest] img.rgba is NULL before read_frame, aborting\n");
            goto done;
        }

        int rc = ffmpeg_decoder_read_frame(dec, &img, &pts_us);

        if (rc != 0) {
            /* Drain any remaining audio before close */
            if (g_has_audio) {
                for (;;) {
                    int n = ffmpeg_decoder_read_audio(dec, g_audio_pcm,
                                                       AUDIO_PCM_BUF_SIZE);
                    if (n <= 0) break;
                    os_audio_write(g_audio_pcm, (uint64_t)n);
                }
                os_audio_drain(250);
            }

            ffmpeg_decoder_close(dec);
            dec = NULL;

            free(img.rgba);
            memset(&img, 0, sizeof(img));

            sleep_ms(10);

            dec = ffmpeg_decoder_open(INPUT_PATH, &info, error, sizeof(error));
            if (!dec) {
                printf("ffmpegTest: failed to reopen: %s\n", error);
                break;
            }
            ffmpeg_decoder_set_output_size(dec, out_w, out_h);

            /* Re-init audio if available */
            if (g_has_audio) {
                os_audio_close();
                if (os_audio_open() < 0) { g_has_audio = false; serial_write_string("[ffmpegTest] re-open audio failed\n"); }
            }

            g_first_pts     = -1;
            g_play_start_ms = get_uptime_ms();
            g_audio_bytes_written = 0;

            if (alloc_rgba_buffer(&img, out_w, out_h) != 0) {
                serial_write_string("[ffmpegTest] realloc failed\n");
                ffmpeg_decoder_close(dec);
                dec = NULL;
                break;
            }

            if (!img.rgba || img.stride == 0u) {
                serial_write_string("[ffmpegTest] img invalid after realloc\n");
                ffmpeg_decoder_close(dec);
                dec = NULL;
                break;
            }

            int rc2 = ffmpeg_decoder_read_frame(dec, &img, &pts_us);
            if (rc2 != 0) break;
            g_frame_count = 1u;
            continue;
        }

        ++g_frame_count;
    }

done:
    if (g_has_audio) {
        os_audio_drain(250);
        os_audio_close();
    }
    if (dec) {
        ffmpeg_decoder_close(dec);
        dec = NULL;
    }
    if (img.rgba) {
        free(img.rgba);
        img.rgba = NULL;
    }
    window_destroy(g_win);
    return 0;
}

void _start(void)
{
    int32_t status = (int32_t)main();
    process_exit(status);
    for (;;) { process_yield(); }
}