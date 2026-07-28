#pragma once

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint8_t *rgba;
} ffmpeg_rgba_image_t;

int ffmpeg_decode_thumbnail_from_file(const char *path,
                                      uint32_t max_width,
                                      uint32_t max_height,
                                      uint64_t seek_us,
                                      ffmpeg_rgba_image_t *out_image,
                                      char *error_out,
                                      size_t error_out_size);
void ffmpeg_free_image(ffmpeg_rgba_image_t *image);

typedef struct ffmpeg_decoder_s ffmpeg_decoder_t;

typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t fps_num;
    uint32_t fps_den;
    uint64_t duration_us;
} ffmpeg_video_info_t;

ffmpeg_decoder_t *ffmpeg_decoder_open(const char *path,
                                       ffmpeg_video_info_t *info_out,
                                       char *error_out,
                                       size_t error_out_size);
void ffmpeg_decoder_set_output_size(ffmpeg_decoder_t *dec,
                                     uint32_t width, uint32_t height);
int ffmpeg_decoder_read_frame(ffmpeg_decoder_t *dec,
                               ffmpeg_rgba_image_t *out_image,
                               int64_t *pts_us);

typedef struct {
    int has_audio;
    int audio_stream_index;
    uint32_t sample_rate;
    uint8_t channels;
} ffmpeg_audio_stream_info_t;

int ffmpeg_decoder_get_audio_info(ffmpeg_decoder_t *dec,
                                   ffmpeg_audio_stream_info_t *info);
uint32_t ffmpeg_decoder_read_audio(ffmpeg_decoder_t *dec,
                                    int16_t *pcm, uint32_t max_frames);

void ffmpeg_decoder_close(ffmpeg_decoder_t *dec);
