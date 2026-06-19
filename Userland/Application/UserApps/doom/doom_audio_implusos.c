#include <Audio.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "deh_str.h"
#include "doomtype.h"
#include "i_sound.h"
#include "i_timer.h"
#include "m_misc.h"
#include "w_wad.h"
#include "z_zone.h"

#define IMPLUS_DOOM_AUDIO_CHANNELS 16
#define IMPLUS_DOOM_AUDIO_MIN_FRAMES 512u
#define IMPLUS_DOOM_AUDIO_MAX_FRAMES 4096u

typedef struct cached_sound {
    sfxinfo_t *sfxinfo;
    int16_t *samples;
    uint32_t frames;
    uint32_t use_count;
    struct cached_sound *next;
} cached_sound_t;

typedef struct {
    cached_sound_t *sound;
    uint32_t position;
    int left_volume;
    int right_volume;
} audio_channel_t;

int use_libsamplerate = 0;
float libsamplerate_scale = 0.65f;

static snddevice_t g_sound_devices[] = {
    SNDDEVICE_SB,
    SNDDEVICE_PAS,
    SNDDEVICE_GUS,
    SNDDEVICE_WAVEBLASTER,
    SNDDEVICE_SOUNDCANVAS,
    SNDDEVICE_AWE32,
};

static boolean g_sound_initialized = false;
static boolean g_use_sfx_prefix = false;
static uint32_t g_output_rate = 48000u;
static uint32_t g_frames_per_update = 1371u;
static audio_channel_t g_channels[IMPLUS_DOOM_AUDIO_CHANNELS];
static int32_t *g_mix_accum;
static int16_t *g_mix_pcm;
static cached_sound_t *g_cached_sounds;

static int16_t clamp_i16(int32_t value)
{
    if (value > INT16_MAX) {
        return INT16_MAX;
    }
    if (value < INT16_MIN) {
        return INT16_MIN;
    }
    return (int16_t)value;
}

static void get_sfx_lump_name(sfxinfo_t *sfx, char *buf, size_t buf_len)
{
    if (sfx->link != NULL) {
        sfx = sfx->link;
    }

    if (g_use_sfx_prefix) {
        M_snprintf(buf, buf_len, "ds%s", DEH_String(sfx->name));
    } else {
        M_StringCopy(buf, DEH_String(sfx->name), buf_len);
    }
}

static void unlock_sound(cached_sound_t *sound)
{
    if (sound != NULL && sound->use_count > 0u) {
        --sound->use_count;
    }
}

static void stop_channel(int channel)
{
    if (channel < 0 || channel >= IMPLUS_DOOM_AUDIO_CHANNELS) {
        return;
    }

    unlock_sound(g_channels[channel].sound);
    memset(&g_channels[channel], 0, sizeof(g_channels[channel]));
}

static boolean convert_sound(sfxinfo_t *sfxinfo, const byte *data,
                             int samplerate, uint32_t length)
{
    uint32_t frames;
    int16_t *samples;
    cached_sound_t *sound;

    if (samplerate <= 0 || length == 0u) {
        return false;
    }

    frames = (uint32_t)(((uint64_t)length * g_output_rate) / (uint32_t)samplerate);
    if (frames == 0u) {
        return false;
    }

    samples = (int16_t *)malloc((size_t)frames * 2u * sizeof(int16_t));
    sound = (cached_sound_t *)malloc(sizeof(*sound));
    if (samples == NULL || sound == NULL) {
        free(samples);
        free(sound);
        return false;
    }

    for (uint32_t i = 0; i < frames; ++i) {
        uint32_t src = (uint32_t)(((uint64_t)i * (uint32_t)samplerate) / g_output_rate);
        if (src >= length) {
            src = length - 1u;
        }

        int16_t sample = (int16_t)(((int)data[src] - 128) << 8);
        samples[i * 2u] = sample;
        samples[i * 2u + 1u] = sample;
    }

    sound->sfxinfo = sfxinfo;
    sound->samples = samples;
    sound->frames = frames;
    sound->use_count = 0u;
    sound->next = g_cached_sounds;
    g_cached_sounds = sound;
    sfxinfo->driver_data = sound;

    return true;
}

static boolean cache_sfx(sfxinfo_t *sfxinfo)
{
    int lumpnum = sfxinfo->lumpnum;
    int lumplen;
    byte *data;
    int samplerate;
    uint32_t length;
    boolean result;

    if (lumpnum < 0) {
        return false;
    }

    lumplen = W_LumpLength((unsigned int)lumpnum);
    data = W_CacheLumpNum(lumpnum, PU_STATIC);

    if (lumplen < 8 || data[0] != 0x03 || data[1] != 0x00) {
        W_ReleaseLumpNum(lumpnum);
        return false;
    }

    samplerate = ((int)data[3] << 8) | (int)data[2];
    length = ((uint32_t)data[7] << 24) | ((uint32_t)data[6] << 16) |
             ((uint32_t)data[5] << 8) | (uint32_t)data[4];

    if (length > (uint32_t)lumplen - 8u || length <= 48u) {
        W_ReleaseLumpNum(lumpnum);
        return false;
    }

    result = convert_sound(sfxinfo, data + 24, samplerate, length - 32u);
    W_ReleaseLumpNum(lumpnum);
    return result;
}

static boolean lock_sound(sfxinfo_t *sfxinfo)
{
    cached_sound_t *sound;

    if (sfxinfo->driver_data == NULL && !cache_sfx(sfxinfo)) {
        return false;
    }

    sound = (cached_sound_t *)sfxinfo->driver_data;
    ++sound->use_count;
    return true;
}

static boolean implus_sound_init(boolean use_sfx_prefix)
{
    os_audio_info_t info;

    g_use_sfx_prefix = use_sfx_prefix;
    memset(g_channels, 0, sizeof(g_channels));

    if (os_audio_open() < 0) {
        return false;
    }

    if (os_audio_get_info(&info) == 0 && info.sample_rate != 0u) {
        g_output_rate = info.sample_rate;
    }

    g_frames_per_update = g_output_rate / TICRATE;
    if (g_frames_per_update < IMPLUS_DOOM_AUDIO_MIN_FRAMES) {
        g_frames_per_update = IMPLUS_DOOM_AUDIO_MIN_FRAMES;
    }
    if (g_frames_per_update > IMPLUS_DOOM_AUDIO_MAX_FRAMES) {
        g_frames_per_update = IMPLUS_DOOM_AUDIO_MAX_FRAMES;
    }

    g_mix_accum = (int32_t *)malloc((size_t)g_frames_per_update * 2u * sizeof(int32_t));
    g_mix_pcm = (int16_t *)malloc((size_t)g_frames_per_update * 2u * sizeof(int16_t));
    if (g_mix_accum == NULL || g_mix_pcm == NULL) {
        free(g_mix_accum);
        free(g_mix_pcm);
        g_mix_accum = NULL;
        g_mix_pcm = NULL;
        (void)os_audio_close();
        return false;
    }

    g_sound_initialized = true;
    return true;
}

static void implus_sound_shutdown(void)
{
    cached_sound_t *sound = g_cached_sounds;

    if (!g_sound_initialized) {
        return;
    }

    (void)os_audio_drain(250);
    (void)os_audio_close();

    while (sound != NULL) {
        cached_sound_t *next = sound->next;
        free(sound->samples);
        free(sound);
        sound = next;
    }

    g_cached_sounds = NULL;
    free(g_mix_accum);
    free(g_mix_pcm);
    g_mix_accum = NULL;
    g_mix_pcm = NULL;
    g_sound_initialized = false;
}

static int implus_get_sfx_lump_num(sfxinfo_t *sfxinfo)
{
    char namebuf[9];

    get_sfx_lump_name(sfxinfo, namebuf, sizeof(namebuf));
    return W_GetNumForName(namebuf);
}

static void implus_update_sound_params(int channel, int vol, int sep)
{
    if (!g_sound_initialized || channel < 0 ||
        channel >= IMPLUS_DOOM_AUDIO_CHANNELS) {
        return;
    }

    g_channels[channel].left_volume = ((254 - sep) * vol) / 127;
    g_channels[channel].right_volume = (sep * vol) / 127;

    if (g_channels[channel].left_volume < 0) {
        g_channels[channel].left_volume = 0;
    } else if (g_channels[channel].left_volume > 255) {
        g_channels[channel].left_volume = 255;
    }

    if (g_channels[channel].right_volume < 0) {
        g_channels[channel].right_volume = 0;
    } else if (g_channels[channel].right_volume > 255) {
        g_channels[channel].right_volume = 255;
    }
}

static int implus_start_sound(sfxinfo_t *sfxinfo, int channel, int vol, int sep)
{
    cached_sound_t *sound;

    if (!g_sound_initialized || channel < 0 ||
        channel >= IMPLUS_DOOM_AUDIO_CHANNELS) {
        return -1;
    }

    stop_channel(channel);

    if (!lock_sound(sfxinfo)) {
        return -1;
    }

    sound = (cached_sound_t *)sfxinfo->driver_data;
    g_channels[channel].sound = sound;
    g_channels[channel].position = 0u;
    implus_update_sound_params(channel, vol, sep);

    return channel;
}

static void implus_stop_sound(int channel)
{
    stop_channel(channel);
}

static boolean implus_sound_is_playing(int channel)
{
    return g_sound_initialized &&
           channel >= 0 &&
           channel < IMPLUS_DOOM_AUDIO_CHANNELS &&
           g_channels[channel].sound != NULL;
}

static void implus_update_sound(void)
{
    size_t sample_count;

    if (!g_sound_initialized || g_mix_accum == NULL || g_mix_pcm == NULL) {
        return;
    }

    sample_count = (size_t)g_frames_per_update * 2u;
    memset(g_mix_accum, 0, sample_count * sizeof(int32_t));

    for (int ch = 0; ch < IMPLUS_DOOM_AUDIO_CHANNELS; ++ch) {
        audio_channel_t *channel = &g_channels[ch];
        cached_sound_t *sound = channel->sound;

        if (sound == NULL) {
            continue;
        }

        for (uint32_t frame = 0; frame < g_frames_per_update; ++frame) {
            if (channel->position >= sound->frames) {
                stop_channel(ch);
                break;
            }

            int16_t left = sound->samples[channel->position * 2u];
            int16_t right = sound->samples[channel->position * 2u + 1u];
            g_mix_accum[frame * 2u] += ((int32_t)left * channel->left_volume) / 255;
            g_mix_accum[frame * 2u + 1u] += ((int32_t)right * channel->right_volume) / 255;
            ++channel->position;
        }
    }

    for (size_t i = 0; i < sample_count; ++i) {
        g_mix_pcm[i] = clamp_i16(g_mix_accum[i]);
    }

    (void)os_audio_write(g_mix_pcm,
                         (uint64_t)(sample_count * sizeof(int16_t)));
}

static void implus_precache_sounds(sfxinfo_t *sounds, int num_sounds)
{
    (void)sounds;
    (void)num_sounds;
}

sound_module_t DG_sound_module = {
    g_sound_devices,
    (int)(sizeof(g_sound_devices) / sizeof(g_sound_devices[0])),
    implus_sound_init,
    implus_sound_shutdown,
    implus_get_sfx_lump_num,
    implus_update_sound,
    implus_update_sound_params,
    implus_start_sound,
    implus_stop_sound,
    implus_sound_is_playing,
    implus_precache_sounds,
};

static boolean implus_music_init(void) { return false; }
static void implus_music_shutdown(void) {}
static void implus_music_set_volume(int volume) { (void)volume; }
static void implus_music_pause(void) {}
static void implus_music_resume(void) {}
static void *implus_music_register(void *data, int len)
{
    (void)data;
    (void)len;
    return NULL;
}
static void implus_music_unregister(void *handle) { (void)handle; }
static void implus_music_play(void *handle, boolean looping)
{
    (void)handle;
    (void)looping;
}
static void implus_music_stop(void) {}
static boolean implus_music_is_playing(void) { return false; }
static void implus_music_poll(void) {}

music_module_t DG_music_module = {
    g_sound_devices,
    (int)(sizeof(g_sound_devices) / sizeof(g_sound_devices[0])),
    implus_music_init,
    implus_music_shutdown,
    implus_music_set_volume,
    implus_music_pause,
    implus_music_resume,
    implus_music_register,
    implus_music_unregister,
    implus_music_play,
    implus_music_stop,
    implus_music_is_playing,
    implus_music_poll,
};
