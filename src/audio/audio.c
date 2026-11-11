#include "audio/audio.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

/*
 * The mixing callback runs on SDL's audio thread while the app calls audio_play
 * from the main thread. SDL_LockAudioStream is used to serialise them: the
 * callback already runs with that lock held, so the app only has to take it
 * around the voice array and the mute flag. No extra mutex is needed, and the
 * callback never takes the lock itself, which would deadlock.
 *
 * Logging uses SDL_LOG_CATEGORY_APPLICATION rather than the audio category:
 * SDL's audio category defaults to ERROR, which would swallow the warnings this
 * module exists to print on a machine without a working device.
 */

/* Frames rendered per mixing step in the callback; 1024 is about 21 ms at 48 kHz. */
#define AUDIO_SCRATCH_FRAMES 1024

/* How far the mute fade moves per block: about three blocks, roughly 60 ms. */
#define AUDIO_GAIN_STEP 0.15f

static float audio_clamp_gain(float gain)
{
    if (gain < 0.0f) {
        return 0.0f;
    }
    if (gain > 1.0f) {
        return 1.0f;
    }
    return gain;
}

/*
 * The callback asks for a number of bytes in the stream's input format (mono
 * float32), so it renders that many frames in scratch-sized blocks. It does not
 * take the stream lock: SDL holds it while the callback runs.
 */
static void SDLCALL audio_mix_callback(void *userdata, SDL_AudioStream *stream,
                                       int additional_amount, int total_amount)
{
    (void)total_amount;
    Audio *audio = userdata;
    if (!audio->ready || audio->scratch == NULL || audio->scratch_frames <= 0) {
        return;
    }

    int frames = additional_amount / (int)sizeof(float);
    while (frames > 0) {
        const int chunk = frames > audio->scratch_frames ? audio->scratch_frames : frames;

        /* Move the master gain one step towards its target, then ramp across
           the block, so muting fades instead of clicking. */
        const float target = audio->muted ? 0.0f : AUDIO_MASTER_GAIN * audio->volume;
        const float from = audio->master_gain;
        float to = target;
        if (target - from > AUDIO_GAIN_STEP) {
            to = from + AUDIO_GAIN_STEP;
        } else if (from - target > AUDIO_GAIN_STEP) {
            to = from - AUDIO_GAIN_STEP;
        }

        mix_voices(audio->voices, MIX_MAX_VOICES, from, to, audio->scratch, chunk);
        mix_clip(audio->scratch, chunk);
        audio->master_gain = to;

        if (!SDL_PutAudioStreamData(stream, audio->scratch, chunk * (int)sizeof(float))) {
            break;
        }
        frames -= chunk;
    }
}

static void audio_log_device(const Audio *audio)
{
    const SDL_AudioDeviceID device = SDL_GetAudioStreamDevice(audio->stream);
    const char *driver = SDL_GetCurrentAudioDriver();
    const char *name = SDL_GetAudioDeviceName(device);

    SDL_AudioSpec device_spec = { SDL_AUDIO_UNKNOWN, 0, 0 };
    int device_frames = 0;
    (void)SDL_GetAudioDeviceFormat(device, &device_spec, &device_frames);

    SDL_Log("audio: opened \"%s\" via %s", name != NULL ? name : "<default>",
            driver != NULL ? driver : "<unknown>");
    SDL_Log("audio: stream %s %d ch %d Hz, device %s %d ch %d Hz, %d frame buffer",
            SDL_GetAudioFormatName(SDL_AUDIO_F32), 1, AUDIO_SAMPLE_RATE,
            SDL_GetAudioFormatName(device_spec.format), device_spec.channels, device_spec.freq,
            device_frames);
}

bool audio_init(Audio *audio)
{
    memset(audio, 0, sizeof *audio);
    audio->master_gain = AUDIO_MASTER_GAIN;
    audio->volume = 1.0f;

    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "audio disabled: SDL_InitSubSystem: %s",
                    SDL_GetError());
        return false;
    }
    audio->subsystem = true;

    const SDL_AudioSpec spec = { SDL_AUDIO_F32, 1, AUDIO_SAMPLE_RATE };
    audio->stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec,
                                              audio_mix_callback, audio);
    if (audio->stream == NULL) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "audio disabled: SDL_OpenAudioDeviceStream: %s", SDL_GetError());
        audio_shutdown(audio);
        return false;
    }

    audio->scratch_frames = AUDIO_SCRATCH_FRAMES;
    audio->scratch = malloc((size_t)audio->scratch_frames * sizeof(float));
    if (audio->scratch == NULL) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "audio disabled: out of memory for the mix buffer");
        audio_shutdown(audio);
        return false;
    }

    for (int id = 0; id < SOUND_COUNT; ++id) {
        const int length = synth_length((SoundId)id, AUDIO_SAMPLE_RATE);
        if (length <= 0) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "audio: sound %s has no samples",
                        synth_name((SoundId)id));
            continue;
        }

        float *samples = malloc((size_t)length * sizeof(float));
        if (samples == NULL) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "audio disabled: out of memory for sound %s",
                        synth_name((SoundId)id));
            audio_shutdown(audio);
            return false;
        }
        audio->sounds[id] = samples;
        audio->sound_lengths[id] = synth_render((SoundId)id, AUDIO_SAMPLE_RATE, samples, length);
    }

    /* The callback may start as soon as the stream is resumed, so everything it
       reads has to be in place by now. */
    audio->ready = true;
    if (!SDL_ResumeAudioStreamDevice(audio->stream)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "audio disabled: SDL_ResumeAudioStreamDevice: %s",
                    SDL_GetError());
        audio_shutdown(audio);
        return false;
    }

    audio_log_device(audio);
    return true;
}

void audio_play(Audio *audio, SoundId id, float gain)
{
    if (audio == NULL || !audio->ready || audio->muted || audio->volume <= 0.0f) {
        return;
    }
    if ((unsigned)id >= (unsigned)SOUND_COUNT) {
        return;
    }
    if (audio->sounds[id] == NULL || audio->sound_lengths[id] <= 0) {
        return;
    }

    if (!SDL_LockAudioStream(audio->stream)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SDL_LockAudioStream: %s", SDL_GetError());
        return;
    }

    /* First free voice, otherwise the one that started longest ago. */
    int slot = -1;
    for (int i = 0; i < MIX_MAX_VOICES; ++i) {
        if (!mix_voice_active(&audio->voices[i])) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        slot = 0;
        for (int i = 1; i < MIX_MAX_VOICES; ++i) {
            if (audio->voice_serial[i] < audio->voice_serial[slot]) {
                slot = i;
            }
        }
    }

    audio->voices[slot].samples = audio->sounds[id];
    audio->voices[slot].length = audio->sound_lengths[id];
    audio->voices[slot].position = 0;
    audio->voices[slot].gain = audio_clamp_gain(gain);
    audio->voice_serial[slot] = audio->next_serial++;

    SDL_UnlockAudioStream(audio->stream);
}

void audio_stop_all(Audio *audio)
{
    if (audio == NULL || !audio->ready) {
        return;
    }
    if (!SDL_LockAudioStream(audio->stream)) {
        return;
    }
    memset(audio->voices, 0, sizeof audio->voices);
    SDL_UnlockAudioStream(audio->stream);
}

void audio_set_muted(Audio *audio, bool muted)
{
    if (audio == NULL) {
        return;
    }
    if (!audio->ready) {
        audio->muted = muted;
        return;
    }
    if (!SDL_LockAudioStream(audio->stream)) {
        return;
    }
    audio->muted = muted;
    SDL_UnlockAudioStream(audio->stream);
}

bool audio_muted(const Audio *audio)
{
    return audio != NULL && audio->muted;
}

void audio_set_volume(Audio *audio, float volume)
{
    if (audio == NULL) {
        return;
    }
    if (!(volume > 0.0f)) {
        volume = 0.0f;
    } else if (volume > 1.0f) {
        volume = 1.0f;
    }
    if (!audio->ready) {
        audio->volume = volume;
        return;
    }
    if (!SDL_LockAudioStream(audio->stream)) {
        return;
    }
    audio->volume = volume;
    SDL_UnlockAudioStream(audio->stream);
}

void audio_shutdown(Audio *audio)
{
    if (audio == NULL) {
        return;
    }

    /*
     * Destroying the stream stops the callback, and SDL waits for a callback
     * that is already running, so nothing it reads is freed underneath it.
     */
    if (audio->stream != NULL) {
        SDL_DestroyAudioStream(audio->stream);
        audio->stream = NULL;
    }
    audio->ready = false;

    for (int id = 0; id < SOUND_COUNT; ++id) {
        free(audio->sounds[id]);
        audio->sounds[id] = NULL;
        audio->sound_lengths[id] = 0;
    }
    free(audio->scratch);
    audio->scratch = NULL;
    audio->scratch_frames = 0;

    if (audio->subsystem) {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        audio->subsystem = false;
    }
}
