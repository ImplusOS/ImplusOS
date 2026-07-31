/*
 * Userland/API/FFmpeg.c
 *
 * ImplusOS Userland FFmpeg API
 */

#include "FFmpeg.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "File.h"
#include "Serial.h"

#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include <libavutil/samplefmt.h>
#include <libswscale/swscale.h>

#ifndef IMPLUSOS_FFMPEG_BUILD
#  error "FFmpeg.c must be compiled with -DIMPLUSOS_FFMPEG_BUILD"
#endif

/* -----------------------------------------------------------------------
 * Memory shims
 * --------------------------------------------------------------------- */

void *FFmpeg_malloc(size_t size)   { return malloc(size); }
void *FFmpeg_realloc(void *ptr, size_t size) { return realloc(ptr, size); }
void  FFmpeg_free(void *ptr)       { free(ptr); }

/* -----------------------------------------------------------------------
 * Math shim
 * --------------------------------------------------------------------- */

#ifndef IMPLUSOS_FMINF_DEFINED
#define IMPLUSOS_FMINF_DEFINED
float fminf(float x, float y) { return (x < y) ? x : y; }
#endif

/* -----------------------------------------------------------------------
 * Internal I/O context
 * --------------------------------------------------------------------- */

#define AVIO_BUFFER_SIZE (64u * 1024u)

typedef struct {
    int32_t  fd;
    int64_t  file_size;
} ffmpeg_io_ctx_t;

static int avio_read_packet(void *opaque, uint8_t *buf, int buf_size)
{
    ffmpeg_io_ctx_t *io = (ffmpeg_io_ctx_t *)opaque;
    if (buf_size <= 0) return 0;
    int64_t n = file_read(io->fd, buf, (uint64_t)buf_size);
    if (n == 0) return AVERROR_EOF;
    if (n < 0)  return AVERROR(5); /* EIO */
    return (int)n;
}

static int64_t avio_seek_packet(void *opaque, int64_t offset, int whence)
{
    ffmpeg_io_ctx_t *io = (ffmpeg_io_ctx_t *)opaque;

    /* FFmpeg specific seek size request */
    if (whence == AVSEEK_SIZE) {
        if (io->file_size > 0) return io->file_size;
        int64_t end = file_seek(io->fd, 0, 2);
        if (end < 0) return AVERROR(5);
        int64_t cur = file_seek(io->fd, 0, 1);
        if (cur < 0) return AVERROR(5);
        io->file_size = end;
        file_seek(io->fd, cur, 0);
        return end;
    }

    /* FFmpeg may pass AVSEEK_FORCE (0x20000) bitwise-ORed with whence */
    int whence_dir = whence & ~AVSEEK_FORCE;

    int posix_whence;
    if      (whence_dir == SEEK_SET) posix_whence = 0;
    else if (whence_dir == SEEK_CUR) posix_whence = 1;
    else if (whence_dir == SEEK_END) posix_whence = 2;
    else return AVERROR(22); /* EINVAL */

    int64_t result = file_seek(io->fd, offset, posix_whence);
    if (result < 0) return AVERROR(5);
    return result;
}

/* -----------------------------------------------------------------------
 * Internal decoder structure
 * --------------------------------------------------------------------- */

struct ffmpeg_decoder {
    ffmpeg_io_ctx_t   io_ctx;
    uint8_t          *avio_buf;
    AVIOContext      *avio;

    AVFormatContext  *fmt_ctx;
    AVCodecContext   *codec_ctx;
    int               video_stream_idx;

    AVPacket         *pkt;
    AVFrame          *frame;

    struct SwsContext *sws;
    uint32_t          sws_src_w;
    uint32_t          sws_src_h;

    uint32_t          out_w;
    uint32_t          out_h;
    uint32_t          coded_w;
    uint32_t          coded_h;

    /* Audio */
    int               audio_stream_idx;
    AVCodecContext   *audio_codec_ctx;
    AVFrame          *audio_frame;
    uint8_t          *audio_pcm_buf;
    int               audio_pcm_used;
    int               audio_pcm_capacity;
    int               audio_sample_rate;
    int               audio_target_rate;
    int               audio_channels;
    int64_t           audio_duration_us;
};

/* -----------------------------------------------------------------------
 * Helper: error buffer
 * --------------------------------------------------------------------- */

static void set_error(char *err_buf, size_t err_len, const char *msg)
{
    if (err_buf && err_len > 0u) {
        size_t n = strlen(msg);
        if (n >= err_len) n = err_len - 1u;
        memcpy(err_buf, msg, n);
        err_buf[n] = '\0';
    }
}

static void set_averror(char *err_buf, size_t err_len,
                        const char *prefix, int averr)
{
    if (!err_buf || err_len == 0u) return;
    char av_msg[128];
    av_strerror(averr, av_msg, sizeof(av_msg));
    snprintf(err_buf, err_len, "%s: %s", prefix, av_msg);
}

/* -----------------------------------------------------------------------
 * ffmpeg_decoder_open
 * --------------------------------------------------------------------- */

ffmpeg_decoder_t *ffmpeg_decoder_open(const char         *path,
                                       ffmpeg_video_info_t *out_info,
                                       char               *err_buf,
                                       size_t              err_len)
{
    if (!path) {
        set_error(err_buf, err_len, "ffmpeg_decoder_open: NULL path");
        return NULL;
    }

    ffmpeg_decoder_t *dec = (ffmpeg_decoder_t *)malloc(sizeof(*dec));
    if (!dec) {
        set_error(err_buf, err_len, "ffmpeg_decoder_open: out of memory");
        return NULL;
    }
    memset(dec, 0, sizeof(*dec));
    dec->audio_stream_idx = -1;

    dec->io_ctx.fd = file_open(path, 0u);
    if (dec->io_ctx.fd < 0) {
        set_error(err_buf, err_len, "ffmpeg_decoder_open: cannot open file");
        free(dec);
        return NULL;
    }
    dec->io_ctx.file_size = 0;

    dec->avio_buf = (uint8_t *)av_malloc(AVIO_BUFFER_SIZE);
    if (!dec->avio_buf) {
        set_error(err_buf, err_len, "ffmpeg_decoder_open: av_malloc avio_buf");
        goto fail_fd;
    }

    dec->avio = avio_alloc_context(
        dec->avio_buf,
        (int)AVIO_BUFFER_SIZE,
        0,
        &dec->io_ctx,
        avio_read_packet,
        NULL,
        avio_seek_packet);

    if (!dec->avio) {
        set_error(err_buf, err_len, "ffmpeg_decoder_open: avio_alloc_context");
        av_free(dec->avio_buf);
        dec->avio_buf = NULL;
        goto fail_fd;
    }

    dec->fmt_ctx = avformat_alloc_context();
    if (!dec->fmt_ctx) {
        set_error(err_buf, err_len, "ffmpeg_decoder_open: avformat_alloc_context");
        goto fail_avio;
    }
    dec->fmt_ctx->pb = dec->avio;
    dec->fmt_ctx->flags |= AVFMT_FLAG_CUSTOM_IO | AVFMT_FLAG_DISCARD_CORRUPT;

    int ret = avformat_open_input(&dec->fmt_ctx, NULL, NULL, NULL);
    if (ret < 0) {
        set_averror(err_buf, err_len, "avformat_open_input", ret);
        dec->fmt_ctx = NULL;
        goto fail_avio;
    }

    ret = avformat_find_stream_info(dec->fmt_ctx, NULL);
    if (ret < 0) {
        set_averror(err_buf, err_len, "avformat_find_stream_info", ret);
        goto fail_fmt;
    }

    ret = av_find_best_stream(dec->fmt_ctx, AVMEDIA_TYPE_VIDEO,
                              -1, -1, NULL, 0);
    if (ret < 0) {
        set_error(err_buf, err_len, "ffmpeg_decoder_open: no video stream");
        goto fail_fmt;
    }
    dec->video_stream_idx = ret;

    dec->audio_stream_idx = -1;
    for (unsigned i = 0u; i < dec->fmt_ctx->nb_streams; i++) {
        if ((int)i == dec->video_stream_idx) continue;
        if (dec->fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO &&
            dec->audio_stream_idx < 0) {
            dec->audio_stream_idx = (int)i;
        } else {
            dec->fmt_ctx->streams[i]->discard = AVDISCARD_ALL;
        }
    }

    AVStream *vstream = dec->fmt_ctx->streams[dec->video_stream_idx];

    const AVCodec *codec = avcodec_find_decoder(
        vstream->codecpar->codec_id);
    if (!codec) {
        set_error(err_buf, err_len,
                  "ffmpeg_decoder_open: no decoder for codec");
        goto fail_fmt;
    }

    dec->codec_ctx = avcodec_alloc_context3(codec);
    if (!dec->codec_ctx) {
        set_error(err_buf, err_len, "ffmpeg_decoder_open: avcodec_alloc_context3");
        goto fail_fmt;
    }

    ret = avcodec_parameters_to_context(dec->codec_ctx, vstream->codecpar);
    if (ret < 0) {
        set_averror(err_buf, err_len, "avcodec_parameters_to_context", ret);
        goto fail_codec;
    }

    dec->codec_ctx->thread_count = 1;

    ret = avcodec_open2(dec->codec_ctx, codec, NULL);
    if (ret < 0) {
        set_averror(err_buf, err_len, "avcodec_open2", ret);
        goto fail_codec;
    }

    dec->pkt = av_packet_alloc();
    if (!dec->pkt) {
        set_error(err_buf, err_len, "ffmpeg_decoder_open: av_packet_alloc");
        goto fail_codec;
    }

    dec->frame = av_frame_alloc();
    if (!dec->frame) {
        set_error(err_buf, err_len, "ffmpeg_decoder_open: av_frame_alloc");
        goto fail_pkt;
    }

    dec->coded_w = (uint32_t)dec->codec_ctx->width;
    dec->coded_h = (uint32_t)dec->codec_ctx->height;
    dec->out_w   = dec->coded_w;
    dec->out_h   = dec->coded_h;

    if (dec->audio_stream_idx >= 0) {
        AVStream *astream = dec->fmt_ctx->streams[dec->audio_stream_idx];
        const AVCodec *acodec = avcodec_find_decoder(
            astream->codecpar->codec_id);
        if (acodec) {
            dec->audio_codec_ctx = avcodec_alloc_context3(acodec);
            if (dec->audio_codec_ctx) {
                avcodec_parameters_to_context(dec->audio_codec_ctx,
                                              astream->codecpar);
                dec->audio_codec_ctx->thread_count = 1;
                if (avcodec_open2(dec->audio_codec_ctx, acodec, NULL) >= 0) {
                    dec->audio_frame = av_frame_alloc();
                    dec->audio_sample_rate =
                        dec->audio_codec_ctx->sample_rate;
                    dec->audio_channels =
                        dec->audio_codec_ctx->ch_layout.nb_channels;
                    if (astream->duration != AV_NOPTS_VALUE) {
                        dec->audio_duration_us =
                            av_rescale_q(astream->duration,
                                         astream->time_base,
                                         (AVRational){1, 1000000});
                    } else if (dec->fmt_ctx->duration != AV_NOPTS_VALUE) {
                        dec->audio_duration_us =
                            (int64_t)dec->fmt_ctx->duration;
                    }
                } else {
                    avcodec_free_context(&dec->audio_codec_ctx);
                    dec->audio_stream_idx = -1;
                }
            }
        } else {
            dec->audio_stream_idx = -1;
        }
    }

    if (out_info) {
        memset(out_info, 0, sizeof(*out_info));
        out_info->width  = dec->coded_w;
        out_info->height = dec->coded_h;

        if (vstream->avg_frame_rate.den != 0) {
            out_info->fps_num = (uint32_t)vstream->avg_frame_rate.num;
            out_info->fps_den = (uint32_t)vstream->avg_frame_rate.den;
        } else if (vstream->r_frame_rate.den != 0) {
            out_info->fps_num = (uint32_t)vstream->r_frame_rate.num;
            out_info->fps_den = (uint32_t)vstream->r_frame_rate.den;
        }

        if (dec->fmt_ctx->duration != AV_NOPTS_VALUE) {
            out_info->duration_us = (int64_t)dec->fmt_ctx->duration;
        } else if (vstream->duration != AV_NOPTS_VALUE &&
                   vstream->time_base.den != 0) {
            out_info->duration_us =
                av_rescale_q(vstream->duration,
                             vstream->time_base,
                             (AVRational){1, 1000000});
        }
    }

    return dec;

fail_pkt:
    av_packet_free(&dec->pkt);
fail_codec:
    avcodec_free_context(&dec->codec_ctx);
fail_fmt:
    if (dec->fmt_ctx) avformat_close_input(&dec->fmt_ctx);
fail_avio:
    if (dec->avio) {
        dec->avio->buffer = NULL;
        avio_context_free(&dec->avio);
    }
    if (dec->avio_buf) av_free(dec->avio_buf);
fail_fd:
    file_close(dec->io_ctx.fd);
    free(dec);
    return NULL;
}

/* -----------------------------------------------------------------------
 * ffmpeg_decoder_set_output_size
 * --------------------------------------------------------------------- */

void ffmpeg_decoder_set_output_size(ffmpeg_decoder_t *dec,
                                     uint32_t          width,
                                     uint32_t          height)
{
    if (!dec || width == 0u || height == 0u) return;
    if (dec->out_w == width && dec->out_h == height) return;

    dec->out_w = width;
    dec->out_h = height;

    if (dec->sws) {
        sws_freeContext(dec->sws);
        dec->sws = NULL;
    }
}

/* -----------------------------------------------------------------------
 * Internal: (re)build SwsContext
 * --------------------------------------------------------------------- */

static int ensure_sws(ffmpeg_decoder_t *dec,
                       uint32_t src_w, uint32_t src_h,
                       enum AVPixelFormat src_pix_fmt)
{
    if (dec->sws &&
        dec->sws_src_w == src_w &&
        dec->sws_src_h == src_h) {
        return 0;
    }

    if (dec->sws) {
        sws_freeContext(dec->sws);
        dec->sws = NULL;
    }

    dec->sws = sws_getContext(
        (int)src_w,  (int)src_h,  src_pix_fmt,
        (int)dec->out_w, (int)dec->out_h, AV_PIX_FMT_BGRA,
        SWS_FAST_BILINEAR, NULL, NULL, NULL);

    if (!dec->sws) {
        serial_write_string("[FFmpeg] ensure_sws: sws_getContext failed\n");
        return -1;
    }

    dec->sws_src_w = src_w;
    dec->sws_src_h = src_h;
    return 0;
}

/* -----------------------------------------------------------------------
 * Internal: convert audio frame to S16_LE stereo and append to PCM buffer
 * --------------------------------------------------------------------- */

#define AUDIO_MAX_CH 8

static int audio_convert_and_buffer(ffmpeg_decoder_t *dec, AVFrame *frame)
{
    int fmt       = frame->format;
    int in_ch     = frame->ch_layout.nb_channels;
    int samples   = frame->nb_samples;
    int src_rate  = dec->audio_sample_rate;
    int dst_rate  = dec->audio_target_rate;
    if (samples <= 0 || in_ch <= 0) return 0;
    if (dst_rate <= 0) dst_rate = src_rate;

    /* Convert to S16_LE stereo interleaved at src_rate into a temp buffer */
    int max_src_samples = samples;
    size_t src_buf_size = (size_t)max_src_samples * 2u * sizeof(int16_t);
    int16_t *src_buf = (int16_t *)av_malloc(src_buf_size + 32u);
    if (!src_buf) return -1;

    float   tmp_f[AUDIO_MAX_CH];
    int     tmp_i[AUDIO_MAX_CH];
    int16_t *tmp = src_buf;

    switch (fmt) {
    case AV_SAMPLE_FMT_FLTP: {
        float **src = (float **)frame->extended_data;
        for (int i = 0; i < samples; i++) {
            for (int ch = 0; ch < in_ch && ch < AUDIO_MAX_CH; ch++)
                tmp_f[ch] = src[ch][i];
            float l = (in_ch >= 1) ? tmp_f[0] : 0.0f;
            float r = (in_ch >= 2) ? tmp_f[1] : l;
            if (in_ch > 2) {
                l = 0.0f; r = 0.0f;
                for (int ch = 0; ch < in_ch; ch += 2) l += tmp_f[ch];
                for (int ch = 1; ch < in_ch; ch += 2) r += tmp_f[ch];
                l /= (float)((in_ch + 1) / 2);
                r /= (float)(in_ch / 2);
            }
            if (l < -1.0f) l = -1.0f; if (l > 1.0f) l = 1.0f;
            if (r < -1.0f) r = -1.0f; if (r > 1.0f) r = 1.0f;
            tmp[i * 2]     = (int16_t)(l * 32767.0f);
            tmp[i * 2 + 1] = (int16_t)(r * 32767.0f);
        }
        break;
    }
    case AV_SAMPLE_FMT_S16P: {
        int16_t **src = (int16_t **)frame->extended_data;
        for (int i = 0; i < samples; i++) {
            for (int ch = 0; ch < in_ch && ch < AUDIO_MAX_CH; ch++)
                tmp_i[ch] = (int)src[ch][i];
            int l = (in_ch >= 1) ? tmp_i[0] : 0;
            int r = (in_ch >= 2) ? tmp_i[1] : l;
            if (in_ch > 2) { l = 0; r = 0;
                for (int ch = 0; ch < in_ch; ch += 2) l += tmp_i[ch];
                for (int ch = 1; ch < in_ch; ch += 2) r += tmp_i[ch];
                l /= ((in_ch + 1) / 2); r /= (in_ch / 2);
            }
            if (l < -32768) l = -32768; if (l > 32767) l = 32767;
            if (r < -32768) r = -32768; if (r > 32767) r = 32767;
            tmp[i * 2] = (int16_t)l; tmp[i * 2 + 1] = (int16_t)r;
        }
        break;
    }
    case AV_SAMPLE_FMT_FLT: {
        float *src = (float *)frame->data[0];
        for (int i = 0; i < samples; i++) {
            for (int ch = 0; ch < in_ch && ch < AUDIO_MAX_CH; ch++)
                tmp_f[ch] = src[i * in_ch + ch];
            float l = (in_ch >= 1) ? tmp_f[0] : 0.0f;
            float r = (in_ch >= 2) ? tmp_f[1] : l;
            if (in_ch > 2) {
                l = 0.0f; r = 0.0f;
                for (int ch = 0; ch < in_ch; ch += 2) l += tmp_f[ch];
                for (int ch = 1; ch < in_ch; ch += 2) r += tmp_f[ch];
                l /= (float)((in_ch + 1) / 2); r /= (float)(in_ch / 2);
            }
            if (l < -1.0f) l = -1.0f; if (l > 1.0f) l = 1.0f;
            if (r < -1.0f) r = -1.0f; if (r > 1.0f) r = 1.0f;
            tmp[i * 2] = (int16_t)(l * 32767.0f);
            tmp[i * 2 + 1] = (int16_t)(r * 32767.0f);
        }
        break;
    }
    case AV_SAMPLE_FMT_S16: {
        int16_t *src = (int16_t *)frame->data[0];
        if (in_ch == 2) {
            memcpy(tmp, src, src_buf_size);
        } else {
            for (int i = 0; i < samples; i++) {
                for (int ch = 0; ch < in_ch && ch < AUDIO_MAX_CH; ch++)
                    tmp_i[ch] = (int)src[i * in_ch + ch];
                int l = (in_ch >= 1) ? tmp_i[0] : 0;
                int r = (in_ch >= 2) ? tmp_i[1] : l;
                if (in_ch > 2) { l = 0; r = 0;
                    for (int ch = 0; ch < in_ch; ch += 2) l += tmp_i[ch];
                    for (int ch = 1; ch < in_ch; ch += 2) r += tmp_i[ch];
                    l /= ((in_ch + 1) / 2); r /= (in_ch / 2);
                }
                if (l < -32768) l = -32768; if (l > 32767) l = 32767;
                if (r < -32768) r = -32768; if (r > 32767) r = 32767;
                tmp[i * 2] = (int16_t)l; tmp[i * 2 + 1] = (int16_t)r;
            }
        }
        break;
    }
    case AV_SAMPLE_FMT_U8P: {
        uint8_t **src = (uint8_t **)frame->extended_data;
        for (int i = 0; i < samples; i++) {
            for (int ch = 0; ch < in_ch && ch < AUDIO_MAX_CH; ch++)
                tmp_i[ch] = ((int)src[ch][i] - 128) << 8;
            int l = (in_ch >= 1) ? tmp_i[0] : 0;
            int r = (in_ch >= 2) ? tmp_i[1] : l;
            if (in_ch > 2) { l = 0; r = 0;
                for (int ch = 0; ch < in_ch; ch += 2) l += tmp_i[ch];
                for (int ch = 1; ch < in_ch; ch += 2) r += tmp_i[ch];
                l /= ((in_ch + 1) / 2); r /= (in_ch / 2);
            }
            if (l < -32768) l = -32768; if (l > 32767) l = 32767;
            if (r < -32768) r = -32768; if (r > 32767) r = 32767;
            tmp[i * 2] = (int16_t)l; tmp[i * 2 + 1] = (int16_t)r;
        }
        break;
    }
    case AV_SAMPLE_FMT_U8: {
        uint8_t *src = (uint8_t *)frame->data[0];
        for (int i = 0; i < samples; i++) {
            for (int ch = 0; ch < in_ch && ch < AUDIO_MAX_CH; ch++)
                tmp_i[ch] = ((int)src[i * in_ch + ch] - 128) << 8;
            int l = (in_ch >= 1) ? tmp_i[0] : 0;
            int r = (in_ch >= 2) ? tmp_i[1] : l;
            if (in_ch > 2) { l = 0; r = 0;
                for (int ch = 0; ch < in_ch; ch += 2) l += tmp_i[ch];
                for (int ch = 1; ch < in_ch; ch += 2) r += tmp_i[ch];
                l /= ((in_ch + 1) / 2); r /= (in_ch / 2);
            }
            if (l < -32768) l = -32768; if (l > 32767) l = 32767;
            if (r < -32768) r = -32768; if (r > 32767) r = 32767;
            tmp[i * 2] = (int16_t)l; tmp[i * 2 + 1] = (int16_t)r;
        }
        break;
    }
    default:
        memset(tmp, 0, src_buf_size);
        break;
    }

    /* Resample from src_rate to dst_rate using linear interpolation */
    int16_t *out_buf = tmp;
    int out_samples = samples;

    if (dst_rate != src_rate && src_rate > 0 && dst_rate > 0) {
        out_samples = (int)((int64_t)samples * dst_rate / src_rate);
        if (out_samples < 1) out_samples = 1;
        size_t dst_buf_size = (size_t)out_samples * 2u * sizeof(int16_t);
        int16_t *dst_buf = (int16_t *)av_malloc(dst_buf_size + 32u);
        if (!dst_buf) { av_free(src_buf); return -1; }

        for (int i = 0; i < out_samples; i++) {
            int64_t src_pos = (int64_t)i * src_rate;
            int idx = (int)(src_pos / dst_rate);
            int frac = (int)(src_pos % dst_rate);
            if (idx >= samples - 1) {
                dst_buf[i * 2]     = tmp[(samples - 1) * 2];
                dst_buf[i * 2 + 1] = tmp[(samples - 1) * 2 + 1];
            } else {
                int l0 = tmp[idx * 2];
                int l1 = tmp[(idx + 1) * 2];
                int r0 = tmp[idx * 2 + 1];
                int r1 = tmp[(idx + 1) * 2 + 1];
                int l = l0 + ((l1 - l0) * frac + dst_rate / 2) / dst_rate;
                int r = r0 + ((r1 - r0) * frac + dst_rate / 2) / dst_rate;
                if (l < -32768) l = -32768; if (l > 32767) l = 32767;
                if (r < -32768) r = -32768; if (r > 32767) r = 32767;
                dst_buf[i * 2]     = (int16_t)l;
                dst_buf[i * 2 + 1] = (int16_t)r;
            }
        }
        out_buf = dst_buf;
    }

    /* Append to audio PCM buffer */
    size_t out_size = (size_t)out_samples * 2u * sizeof(int16_t);
    int needed = dec->audio_pcm_used + (int)out_size;
    if (needed > dec->audio_pcm_capacity) {
        int new_cap = needed + 65536;
        uint8_t *nb = (uint8_t *)av_realloc(dec->audio_pcm_buf,
                                            (size_t)new_cap);
        if (!nb) { 
            if (out_buf != src_buf) av_free(out_buf);
            av_free(src_buf);
            return -1; 
        }
        dec->audio_pcm_buf     = nb;
        dec->audio_pcm_capacity = new_cap;
    }
    memcpy(dec->audio_pcm_buf + dec->audio_pcm_used, out_buf, out_size);
    dec->audio_pcm_used += (int)out_size;

    if (out_buf != src_buf) av_free(out_buf);
    av_free(src_buf);
    return 0;
}

/* -----------------------------------------------------------------------
 * ffmpeg_decoder_read_frame
 * --------------------------------------------------------------------- */

int ffmpeg_decoder_read_frame(ffmpeg_decoder_t   *dec,
                               ffmpeg_rgba_image_t *img,
                               int64_t             *pts_us)
{
    if (!dec || !img) return -1;

    if (!dec->codec_ctx || !dec->frame || !dec->pkt || !dec->fmt_ctx) {
        return -1;
    }

    AVStream *vstream = dec->fmt_ctx->streams[dec->video_stream_idx];

    for (;;) {
        for (;;) {
            int ret = avcodec_receive_frame(dec->codec_ctx, dec->frame);

            if (ret == 0) {
                goto got_frame;
            }
            if (ret != AVERROR(EAGAIN)) {
                if (ret == AVERROR_EOF) {
                    return 1;
                }
                return -1; /* AVERROR_INVALIDDATA, etc. */
            }

            ret = av_read_frame(dec->fmt_ctx, dec->pkt);
            if (ret == AVERROR_EOF) {
                avcodec_send_packet(dec->codec_ctx, NULL);
                ret = avcodec_receive_frame(dec->codec_ctx, dec->frame);
                if (ret == 0) goto got_frame;

                if (dec->audio_codec_ctx) {
                    avcodec_send_packet(dec->audio_codec_ctx, NULL);
                    while (avcodec_receive_frame(dec->audio_codec_ctx,
                                                 dec->audio_frame) == 0) {
                        audio_convert_and_buffer(dec, dec->audio_frame);
                        av_frame_unref(dec->audio_frame);
                    }
                }
                return 1;
            }
            if (ret < 0) {
                return -1;
            }

            if (dec->pkt->stream_index == dec->video_stream_idx) {
                ret = avcodec_send_packet(dec->codec_ctx, dec->pkt);
                av_packet_unref(dec->pkt);
                if (ret < 0 && ret != AVERROR(EAGAIN)) return -1;
            } else if (dec->audio_stream_idx >= 0 &&
                       dec->pkt->stream_index == dec->audio_stream_idx) {
                ret = avcodec_send_packet(dec->audio_codec_ctx, dec->pkt);
                av_packet_unref(dec->pkt);
                if (ret < 0 && ret != AVERROR(EAGAIN)) {
                    /* skip corrupt packet */;
                }
                while (1) {
                    ret = avcodec_receive_frame(dec->audio_codec_ctx,
                                                dec->audio_frame);
                    if (ret == AVERROR(EAGAIN)) break;
                    if (ret < 0) break;
                    audio_convert_and_buffer(dec, dec->audio_frame);
                    av_frame_unref(dec->audio_frame);
                }
            } else {
                av_packet_unref(dec->pkt);
            }
        }

got_frame:
        {
            /* PTS 変換 */
            int64_t raw_pts = dec->frame->best_effort_timestamp;
            if (raw_pts == AV_NOPTS_VALUE) raw_pts = dec->frame->pts;
            if (raw_pts == AV_NOPTS_VALUE) raw_pts = 0;

            if (pts_us) {
                *pts_us = av_rescale_q(raw_pts,
                                       vstream->time_base,
                                       (AVRational){1, 1000000});
            }

            uint32_t src_w = (uint32_t)dec->frame->width;
            uint32_t src_h = (uint32_t)dec->frame->height;
            enum AVPixelFormat src_fmt =
                (enum AVPixelFormat)dec->frame->format;

            if (src_w == 0u || src_h == 0u) {
                av_frame_unref(dec->frame);
                continue;
            }

            if (!dec->frame->data[0] || dec->frame->linesize[0] <= 0) {
                av_frame_unref(dec->frame);
                continue;
            }

            if (dec->out_w == 0u) dec->out_w = src_w;
            if (dec->out_h == 0u) dec->out_h = src_h;

            if (ensure_sws(dec, src_w, src_h, src_fmt) < 0) {
                av_frame_unref(dec->frame);
                return -1;
            }

            size_t need_stride = ((size_t)dec->out_w * 4u + 63u) & ~(size_t)63u;

            if (!img->rgba) {
                size_t need_size = need_stride * (size_t)dec->out_h + 4096u;
                img->rgba = (uint8_t *)malloc(need_size);
                if (!img->rgba) {
                    av_frame_unref(dec->frame);
                    return -1;
                }
                img->width  = dec->out_w;
                img->height = dec->out_h;
                img->stride = (uint32_t)need_stride;
            }

            uint8_t *dst_data[4]    = { img->rgba, NULL, NULL, NULL };
            int      dst_linesize[4] = { (int)(uint32_t)img->stride, 0, 0, 0 };

            sws_scale(dec->sws,
                      (const uint8_t * const *)dec->frame->data,
                      dec->frame->linesize,
                      0, (int)src_h,
                      dst_data, dst_linesize);

            img->width  = dec->out_w;
            img->height = dec->out_h;
            img->stride = (uint32_t)need_stride;

            av_frame_unref(dec->frame);
            return 0;
        }
    }
}

/* -----------------------------------------------------------------------
 * ffmpeg_decoder_has_audio
 * --------------------------------------------------------------------- */

int ffmpeg_decoder_has_audio(ffmpeg_decoder_t *dec)
{
    return (dec && dec->audio_stream_idx >= 0) ? 1 : 0;
}

/* -----------------------------------------------------------------------
 * ffmpeg_decoder_get_audio_info
 * --------------------------------------------------------------------- */

int ffmpeg_decoder_get_audio_info(ffmpeg_decoder_t   *dec,
                                   ffmpeg_audio_info_t *out_info)
{
    if (!dec || !out_info) return -1;
    if (dec->audio_stream_idx < 0) return -1;
    out_info->sample_rate = dec->audio_sample_rate;
    out_info->channels    = dec->audio_channels;
    out_info->duration_us = dec->audio_duration_us;
    return 0;
}

/* -----------------------------------------------------------------------
 * ffmpeg_decoder_read_audio
 * --------------------------------------------------------------------- */

int ffmpeg_decoder_read_audio(ffmpeg_decoder_t *dec,
                               uint8_t          *buf,
                               int               buf_size)
{
    if (!dec || !buf || buf_size <= 0) return 0;
    if (!dec->audio_codec_ctx || dec->audio_pcm_used <= 0) return 0;

    int copy = dec->audio_pcm_used;
    if (copy > buf_size) copy = buf_size;

    memcpy(buf, dec->audio_pcm_buf, (size_t)copy);

    int remaining = dec->audio_pcm_used - copy;
    if (remaining > 0) {
        memmove(dec->audio_pcm_buf, dec->audio_pcm_buf + copy,
                (size_t)remaining);
    }
    dec->audio_pcm_used = remaining;
    return copy;
}

/* -----------------------------------------------------------------------
 * ffmpeg_decoder_set_audio_target_rate
 * --------------------------------------------------------------------- */

void ffmpeg_decoder_set_audio_target_rate(ffmpeg_decoder_t *dec,
                                           int               rate)
{
    if (!dec) return;
    dec->audio_target_rate = (rate > 0) ? rate : 0;
}

/* -----------------------------------------------------------------------
 * ffmpeg_decoder_close
 * --------------------------------------------------------------------- */

void ffmpeg_decoder_close(ffmpeg_decoder_t *dec)
{
    if (!dec) return;

    if (dec->sws)          { sws_freeContext(dec->sws);                            }
    if (dec->frame)        { av_frame_free(&dec->frame);                           }
    if (dec->pkt)          { av_packet_free(&dec->pkt);                            }
    if (dec->codec_ctx)    { avcodec_free_context(&dec->codec_ctx);                }
    if (dec->audio_frame)  { av_frame_free(&dec->audio_frame);                    }
    if (dec->audio_codec_ctx) { avcodec_free_context(&dec->audio_codec_ctx);       }
    if (dec->audio_pcm_buf) { av_free(dec->audio_pcm_buf);                        }
    if (dec->fmt_ctx)      { avformat_close_input(&dec->fmt_ctx);                  }

    if (dec->avio) {
        dec->avio->buffer = NULL;
        avio_context_free(&dec->avio);
    }
    if (dec->avio_buf) {
        av_free(dec->avio_buf);
        dec->avio_buf = NULL;
    }

    file_close(dec->io_ctx.fd);
    free(dec);
}