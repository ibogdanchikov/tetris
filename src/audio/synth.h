#pragma once

#include <stdint.h>

/*
 * Sound effects synthesised from scratch: no SDL, no OpenGL, no audio files and
 * no libm. Every effect is a short note list rendered into a caller-provided
 * float mono buffer, so the audio layer can pre-render them at startup and the
 * tests can check the waveforms directly.
 *
 * Rendering is deterministic: the note tables are constant and the noise
 * generator is seeded from the sound id and note index, never from the clock.
 * Rendering the same sound twice therefore gives bit-identical samples, which
 * is what lets the audio layer pre-render every effect once at startup.
 *
 * Waveforms are the ones a small retro game needs: a fast sine approximation, a
 * triangle, a soft (three-harmonic) square and xorshift noise. Envelopes are
 * linear attack / release, so every note starts and ends at zero and no effect
 * clicks. Levels are deliberately conservative (peak under 0.6) so several
 * sounds can be mixed without clipping.
 */

typedef enum SoundId {
    SOUND_MOVE = 0, /* a very short, quiet tick while a piece shifts */
    SOUND_ROTATE,
    SOUND_HOLD,
    SOUND_HARD_DROP, /* low thud: a sine sweep down plus a noise burst */
    SOUND_LOCK,      /* soft click / thump when a piece settles */
    SOUND_CLEAR_1,   /* rising arpeggio, one note per cleared line */
    SOUND_CLEAR_2,
    SOUND_CLEAR_3,
    SOUND_CLEAR_4, /* brighter and longer: a tetris (four lines) */
    SOUND_LEVEL_UP,
    SOUND_GAME_OVER,
    SOUND_PAUSE, /* one soft blip, used for pause and resume */
    SOUND_FINISH, /* a rising fanfare for a finished Sprint or Ultra */
    SOUND_COUNT
} SoundId;

/* No effect is longer than this; the buffers the audio layer allocates are sized
   from synth_length, and the tests pin the bound. */
#define SYNTH_MAX_SECONDS 2.0f

/* Peak a single effect may reach. Stacked voices are soft-clipped later. */
#define SYNTH_MAX_PEAK 0.6f

/* Name for logs and for dumping sounds to files; never NULL. */
const char *synth_name(SoundId id);

/*
 * Number of frames synth_render writes for this sound at this sample rate, or 0
 * for an unknown id or a non-positive sample rate. The result is never more
 * than SYNTH_MAX_SECONDS worth of frames.
 */
int synth_length(SoundId id, int sample_rate);

/*
 * The background music: one seamless loop, 8 bars at 120 beats per minute in A
 * minor (Am F C G Am F Dm E), with an octave bass, a soft pad, a quiet
 * arpeggio, light drums and a lead melody. It is rendered as a loop: the end of
 * the buffer runs straight into its start, so playing it over and over never
 * clicks.
 */
#define SYNTH_MUSIC_SECONDS 16.0f
#define SYNTH_MUSIC_MAX_PEAK 0.6f

/* Frames in the music loop at this sample rate, or 0 for a non-positive rate. */
int synth_music_length(int sample_rate);

/*
 * Renders the music loop into out and returns its length in frames. A loop
 * cannot be cut short, so a capacity below synth_music_length writes nothing
 * and returns 0, as does out == NULL. Deterministic, like the effects.
 */
int synth_music_render(int sample_rate, float *out, int capacity);

/*
 * Renders the sound into out as mono float samples and returns how many frames
 * were written: the smaller of synth_length and capacity. If the capacity is
 * short of the full length the buffer holds the beginning of the sound, exactly
 * as the full render would have started, so a truncated render is still
 * deterministic. out == NULL or capacity <= 0 writes nothing and returns 0.
 */
int synth_render(SoundId id, int sample_rate, float *out, int capacity);
