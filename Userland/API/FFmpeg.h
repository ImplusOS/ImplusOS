#pragma once

/*
 * Userland/API/FFmpeg.h
 *
 * ImplusOS Userland FFmpeg API
 *
 * Wraps libavformat / libavcodec / libswscale to provide a simple
 * MP4 (and other container) video-decoding interface for user-space
 * applications.
 *
 * Usage pattern:
 *   ffmpeg_decoder_t *dec = ffmpeg_decoder_open(path, &info, err, sizeof err);
 *   ffmpeg_decoder_set_output_size(dec, out_w, out_h);
 *   while (ffmpeg_decoder_read_frame(dec, &img, &pts_us) == 0) { ... }
 *   ffmpeg_decoder_close(dec);
 *
 * Return-value convention used by read_frame:
 *    0   – frame available in *img
 *    1   – end of stream (EOF)
 *   <0   – transient error (try again or close)
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -----------------------------------------------------------------------
 * Types
 * --------------------------------------------------------------------- */

/**
 * Opaque decoder context.  Allocated by ffmpeg_decoder_open(); freed by
 * ffmpeg_decoder_close().
 */
typedef struct ffmpeg_decoder ffmpeg_decoder_t;

/**
 * Video stream metadata returned by ffmpeg_decoder_open().
 * All fields are zero-initialised when not available.
 */
typedef struct {
    uint32_t width;        /**< Coded width in pixels              */
    uint32_t height;       /**< Coded height in pixels             */
    uint32_t fps_num;      /**< Frame-rate numerator               */
    uint32_t fps_den;      /**< Frame-rate denominator             */
    int64_t  duration_us;  /**< Total stream duration in µs        */
} ffmpeg_video_info_t;

/**
 * RGBA pixel image.  The caller owns the rgba buffer; it must be allocated
 * before the first call to ffmpeg_decoder_read_frame() and freed afterwards.
 *
 * Pre-allocation pattern used by ffmpegTest:
 *   size_t stride = (((size_t)w * 4) + 63) & ~63;   // 64-byte aligned
 *   size_t size   = stride * h + 4096;               // guard headroom
 *   img.rgba   = malloc(size);
 *   img.width  = w;
 *   img.height = h;
 *   img.stride = (uint32_t)stride;
 *
 * When img.rgba is non-NULL on entry to read_frame the decoder writes the
 * scaled RGBA data directly into the provided buffer.  When it is NULL the
 * decoder allocates an internal buffer (caller must free img.rgba after use).
 */
typedef struct {
    uint8_t  *rgba;    /**< Pointer to RGBA pixel data (4 bytes / pixel) */
    uint32_t  width;   /**< Image width  in pixels                        */
    uint32_t  height;  /**< Image height in pixels                        */
    uint32_t  stride;  /**< Row stride in bytes (>= width * 4)            */
} ffmpeg_rgba_image_t;

/**
 * Audio stream metadata.
 */
typedef struct {
    int      sample_rate;
    int      channels;
    int64_t  duration_us;
} ffmpeg_audio_info_t;

/* -----------------------------------------------------------------------
 * API
 * --------------------------------------------------------------------- */

/**
 * Open a video file and initialise the decoder.
 *
 * @param path      Absolute path to the video file (e.g. "/Userland/…/test.mp4").
 * @param out_info  Receives stream metadata on success; may be NULL.
 * @param err_buf   Buffer for a human-readable error message on failure; may be NULL.
 * @param err_len   Size of err_buf in bytes.
 * @return          Non-NULL decoder handle on success, NULL on failure.
 */
ffmpeg_decoder_t *ffmpeg_decoder_open(const char        *path,
                                       ffmpeg_video_info_t *out_info,
                                       char              *err_buf,
                                       size_t             err_len);

/**
 * Set the output (scaled) resolution for subsequent read_frame() calls.
 *
 * May be called at any time before or between read_frame() calls.
 * If never called the output resolution defaults to the coded resolution.
 *
 * @param dec   Decoder handle.
 * @param width  Desired output width  in pixels (must be > 0).
 * @param height Desired output height in pixels (must be > 0).
 */
void ffmpeg_decoder_set_output_size(ffmpeg_decoder_t *dec,
                                     uint32_t          width,
                                     uint32_t          height);

/**
 * Set the target output sample rate for audio resampling.
 *
 * When set to a value different from the source sample rate, decoded
 * audio is linearly resampled to the target rate before being returned
 * by ffmpeg_decoder_read_audio().  Set to 0 (default) to skip resampling.
 *
 * @param dec   Decoder handle.
 * @param rate  Desired output sample rate in Hz, or 0 for passthrough.
 */
void ffmpeg_decoder_set_audio_target_rate(ffmpeg_decoder_t *dec,
                                           int               rate);

/**
 * Decode and return the next video frame.
 *
 * The decoded (and optionally scaled) frame is written as RGBA into
 * img->rgba.  If img->rgba is non-NULL on entry the decoder uses the
 * caller-supplied buffer (must be large enough: img->stride * img->height
 * bytes).  If img->rgba is NULL the decoder allocates its own buffer and
 * updates img->width, img->height, img->stride, and img->rgba accordingly;
 * the caller is responsible for freeing img->rgba.
 *
 * @param dec     Decoder handle (must not be NULL).
 * @param img     In/out image descriptor.
 * @param pts_us  Receives the presentation timestamp in microseconds.
 * @return  0  – success, frame available.
 *          1  – end of stream reached.
 *         -1  – recoverable error (skip and retry).
 *         <-1 – fatal decode error.
 */
int ffmpeg_decoder_read_frame(ffmpeg_decoder_t  *dec,
                               ffmpeg_rgba_image_t *img,
                               int64_t            *pts_us);

/**
 * Close a decoder and free all associated resources.
 *
 * Safe to call with NULL.
 *
 * @param dec  Decoder handle to close.
 */
void ffmpeg_decoder_close(ffmpeg_decoder_t *dec);

/**
 * Returns non-zero if an audio stream is present.
 */
int ffmpeg_decoder_has_audio(ffmpeg_decoder_t *dec);

/**
 * Retrieve audio stream metadata.
 * @return 0 on success, -1 if no audio stream.
 */
int ffmpeg_decoder_get_audio_info(ffmpeg_decoder_t   *dec,
                                   ffmpeg_audio_info_t *out_info);

/**
 * Read decoded PCM audio data (S16_LE, stereo interleaved).
 *
 * Should be called after ffmpeg_decoder_read_frame() which buffers audio
 * internally as a side effect.  If no audio data is currently buffered
 * this function returns 0 without blocking.
 *
 * @param dec      Decoder handle.
 * @param buf      Caller-owned buffer for PCM data.
 * @param buf_size Capacity of buf in bytes.
 * @return Number of bytes written into buf, or 0 if none available.
 */
int ffmpeg_decoder_read_audio(ffmpeg_decoder_t *dec,
                               uint8_t          *buf,
                               int               buf_size);

#ifdef __cplusplus
}
#endif