#pragma once

#include <stdbool.h>

/*
 * Voice mixing for the audio thread, kept pure and SDL-free so the tests can run
 * it directly. A voice is just a window into an already-rendered mono buffer:
 * the synth owns the samples, a voice owns how far through them it is.
 *
 * The mix is "sum of voices x gain x master, then a soft clip". Output stays in
 * [-1, 1] even when many loud sounds stack, and the clip curve is close to
 * linear at the levels the game actually uses.
 */

/* Fixed voice capacity, shared by the mixer and the device. */
#define MIX_MAX_VOICES 16

typedef struct MixVoice {
    const float *samples; /* NULL when the slot is free */
    int length;           /* frames in samples */
    int position;         /* frames already played */
    float gain;           /* per-voice gain, usually the request's volume */
} MixVoice;

/*
 * A looping voice for the music. It never runs out and lives outside the voice
 * pool, so a burst of sound effects can never take its place.
 */
typedef struct MixLoop {
    const float *samples; /* NULL for no music */
    int length;           /* frames in samples */
    int position;         /* next frame to play, 0..length-1 */
} MixLoop;

/* True while the voice has samples left to contribute. */
bool mix_voice_active(const MixVoice *voice);

/*
 * Mixes every active voice into out[0..frames) and overwrites it with the sum.
 * The master gain ramps linearly from master_from to master_to across the block,
 * with master_to reached on the last sample, so the caller can fade a mute
 * change over a few blocks instead of stepping the volume and clicking.
 *
 * Each voice advances by the mixed length. A voice that reaches its end inside
 * the block is cleared (samples set to NULL) and contributes silence for the
 * rest of it, so a finished sound frees its slot immediately.
 *
 * Returns the number of voices still active afterwards. out == NULL or
 * frames <= 0 mixes nothing and only counts.
 */
int mix_voices(MixVoice *voices, int voice_count, float master_from, float master_to, float *out,
               int frames);

/*
 * Adds the loop into out[0..frames) at a gain that ramps linearly from
 * gain_from to gain_to (reached on the last sample), wrapping to the start
 * whenever it reaches the end. Call it after mix_voices, which overwrites out.
 * At a gain of zero it adds nothing but still moves on, so muted music keeps its
 * place. An empty loop, out == NULL or frames <= 0 does nothing.
 */
void mix_add_loop(MixLoop *loop, float gain_from, float gain_to, float *out, int frames);

/* x / (1 + |x|): odd, saturating below 1, and ~linear for small values. */
float mix_soft_clip(float sample);

/* Applies mix_soft_clip to every sample of the buffer. */
void mix_clip(float *out, int frames);
