#include "FFmpeg.h"

#include <File.h>
#include <Process.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libswscale/swscale.h>

typedef struct {
    int32_t fd;
    int64_t size;
} ffmpeg_file_io_t;

static void ffmpeg_set_error(char *error_out, size_t error_out_size,
                             const char *message)
{
    if (!error_out || error_out_size == 0u) {
        return;
    }

    if (!message) {
        message = "ffmpeg error";
    }

    strncpy(error_out, message, error_out_size - 1u);
    error_out[error_out_size - 1u] = '\0';
}

static void ffmpeg_set_error_code(char *error_out, size_t error_out_size,
                                  int code, const char *prefix)
{
    char av_error[128];
    av_error[0] = '\0';
    av_strerror(code, av_error, sizeof(av_error));

    if (!prefix || prefix[0] == '\0') {
        ffmpeg_set_error(error_out, error_out_size, av_error);
        return;
    }

    if (!error_out || error_out_size == 0u) {
        return;
    }

    snprintf(error_out, error_out_size, "%s: %s", prefix, av_error);
}

static int ffmpeg_read_packet(void *opaque, uint8_t *buf, int buf_size)
{
    ffmpeg_file_io_t *io = (ffmpeg_file_io_t *)opaque;
    if (!io || io->fd < 0 || !buf || buf_size <= 0) {
        return AVERROR(EINVAL);
    }

    int64_t read_bytes = file_read(io->fd, buf, (uint64_t)buf_size);
    if (read_bytes < 0) {
        return AVERROR(EIO);
    }

    if (read_bytes == 0) {
        return AVERROR_EOF;
    }

    if (read_bytes > (int64_t)INT32_MAX) {
        return AVERROR(EIO);
    }

    return (int)read_bytes;
}

static int64_t ffmpeg_seek(void *opaque, int64_t offset, int whence)
{
    ffmpeg_file_io_t *io = (ffmpeg_file_io_t *)opaque;
    if (!io || io->fd < 0) {
        return AVERROR(EINVAL);
    }

    if (whence == AVSEEK_SIZE) {
        return io->size;
    }

    whence &= ~AVSEEK_FORCE;

    int32_t os_whence;
    switch (whence) {
    case SEEK_SET:
    case SEEK_CUR:
    case SEEK_END:
        os_whence = whence;
        break;
    default:
        return AVERROR(EINVAL);
    }

    int64_t result = file_seek(io->fd, offset, os_whence);
    if (result < 0) {
        return AVERROR(EIO);
    }

    return result;
}

static int ffmpeg_compute_thumbnail_size(uint32_t src_w, uint32_t src_h,
                                         uint32_t max_w, uint32_t max_h,
                                         uint32_t *out_w, uint32_t *out_h)
{
    if (!out_w || !out_h || src_w == 0u || src_h == 0u ||
        max_w == 0u || max_h == 0u) {
        return -1;
    }

    uint32_t dst_w = src_w;
    uint32_t dst_h = src_h;

    if (src_w > max_w || src_h > max_h) {
        if ((uint64_t)max_w * (uint64_t)src_h <=
            (uint64_t)max_h * (uint64_t)src_w) {
            dst_w = max_w;
            dst_h = (uint32_t)(((uint64_t)src_h * (uint64_t)max_w +
                                (uint64_t)(src_w / 2u)) / (uint64_t)src_w);
        } else {
            dst_h = max_h;
            dst_w = (uint32_t)(((uint64_t)src_w * (uint64_t)max_h +
                                (uint64_t)(src_h / 2u)) / (uint64_t)src_h);
        }
    }

    if (dst_w == 0u) dst_w = 1u;
    if (dst_h == 0u) dst_h = 1u;
    *out_w = dst_w;
    *out_h = dst_h;
    return 0;
}

void ffmpeg_free_image(ffmpeg_rgba_image_t *image)
{
    if (!image) {
        return;
    }

    free(image->rgba);
    image->rgba = NULL;
    image->width = 0u;
    image->height = 0u;
    image->stride = 0u;
}

int ffmpeg_decode_thumbnail_from_file(const char *path,
                                      uint32_t max_width,
                                      uint32_t max_height,
                                      uint64_t seek_us,
                                      ffmpeg_rgba_image_t *out_image,
                                      char *error_out,
                                      size_t error_out_size)
{
    if (out_image) {
        memset(out_image, 0, sizeof(*out_image));
    }

    if (!path || !out_image || max_width == 0u || max_height == 0u) {
        ffmpeg_set_error(error_out, error_out_size, "invalid thumbnail request");
        return -1;
    }

    file_stat_t stat;
    if (file_stat(path, &stat) < 0 || !stat.exists || stat.is_dir) {
        ffmpeg_set_error(error_out, error_out_size, "input file not found");
        return -1;
    }

    ffmpeg_file_io_t io;
    memset(&io, 0, sizeof(io));
    io.fd = file_open(path, 0);
    if (io.fd < 0) {
        ffmpeg_set_error(error_out, error_out_size, "failed to open input");
        return -1;
    }
    io.size = (int64_t)stat.size;

    int rc = -1;
    int format_opened = 0;
    uint8_t *avio_buffer = NULL;
    AVIOContext *avio = NULL;
    AVFormatContext *format_ctx = NULL;
    AVCodecContext *codec_ctx = NULL;
    AVPacket *packet = NULL;
    AVFrame *frame = NULL;
    struct SwsContext *sws = NULL;
    uint8_t *rgba = NULL;

    avio_buffer = (uint8_t *)av_malloc(32u * 1024u);
    if (!avio_buffer) {
        ffmpeg_set_error(error_out, error_out_size, "failed to allocate I/O buffer");
        goto cleanup;
    }

    avio = avio_alloc_context(avio_buffer, 32 * 1024, 0, &io,
                              ffmpeg_read_packet, NULL, ffmpeg_seek);
    if (!avio) {
        ffmpeg_set_error(error_out, error_out_size, "failed to create I/O context");
        goto cleanup;
    }

    format_ctx = avformat_alloc_context();
    if (!format_ctx) {
        ffmpeg_set_error(error_out, error_out_size, "failed to allocate format context");
        goto cleanup;
    }
    format_ctx->pb = avio;
    format_ctx->flags |= AVFMT_FLAG_CUSTOM_IO;

    rc = avformat_open_input(&format_ctx, NULL, NULL, NULL);
    if (rc < 0) {
        ffmpeg_set_error_code(error_out, error_out_size, rc, "avformat_open_input");
        goto cleanup;
    }
    format_opened = 1;

    rc = avformat_find_stream_info(format_ctx, NULL);
    if (rc < 0) {
        ffmpeg_set_error_code(error_out, error_out_size, rc,
                              "avformat_find_stream_info");
        goto cleanup;
    }

    int stream_index = av_find_best_stream(format_ctx, AVMEDIA_TYPE_VIDEO, -1, -1,
                                           NULL, 0);
    if (stream_index < 0) {
        ffmpeg_set_error_code(error_out, error_out_size, stream_index,
                              "av_find_best_stream");
        goto cleanup;
    }

    AVStream *stream = format_ctx->streams[stream_index];
    const AVCodec *codec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!codec) {
        ffmpeg_set_error(error_out, error_out_size, "video decoder not available");
        goto cleanup;
    }

    codec_ctx = avcodec_alloc_context3(codec);
    if (!codec_ctx) {
        ffmpeg_set_error(error_out, error_out_size, "failed to allocate codec context");
        goto cleanup;
    }

    rc = avcodec_parameters_to_context(codec_ctx, stream->codecpar);
    if (rc < 0) {
        ffmpeg_set_error_code(error_out, error_out_size, rc,
                              "avcodec_parameters_to_context");
        goto cleanup;
    }

    rc = avcodec_open2(codec_ctx, codec, NULL);
    if (rc < 0) {
        ffmpeg_set_error_code(error_out, error_out_size, rc, "avcodec_open2");
        goto cleanup;
    }

    int64_t seek_target = 0;
    if (seek_us > 0u) {
        seek_target = av_rescale_q((int64_t)seek_us, AV_TIME_BASE_Q, stream->time_base);
    } else if (format_ctx->duration > 0) {
        seek_target = av_rescale_q(format_ctx->duration / 4, AV_TIME_BASE_Q,
                                   stream->time_base);
    }

    if (av_seek_frame(format_ctx, stream_index, seek_target,
                      AVSEEK_FLAG_BACKWARD) >= 0) {
        avcodec_flush_buffers(codec_ctx);
    } else {
        av_seek_frame(format_ctx, stream_index, 0, AVSEEK_FLAG_BACKWARD);
        avcodec_flush_buffers(codec_ctx);
    }

    packet = av_packet_alloc();
    frame = av_frame_alloc();
    if (!packet || !frame) {
        ffmpeg_set_error(error_out, error_out_size, "failed to allocate decode buffers");
        goto cleanup;
    }

    int got_frame = 0;
    while (!got_frame) {
        rc = av_read_frame(format_ctx, packet);
        if (rc < 0) {
            break;
        }

        if (packet->stream_index != stream_index) {
            av_packet_unref(packet);
            process_yield();
            continue;
        }

        rc = avcodec_send_packet(codec_ctx, packet);
        av_packet_unref(packet);
        if (rc < 0 && rc != AVERROR(EAGAIN)) {
            break;
        }

        while (1) {
            rc = avcodec_receive_frame(codec_ctx, frame);
            if (rc == 0) {
                got_frame = 1;
                break;
            }
            if (rc == AVERROR(EAGAIN) || rc == AVERROR_EOF) {
                break;
            }
            break;
        }

        if (!got_frame) {
            process_yield();
        }
    }

    if (!got_frame) {
        ffmpeg_set_error(error_out, error_out_size, "unable to decode a video frame");
        goto cleanup;
    }

    uint32_t dst_w = 0u;
    uint32_t dst_h = 0u;
    if (ffmpeg_compute_thumbnail_size((uint32_t)frame->width,
                                      (uint32_t)frame->height,
                                      max_width, max_height,
                                      &dst_w, &dst_h) < 0) {
        ffmpeg_set_error(error_out, error_out_size, "invalid frame dimensions");
        goto cleanup;
    }

    sws = sws_getContext(frame->width, frame->height, (enum AVPixelFormat)frame->format,
                         (int)dst_w, (int)dst_h, AV_PIX_FMT_RGBA,
                         SWS_BILINEAR, NULL, NULL, NULL);
    if (!sws) {
        ffmpeg_set_error(error_out, error_out_size, "failed to create scaler");
        goto cleanup;
    }

    size_t stride = (size_t)dst_w * 4u;
    size_t rgba_size = stride * (size_t)dst_h;
    if (stride == 0u || rgba_size / stride != (size_t)dst_h) {
        ffmpeg_set_error(error_out, error_out_size, "thumbnail size overflow");
        goto cleanup;
    }

    rgba = (uint8_t *)malloc(rgba_size);
    if (!rgba) {
        ffmpeg_set_error(error_out, error_out_size, "failed to allocate RGBA buffer");
        goto cleanup;
    }

    uint8_t *dst_data[4] = { rgba, NULL, NULL, NULL };
    int dst_linesize[4] = { (int)stride, 0, 0, 0 };
    rc = sws_scale(sws, (const uint8_t *const *)frame->data, frame->linesize,
                   0, frame->height, dst_data, dst_linesize);
    if (rc <= 0) {
        ffmpeg_set_error(error_out, error_out_size, "failed to scale thumbnail");
        goto cleanup;
    }

    ffmpeg_free_image(out_image);
    out_image->width = dst_w;
    out_image->height = dst_h;
    out_image->stride = (uint32_t)stride;
    out_image->rgba = rgba;
    rgba = NULL;
    rc = 0;

cleanup:
    if (rgba) {
        free(rgba);
    }
    if (sws) {
        sws_freeContext(sws);
    }
    if (frame) {
        av_frame_free(&frame);
    }
    if (packet) {
        av_packet_free(&packet);
    }
    if (codec_ctx) {
        avcodec_free_context(&codec_ctx);
    }
    if (format_ctx) {
        if (format_opened) {
            avformat_close_input(&format_ctx);
        } else {
            avformat_free_context(format_ctx);
            format_ctx = NULL;
        }
    }
    if (avio) {
        avio_context_free(&avio);
    }
    if (avio_buffer) {
        av_free(avio_buffer);
    }
    if (io.fd >= 0) {
        file_close(io.fd);
    }

    if (rc < 0) {
        ffmpeg_free_image(out_image);
    }
    return rc;
}
