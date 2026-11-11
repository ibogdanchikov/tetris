#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "audio/mix.h"
#include "audio/synth.h"

/*
 * Playback device for the synthesised effects. This is the only module that
 * touches SDL audio; the synth and the mixer are pure so the tests never link
 * SDL.
 *
 * Failure is never fatal. If the subsystem, the device or the buffers cannot be
 * set up, audio_init logs a warning and returns false; every other audio_*
 * function then does nothing, and the game runs silent.
 *
 * Everything the effects need is pre-rendered at init: one mono float buffer per
 * SoundId at AUDIO_SAMPLE_RATE, plus one scratch mix buffer. That is the only
 * allocation, and audio_shutdown frees it all.
 */

/* Forward declaration so this header needs no SDL include; audio.c includes it. */
typedef struct SDL_AudioStream SDL_AudioStream;

/* The stream format we ask SDL for; SDL converts to whatever the device wants. */
#define AUDIO_SAMPLE_RATE 48000

/* Level the whole mix is multiplied by, on top of the per-sound and per-voice
   gains. Well below 1 so a stack of sounds still has headroom. */
#define AUDIO_MASTER_GAIN 0.60f

/* Level of the music against the effects, before the music volume setting. The
   music is background, so it sits under the effects. */
#define AUDIO_MUSIC_GAIN 0.50f

typedef struct Audio {
    SDL_AudioStream *stream;
    bool ready;        /* device open, buffers allocated, callback may run */
    bool subsystem;    /* SDL_INIT_AUDIO was initialised and must be quit */
    bool muted;
    float volume; /* 0..1, multiplies AUDIO_MASTER_GAIN; written under the stream lock */

    /* Pre-rendered effects, indexed by SoundId; NULL when the sound is missing. */
    float *sounds[SOUND_COUNT];
    int sound_lengths[SOUND_COUNT];

    MixVoice voices[MIX_MAX_VOICES];
    uint32_t voice_serial[MIX_MAX_VOICES]; /* start order, for stealing the oldest */
    uint32_t next_serial;

    /*
     * The music loop, rendered once at init and played on its own voice, outside
     * the pool: audio_play can never steal it and audio_stop_all leaves it
     * running. music_level is the target set by audio_set_music_volume (under
     * the stream lock); music_gain is the callback's current level, which walks
     * towards it a step per block, so a change fades instead of clicking and the
     * music fades in when the game starts.
     */
    float *music;
    MixLoop music_voice;
    float music_level;
    float music_gain;

    /*
     * The mute fade. muted is written by the app thread under the stream lock and
     * read by the callback; master_gain is only touched by the callback, which
     * walks it towards the target a step per block so muting does not click.
     */
    float master_gain;

    float *scratch; /* mix destination, owned by the callback while it runs */
    int scratch_frames;
} Audio;

/*
 * Initialises SDL audio, opens the default playback device with a mix callback,
 * pre-renders every effect and the music loop and resumes the stream (SDL opens
 * it paused). Music that cannot be allocated is skipped with a warning and the
 * effects still play. Returns false after logging a warning if anything else
 * fails; the struct is then safe to pass to audio_shutdown and every other call
 * becomes a no-op.
 */
bool audio_init(Audio *audio);

/*
 * Starts a sound on the next free voice; when all voices are busy the oldest one
 * is stolen. gain is clamped to 0..1 and multiplies the synth level. A no-op
 * when audio_init failed, and also when muted: the mute fade silences what is
 * already playing, and starting new voices under it would only cost CPU.
 */
void audio_play(Audio *audio, SoundId id, float gain);

/* Stops every voice, for example on restart. */
void audio_stop_all(Audio *audio);

/* Silences or restores the mix; the change is faded in by the callback. */
void audio_set_muted(Audio *audio, bool muted);

bool audio_muted(const Audio *audio);

/*
 * Sets the music level, clamped to 0..1, on top of the volume: 0 silences the
 * music, which keeps playing silently so it resumes in place. Mute silences it
 * too. audio_init sets it to 1, so call this after audio_init.
 */
void audio_set_music_volume(Audio *audio, float level);

/*
 * Sets the volume, clamped to 0..1, as a factor on AUDIO_MASTER_GAIN. The
 * change is faded in by the callback like mute. At 0 no new sound starts.
 * audio_init resets it to 1, so call this after audio_init.
 */
void audio_set_volume(Audio *audio, float volume);

/* Destroys the stream, frees the buffers and quits the audio subsystem. */
void audio_shutdown(Audio *audio);
