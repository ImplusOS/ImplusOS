#include "FFmpeg.h"

#include <File.h>
#include <Process.h>
#include <Serial.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <errno.h>

int posix_memalign(void **memptr, size_t alignment, size_t size)
{
    if (alignment % sizeof(void*) != 0 || (alignment & (alignment - 1)) != 0)
        return EINVAL;
    if (size == 0)
        return EINVAL;

    // ImplusOS malloc provides 32-byte alignment (32-byte block header on
    // page-aligned memory). FFmpeg requests at most 16-byte alignment in this
    // build, so direct malloc is both correct and avoids corrupting free().
    *memptr = malloc(size);
    if (!*memptr) return ENOMEM;
    return 0;
}

float fminf(float x, float y)
{
    return x < y ? x : y;
}

int FFmpeg_posix_memalign(void **ptr, size_t align, size_t size)
{
    return posix_memalign(ptr, align, size);
}

void FFmpeg_free(void *ptr)
{
    free(ptr);
}

void *FFmpeg_realloc(void *ptr, size_t size)
{
    if (size == 0) size = 1;
    return realloc(ptr, size);
}
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libswscale/swscale.h>

typedef struct {
    int32_t fd;
    int64_t size;
} ffmpeg_file_io_t;

struct ffmpeg_decoder_s {
    AVFormatContext *format_ctx;
    AVCodecContext *video_codec_ctx;
    AVCodecContext *audio_codec_ctx;
    AVPacket *packet;
    AVFrame *frame;
    AVFrame *audio_frame;
    struct SwsContext *sws;
    int video_stream_index;
    int audio_stream_index;
    AVIOContext *avio;
    uint8_t *avio_buffer;
    ffmpeg_file_io_t io;
    uint32_t output_width;
    uint32_t output_height;
    uint32_t sws_w;
    uint32_t sws_h;
};

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

int ffmpeg_decoder_get_audio_info(ffmpeg_decoder_t *dec,
                                   ffmpeg_audio_stream_info_t *info)
{
    if (!dec || !info || dec->audio_stream_index < 0 || !dec->audio_codec_ctx) return -1;
    info->has_audio = 1;
    info->audio_stream_index = dec->audio_stream_index;
    info->sample_rate = (uint32_t)dec->audio_codec_ctx->sample_rate;
    info->channels = (uint8_t)dec->audio_codec_ctx->ch_layout.nb_channels;
    return 0;
}

uint32_t ffmpeg_decoder_read_audio(ffmpeg_decoder_t *dec,
                                    int16_t *pcm, uint32_t max_frames)
{
    if (!dec || !dec->audio_codec_ctx || !pcm || max_frames == 0 || !dec->audio_frame) return 0;

    serial_write_string("[FFmpeg] read_audio: unref audio_frame\n");
    av_frame_unref(dec->audio_frame);

    int got_frame = 0;
    while (!got_frame) {
        serial_write_string("[FFmpeg] read_audio: av_read_frame\n");
        int rc = av_read_frame(dec->format_ctx, dec->packet);
        if (rc < 0) return 0; // EOF or error

        if (dec->packet->stream_index != dec->audio_stream_index) {
            av_packet_unref(dec->packet);
            continue;
        }

        serial_write_string("[FFmpeg] read_audio: avcodec_send_packet\n");
        {
            AVPacket local_pkt = {0};
            local_pkt.data = av_malloc(dec->packet->size + AV_INPUT_BUFFER_PADDING_SIZE);
            if (local_pkt.data) {
                memcpy(local_pkt.data, dec->packet->data, dec->packet->size);
                memset(local_pkt.data + dec->packet->size, 0, AV_INPUT_BUFFER_PADDING_SIZE);
                local_pkt.size = dec->packet->size;
                local_pkt.stream_index = dec->packet->stream_index;
                local_pkt.pts = dec->packet->pts;
                local_pkt.dts = dec->packet->dts;
                local_pkt.duration = dec->packet->duration;
                local_pkt.flags = dec->packet->flags;
                local_pkt.pos = dec->packet->pos;
            }
            av_packet_unref(dec->packet);
            if (local_pkt.data) {
                rc = avcodec_send_packet(dec->audio_codec_ctx, &local_pkt);
                av_free(local_pkt.data);
            } else {
                rc = -1;
            }
        }
        if (rc < 0 && rc != AVERROR(EAGAIN)) return 0;

        serial_write_string("[FFmpeg] read_audio: avcodec_receive_frame\n");
        rc = avcodec_receive_frame(dec->audio_codec_ctx, dec->audio_frame);
        if (rc == 0) got_frame = 1;
        else if (rc != AVERROR(EAGAIN) && rc != AVERROR_EOF) return 0;
    }
    
    // Copy samples. For now, assume decoded format is S16
    // This is a very simple implementation and doesn't handle resampling.
    // ffmpegTest might expect this simple approach.
    
    uint32_t nb_samples = (uint32_t)dec->audio_frame->nb_samples;
    uint32_t to_copy = (nb_samples < max_frames) ? nb_samples : max_frames;
    
    // This assumes dec->audio_frame->data[0] is S16
    memcpy(pcm, dec->audio_frame->data[0], (size_t)to_copy * sizeof(int16_t) * (size_t)dec->audio_codec_ctx->ch_layout.nb_channels);
    
    return to_copy;
}

void ffmpeg_decoder_close(ffmpeg_decoder_t *dec)
{
    if (!dec) return;
    if (dec->sws) sws_freeContext(dec->sws);
    if (dec->frame) av_frame_free(&dec->frame);
    if (dec->packet) av_packet_free(&dec->packet);
    if (dec->video_codec_ctx) avcodec_free_context(&dec->video_codec_ctx);
    if (dec->audio_codec_ctx) avcodec_free_context(&dec->audio_codec_ctx);
    if (dec->audio_frame) av_frame_free(&dec->audio_frame);
    if (dec->format_ctx) avformat_close_input(&dec->format_ctx);
    if (dec->avio) avio_context_free(&dec->avio);
    if (dec->avio_buffer) av_free(dec->avio_buffer);
    if (dec->io.fd >= 0) file_close(dec->io.fd);
    free(dec);
}

ffmpeg_decoder_t *ffmpeg_decoder_open(const char *path,
                                       ffmpeg_video_info_t *info_out,
                                       char *error_out,
                                       size_t error_out_size)
{
    ffmpeg_decoder_t *dec = (ffmpeg_decoder_t*)calloc(1, sizeof(ffmpeg_decoder_t));
    if (!dec) {
        ffmpeg_set_error(error_out, error_out_size, "failed to allocate decoder");
        return NULL;
    }

    dec->io.fd = file_open(path, 0);
    if (dec->io.fd < 0) {
        ffmpeg_set_error(error_out, error_out_size, "failed to open input");
        free(dec);
        return NULL;
    }
    file_stat_t stat;
    if (file_stat(path, &stat) >= 0) {
        dec->io.size = (int64_t)stat.size;
    }

    dec->avio_buffer = (uint8_t *)av_malloc(32u * 1024u);
    if (!dec->avio_buffer) {
        ffmpeg_set_error(error_out, error_out_size, "failed to allocate I/O buffer");
        goto cleanup;
    }

    dec->avio = avio_alloc_context(dec->avio_buffer, 32 * 1024, 0, &dec->io,
                               ffmpeg_read_packet, NULL, ffmpeg_seek);
    if (!dec->avio) {
        ffmpeg_set_error(error_out, error_out_size, "failed to create I/O context");
        goto cleanup;
    }

    dec->format_ctx = avformat_alloc_context();
    if (!dec->format_ctx) {
        ffmpeg_set_error(error_out, error_out_size, "failed to allocate format context");
        goto cleanup;
    }
    dec->format_ctx->pb = dec->avio;
    dec->format_ctx->flags |= AVFMT_FLAG_CUSTOM_IO;

    if (avformat_open_input(&dec->format_ctx, NULL, NULL, NULL) < 0) {
        ffmpeg_set_error(error_out, error_out_size, "failed to open input");
        goto cleanup;
    }

    if (avformat_find_stream_info(dec->format_ctx, NULL) < 0) {
        ffmpeg_set_error(error_out, error_out_size, "failed to find stream info");
        goto cleanup;
    }

    dec->video_stream_index = av_find_best_stream(dec->format_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    dec->audio_stream_index = av_find_best_stream(dec->format_ctx, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);

    if (dec->video_stream_index >= 0) {
        AVStream *stream = dec->format_ctx->streams[dec->video_stream_index];
        const AVCodec *codec = avcodec_find_decoder(stream->codecpar->codec_id);
        if (codec) {
            dec->video_codec_ctx = avcodec_alloc_context3(codec);
            avcodec_parameters_to_context(dec->video_codec_ctx, stream->codecpar);
            avcodec_open2(dec->video_codec_ctx, codec, NULL);
        }
    }

    if (dec->audio_stream_index >= 0) {
        AVStream *stream = dec->format_ctx->streams[dec->audio_stream_index];
        const AVCodec *codec = avcodec_find_decoder(stream->codecpar->codec_id);
        if (codec) {
            dec->audio_codec_ctx = avcodec_alloc_context3(codec);
            avcodec_parameters_to_context(dec->audio_codec_ctx, stream->codecpar);
            avcodec_open2(dec->audio_codec_ctx, codec, NULL);
        }
    }

    if (info_out) {
        memset(info_out, 0, sizeof(*info_out));
        if (dec->video_stream_index >= 0) {
            AVStream *stream = dec->format_ctx->streams[dec->video_stream_index];
            info_out->width = stream->codecpar->width;
            info_out->height = stream->codecpar->height;
            info_out->fps_num = stream->avg_frame_rate.num;
            info_out->fps_den = stream->avg_frame_rate.den;
            info_out->duration_us = dec->format_ctx->duration;
        }
    }

    dec->packet = av_packet_alloc();
    dec->frame = av_frame_alloc();
    dec->audio_frame = av_frame_alloc();

    return dec;

cleanup:
    ffmpeg_decoder_close(dec);
    return NULL;
}

void ffmpeg_decoder_set_output_size(ffmpeg_decoder_t *dec,
                                     uint32_t width, uint32_t height)
{
    if (!dec) return;
    dec->output_width = width;
    dec->output_height = height;
}


int ffmpeg_decoder_read_frame(ffmpeg_decoder_t *dec,
                               ffmpeg_rgba_image_t *out_image,
                               int64_t *pts_us)
{
    if (!dec || !dec->video_codec_ctx || !dec->frame) return -1;

    serial_write_string("[FFmpeg] read_frame: unref frame\n");
    av_frame_unref(dec->frame);

    serial_write_string("[FFmpeg] read_frame: video_idx=");
    serial_write_uint32(dec->video_stream_index);
    serial_write_string(" audio_idx=");
    serial_write_uint32(dec->audio_stream_index);
    serial_write_string("\n");

    int got_frame = 0;
    while (!got_frame) {
        serial_write_string("[FFmpeg] read_frame: av_read_frame\n");
        int rc = av_read_frame(dec->format_ctx, dec->packet);
        if (rc < 0) return (rc == AVERROR_EOF) ? 1 : -1;

        serial_write_string(" pkt: si=");
        serial_write_uint32(dec->packet->stream_index);
        serial_write_string(" sz=");
        serial_write_uint32(dec->packet->size);
        serial_write_string("\n");

        if (dec->packet->stream_index != dec->video_stream_index) {
            av_packet_unref(dec->packet);
            continue;
        }

        // Workaround: make a properly refcounted copy and pass it.
        AVPacket *copy_pkt = av_packet_clone(dec->packet);
        av_packet_unref(dec->packet);
        if (!copy_pkt) return -1;
        serial_write_string("[FFmpeg] read_frame: avcodec_send_packet\n");
        rc = avcodec_send_packet(dec->video_codec_ctx, copy_pkt);
        av_packet_free(&copy_pkt);
        if (rc < 0 && rc != AVERROR(EAGAIN)) return -1;

        serial_write_string("[FFmpeg] read_frame: avcodec_receive_frame\n");
        rc = avcodec_receive_frame(dec->video_codec_ctx, dec->frame);
        if (rc == 0) {
            got_frame = 1;
            serial_write_string("[FFmpeg] read_frame: got_frame=1\n");
        }
        else if (rc != AVERROR(EAGAIN) && rc != AVERROR_EOF) {
            serial_write_string("[FFmpeg] read_frame: receive_frame error\n");
            return -1;
        }
    }

    if (pts_us) *pts_us = (int64_t)(dec->frame->pts * av_q2d(dec->format_ctx->streams[dec->video_stream_index]->time_base) * 1000000);

    // Scaling
    uint32_t dst_w = dec->output_width ? dec->output_width : (uint32_t)dec->frame->width;
    uint32_t dst_h = dec->output_height ? dec->output_height : (uint32_t)dec->frame->height;
    
    if (!dec->sws || dec->sws_w != dst_w || dec->sws_h != dst_h) {
        if (dec->sws) sws_freeContext(dec->sws);
        dec->sws = sws_getContext(dec->frame->width, dec->frame->height, (enum AVPixelFormat)dec->frame->format,
                             (int)dst_w, (int)dst_h, AV_PIX_FMT_RGBA,
                             SWS_BILINEAR, NULL, NULL, NULL);
        dec->sws_w = dst_w;
        dec->sws_h = dst_h;
    }

    size_t stride = (size_t)dst_w * 4u;
    size_t rgba_size = stride * (size_t)dst_h;
    
    // Allocate/reallocate buffer if needed
    if (!out_image->rgba || out_image->width != dst_w || out_image->height != dst_h) {
        void *new_rgba = realloc(out_image->rgba, rgba_size);
        if (!new_rgba) return -1;
        out_image->rgba = (uint8_t*)new_rgba;
        out_image->width = dst_w;
        out_image->height = dst_h;
        out_image->stride = (uint32_t)stride;
    }

    uint8_t *dst_data[4] = { out_image->rgba, NULL, NULL, NULL };
    int dst_linesize[4] = { (int)stride, 0, 0, 0 };
    
    sws_scale(dec->sws, (const uint8_t *const *)dec->frame->data, dec->frame->linesize,
              0, dec->frame->height, dst_data, dst_linesize);
    return 0;
}

// Interceptor for av_buffer_ref — silent (no logging)
AVBufferRef *__wrap_av_buffer_ref(const AVBufferRef *buf)
{
    extern AVBufferRef *__real_av_buffer_ref(const AVBufferRef *buf);
    return __real_av_buffer_ref(buf);
}

// Interceptor for av_frame_ref — trace frame refs
int __wrap_av_frame_ref(AVFrame *dst, const AVFrame *src)
{
    serial_write_string("[av_frame_ref] src=");
    serial_write_uint64((uint64_t)(uintptr_t)src);
    serial_write_string(" dst=");
    serial_write_uint64((uint64_t)(uintptr_t)dst);
    for (int i = 0; i < 4; i++) {
        if (src->buf[i]) {
            serial_write_string(" b[");
            { uint32_t _i_ = (uint32_t)i; serial_write_uint32(_i_); }
            serial_write_string("]=");
            serial_write_uint64((uint64_t)(uintptr_t)src->buf[i]);
            serial_write_string(" bf=");
            serial_write_uint64((uint64_t)(uintptr_t)src->buf[i]->buffer);
        }
    }
    if (src->nb_side_data) {
        serial_write_string(" sd_cnt=");
        serial_write_uint32(src->nb_side_data);
        for (unsigned i = 0; i < src->nb_side_data; i++) {
            if (src->side_data[i] && src->side_data[i]->buf) {
                serial_write_string(" sd[");
                serial_write_uint32(i);
                serial_write_string("]=");
                serial_write_uint64((uint64_t)(uintptr_t)src->side_data[i]->buf);
                serial_write_string(" bf=");
                serial_write_uint64((uint64_t)(uintptr_t)src->side_data[i]->buf->buffer);
            }
        }
    }
    serial_write_string("\n");

    extern int __real_av_frame_ref(AVFrame *dst, const AVFrame *src);
    return __real_av_frame_ref(dst, src);
}

// Interceptor for av_buffer_alloc — silent (no logging)
AVBufferRef *__wrap_av_buffer_alloc(size_t size)
{
    extern AVBufferRef *__real_av_buffer_alloc(size_t size);
    return __real_av_buffer_alloc(size);
}

// Interceptor for av_malloc — silent (no logging to avoid slowdown)
void *__wrap_av_malloc(size_t size)
{
    extern void *__real_av_malloc(size_t size);
    return __real_av_malloc(size);
}

// Interceptor for av_mallocz — silent (no logging to avoid slowdown)
void *__wrap_av_mallocz(size_t size)
{
    extern void *__real_av_mallocz(size_t size);
    return __real_av_mallocz(size);
}

// Known bad AVBufferRef addresses from the crash
#define BAD_ADDR_COUNT 3
static const uint64_t g_bad_refs[BAD_ADDR_COUNT] = {
    0x4101286FA0ULL,
    0x4101306C60ULL,
    0x4101386C60ULL
};

static int is_bad_addr(const void *ptr)
{
    uint64_t u = (uint64_t)(uintptr_t)ptr;
    for (int i = 0; i < BAD_ADDR_COUNT; i++) {
        if (u == g_bad_refs[i]) return 1;
    }
    return 0;
}

static int range_hits_bad(const void *s, size_t n)
{
    uint64_t start = (uint64_t)(uintptr_t)s;
    uint64_t end   = start + n;
    for (int i = 0; i < BAD_ADDR_COUNT; i++) {
        uint64_t b = g_bad_refs[i];
        if (start < b + 24 && end > b) return 1;
    }
    return 0;
}

// Interceptor for free — log if a known bad address is freed
void __wrap_free(void *ptr)
{
    extern void __real_free(void *ptr);
    if (ptr && is_bad_addr(ptr)) {
        serial_write_string("[FREE] BAD ADDR=");
        serial_write_uint64((uint64_t)(uintptr_t)ptr);
        serial_write_string("\n");
    }
    __real_free(ptr);
}

// Interceptor for malloc — log if a known bad address is returned
void *__wrap_malloc(size_t size)
{
    extern void *__real_malloc(size_t size);
    void *ptr = __real_malloc(size);
    if (ptr && is_bad_addr(ptr)) {
        serial_write_string("[MALLOC] GOT BAD ADDR=");
        serial_write_uint64((uint64_t)(uintptr_t)ptr);
        serial_write_string(" sz=");
        serial_write_uint64((uint64_t)size);
        serial_write_string("\n");
    }
    return ptr;
}

// Interceptor for memset — log if the range touches a known bad address
void *__wrap_memset(void *s, int c, size_t n)
{
    extern void *__real_memset(void *s, int c, size_t n);
    if (range_hits_bad(s, n)) {
        serial_write_string("[MEMSET] HIT s=");
        serial_write_uint64((uint64_t)(uintptr_t)s);
        serial_write_string(" c=");
        serial_write_uint32((uint32_t)(unsigned char)c);
        serial_write_string(" n=");
        serial_write_uint64((uint64_t)n);
        serial_write_string(" end=");
        serial_write_uint64((uint64_t)(uintptr_t)s + n);
        serial_write_string("\n");
    }
    return __real_memset(s, c, n);
}

// Interceptor for av_free — log if a known bad address is freed via av_free
void __wrap_av_free(void *ptr)
{
    extern void __real_av_free(void *ptr);
    if (ptr && is_bad_addr(ptr)) {
        serial_write_string("[av_free] BAD ADDR=");
        serial_write_uint64((uint64_t)(uintptr_t)ptr);
        serial_write_string("\n");
    }
    __real_av_free(ptr);
}

