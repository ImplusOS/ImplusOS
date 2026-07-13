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
