#include "SDL_internal.h"

#include "audio/SDL_sysaudio.h"

#include "Userland/API/Audio.h"

struct SDL_PrivateAudioData {
    Uint8 *mixbuf;
    Uint32 delay_ms;
};

static bool implus_audio_open_device(SDL_AudioDevice *device)
{
    if (device->recording) {
        return SDL_SetError("ImplusOS recording devices are not supported");
    }

    if (os_audio_open() < 0) {
        return SDL_SetError("os_audio_open failed");
    }

    device->hidden = (struct SDL_PrivateAudioData *)SDL_calloc(1, sizeof(*device->hidden));
    if (!device->hidden) {
        os_audio_close();
        return false;
    }

    os_audio_info_t info;
    if (os_audio_get_info(&info) == 0) {
        if (info.sample_rate != 0) {
            device->spec.freq = (int)info.sample_rate;
        }
        if (info.channels != 0) {
            device->spec.channels = (int)info.channels;
        }
    }

    if (device->spec.freq <= 0) {
        device->spec.freq = 44100;
    }
    if (device->spec.channels <= 0) {
        device->spec.channels = 2;
    }
    device->spec.format = SDL_AUDIO_S16;
    if (device->sample_frames <= 0) {
        device->sample_frames = SDL_GetDefaultSampleFramesFromFreq(device->spec.freq);
    }

    SDL_UpdatedAudioDeviceFormat(device);

    device->hidden->mixbuf = (Uint8 *)SDL_malloc((size_t)device->buffer_size);
    if (!device->hidden->mixbuf) {
        os_audio_close();
        SDL_free(device->hidden);
        device->hidden = NULL;
        return false;
    }

    device->hidden->delay_ms = (Uint32)((device->sample_frames * 1000) / device->spec.freq);
    if (device->hidden->delay_ms == 0) {
        device->hidden->delay_ms = 1;
    }
    return true;
}

static bool implus_audio_wait_device(SDL_AudioDevice *device)
{
    if (device && device->hidden && device->hidden->delay_ms != 0) {
        SDL_Delay(device->hidden->delay_ms);
    }
    return true;
}

static bool implus_audio_play_device(SDL_AudioDevice *device, const Uint8 *buffer, int buflen)
{
    (void)device;
    int64_t written = os_audio_write(buffer, (uint64_t)buflen);
    return written == (int64_t)buflen;
}

static Uint8 *implus_audio_get_device_buf(SDL_AudioDevice *device, int *buffer_size)
{
    if (buffer_size) {
        *buffer_size = device->buffer_size;
    }
    return device->hidden ? device->hidden->mixbuf : NULL;
}

static void implus_audio_close_device(SDL_AudioDevice *device)
{
    if (!device || !device->hidden) {
        return;
    }

    SDL_free(device->hidden->mixbuf);
    device->hidden->mixbuf = NULL;
    SDL_free(device->hidden);
    device->hidden = NULL;

    (void)os_audio_drain(250);
    (void)os_audio_close();
}

static bool implus_audio_init(SDL_AudioDriverImpl *impl)
{
    impl->OpenDevice = implus_audio_open_device;
    impl->WaitDevice = implus_audio_wait_device;
    impl->PlayDevice = implus_audio_play_device;
    impl->GetDeviceBuf = implus_audio_get_device_buf;
    impl->CloseDevice = implus_audio_close_device;
    impl->OnlyHasDefaultPlaybackDevice = true;
    impl->HasRecordingSupport = false;
    return true;
}

AudioBootStrap PRIVATEAUDIO_bootstrap = {
    "ImplusOS",
    "ImplusOS audio driver",
    implus_audio_init,
    false,
    true
};
