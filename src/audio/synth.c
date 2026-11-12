#include "audio/synth.h"

#include <stdbool.h>
#include <stddef.h>

/*
 * Every effect is a list of notes. A note is a waveform with a linear pitch
 * sweep, a linear attack / release envelope and an optional one-pole low-pass.
 * synth_render adds all notes of a sound into the output buffer, so overlapping
 * notes simply sum and there is no per-sound mixing code.
 *
 * Frequencies use equal temperament values of a C major / A minor set; nothing
 * depends on libm, so the project still links without -lm.
 */

#define SYNTH_TAIL_SECONDS 0.005f /* silence after the last note, so the end is clean */
#define SYNTH_EDGE_SECONDS 0.002f /* fade at the very start and end of the buffer */

typedef enum SynthWave {
    SYNTH_WAVE_SINE = 0,
    SYNTH_WAVE_TRIANGLE,
    SYNTH_WAVE_SQUARE,
    SYNTH_WAVE_NOISE,
} SynthWave;

typedef struct SynthNote {
    float start;    /* seconds from the start of the sound */
    float duration; /* seconds */
    float hz_from;  /* pitch at the start of the note, swept linearly */
    float hz_to;    /* pitch at the end of the note */
    float volume;   /* 0..1, before the per-sound mix */
    float attack;   /* seconds of fade-in; must be > 0 so the note starts at zero */
    float release;  /* seconds of fade-out at the end; must be > 0 */
    float lowpass;  /* one-pole coefficient 0..1; 0 leaves the wave untouched */
    SynthWave wave;
} SynthNote;

#define NOTE(start_, duration_, from_, to_, volume_, attack_, release_, lowpass_, wave_)        \
    { (start_), (duration_), (from_), (to_), (volume_), (attack_), (release_), (lowpass_),       \
      (wave_) }

/*
 * A move is the quietest thing that happens, often several times a second while
 * ARR repeats, so it is a 20 ms tick and nothing more.
 */
static const SynthNote MOVE_NOTES[] = {
    NOTE(0.000f, 0.026f, 1046.0f, 880.0f, 0.11f, 0.002f, 0.018f, 0.50f, SYNTH_WAVE_TRIANGLE),
};

/* A short upward slide, deliberately brighter than a move. */
static const SynthNote ROTATE_NOTES[] = {
    NOTE(0.000f, 0.070f, 587.0f, 784.0f, 0.15f, 0.003f, 0.040f, 0.60f, SYNTH_WAVE_TRIANGLE),
};

static const SynthNote HOLD_NOTES[] = {
    NOTE(0.000f, 0.095f, 440.0f, 659.0f, 0.17f, 0.004f, 0.050f, 0.40f, SYNTH_WAVE_SINE),
};

/*
 * The thud: a low sine falling from 200 Hz to 48 Hz carries the weight, a
 * low-passed noise burst is the impact and a brief triangle is the click of the
 * piece hitting the stack. Nothing here is loud enough to hurt.
 */
static const SynthNote HARD_DROP_NOTES[] = {
    NOTE(0.000f, 0.190f, 200.0f, 48.0f, 0.38f, 0.0015f, 0.070f, 0.35f, SYNTH_WAVE_SINE),
    NOTE(0.000f, 0.070f, 0.0f, 0.0f, 0.10f, 0.001f, 0.060f, 0.10f, SYNTH_WAVE_NOISE),
    NOTE(0.000f, 0.040f, 150.0f, 70.0f, 0.07f, 0.001f, 0.030f, 0.20f, SYNTH_WAVE_TRIANGLE),
};

/* A soft thump plus a tiny click: the piece settling, not an impact. */
static const SynthNote LOCK_NOTES[] = {
    NOTE(0.000f, 0.105f, 140.0f, 85.0f, 0.30f, 0.002f, 0.055f, 0.30f, SYNTH_WAVE_SINE),
    NOTE(0.000f, 0.022f, 320.0f, 180.0f, 0.06f, 0.001f, 0.015f, 0.00f, SYNTH_WAVE_TRIANGLE),
};

/*
 * Clear sounds are rising arpeggios from E5: one more note per line, each note
 * a little higher than the last, so the ear hears how good the clear was.
 */
static const SynthNote CLEAR_1_NOTES[] = {
    NOTE(0.000f, 0.140f, 659.0f, 659.0f, 0.17f, 0.004f, 0.070f, 0.50f, SYNTH_WAVE_TRIANGLE),
    NOTE(0.070f, 0.140f, 988.0f, 988.0f, 0.17f, 0.004f, 0.070f, 0.50f, SYNTH_WAVE_TRIANGLE),
};

static const SynthNote CLEAR_2_NOTES[] = {
    NOTE(0.000f, 0.140f, 659.0f, 659.0f, 0.17f, 0.004f, 0.070f, 0.50f, SYNTH_WAVE_TRIANGLE),
    NOTE(0.065f, 0.140f, 880.0f, 880.0f, 0.17f, 0.004f, 0.070f, 0.50f, SYNTH_WAVE_TRIANGLE),
    NOTE(0.130f, 0.140f, 1046.0f, 1046.0f, 0.17f, 0.004f, 0.070f, 0.50f, SYNTH_WAVE_TRIANGLE),
};

static const SynthNote CLEAR_3_NOTES[] = {
    NOTE(0.000f, 0.145f, 659.0f, 659.0f, 0.17f, 0.004f, 0.070f, 0.50f, SYNTH_WAVE_TRIANGLE),
    NOTE(0.060f, 0.145f, 880.0f, 880.0f, 0.17f, 0.004f, 0.070f, 0.50f, SYNTH_WAVE_TRIANGLE),
    NOTE(0.120f, 0.145f, 1046.0f, 1046.0f, 0.17f, 0.004f, 0.070f, 0.50f, SYNTH_WAVE_TRIANGLE),
    NOTE(0.180f, 0.145f, 1318.0f, 1318.0f, 0.17f, 0.004f, 0.070f, 0.50f, SYNTH_WAVE_TRIANGLE),
};

/*
 * A tetris gets the same idea with a soft square arpeggio and a C major chord
 * left ringing under it, which makes it the brightest sound in the game.
 */
static const SynthNote CLEAR_4_NOTES[] = {
    NOTE(0.000f, 0.150f, 523.0f, 523.0f, 0.130f, 0.004f, 0.080f, 0.45f, SYNTH_WAVE_SQUARE),
    NOTE(0.045f, 0.150f, 659.0f, 659.0f, 0.130f, 0.004f, 0.080f, 0.45f, SYNTH_WAVE_SQUARE),
    NOTE(0.090f, 0.150f, 784.0f, 784.0f, 0.130f, 0.004f, 0.080f, 0.45f, SYNTH_WAVE_SQUARE),
    NOTE(0.135f, 0.150f, 1046.0f, 1046.0f, 0.130f, 0.004f, 0.080f, 0.45f, SYNTH_WAVE_SQUARE),
    NOTE(0.180f, 0.150f, 1318.0f, 1318.0f, 0.112f, 0.004f, 0.080f, 0.45f, SYNTH_WAVE_SQUARE),
    NOTE(0.210f, 0.320f, 523.0f, 523.0f, 0.085f, 0.010f, 0.160f, 0.55f, SYNTH_WAVE_SINE),
    NOTE(0.210f, 0.320f, 784.0f, 784.0f, 0.085f, 0.010f, 0.160f, 0.55f, SYNTH_WAVE_SINE),
    NOTE(0.210f, 0.320f, 1046.0f, 1046.0f, 0.085f, 0.010f, 0.160f, 0.55f, SYNTH_WAVE_SINE),
};

/* Four rising notes then a fifth above them: a small fanfare, not a jingle. */
static const SynthNote LEVEL_UP_NOTES[] = {
    NOTE(0.000f, 0.160f, 523.0f, 523.0f, 0.16f, 0.006f, 0.080f, 0.55f, SYNTH_WAVE_SINE),
    NOTE(0.085f, 0.160f, 659.0f, 659.0f, 0.16f, 0.006f, 0.080f, 0.55f, SYNTH_WAVE_SINE),
    NOTE(0.170f, 0.160f, 784.0f, 784.0f, 0.16f, 0.006f, 0.080f, 0.55f, SYNTH_WAVE_SINE),
    NOTE(0.255f, 0.160f, 1046.0f, 1046.0f, 0.16f, 0.006f, 0.080f, 0.55f, SYNTH_WAVE_SINE),
    NOTE(0.340f, 0.200f, 1318.0f, 1318.0f, 0.15f, 0.006f, 0.110f, 0.55f, SYNTH_WAVE_SINE),
};

/*
 * The game-over phrase walks down G4, E4, C4 and G3 and leaves one low sine to
 * fade out. At a little over a second it is the longest sound, but still well
 * under the two-second bound.
 */
static const SynthNote GAME_OVER_NOTES[] = {
    NOTE(0.000f, 0.280f, 392.0f, 392.0f, 0.16f, 0.008f, 0.120f, 0.45f, SYNTH_WAVE_SINE),
    NOTE(0.150f, 0.280f, 330.0f, 330.0f, 0.16f, 0.008f, 0.120f, 0.45f, SYNTH_WAVE_SINE),
    NOTE(0.300f, 0.300f, 262.0f, 262.0f, 0.17f, 0.008f, 0.120f, 0.45f, SYNTH_WAVE_SINE),
    NOTE(0.460f, 0.300f, 196.0f, 196.0f, 0.17f, 0.008f, 0.120f, 0.45f, SYNTH_WAVE_SINE),
    NOTE(0.630f, 0.450f, 131.0f, 131.0f, 0.18f, 0.010f, 0.220f, 0.50f, SYNTH_WAVE_SINE),
};

/* One neutral two-tone blip for pause and for resume. */
static const SynthNote PAUSE_NOTES[] = {
    NOTE(0.000f, 0.070f, 659.0f, 880.0f, 0.13f, 0.004f, 0.040f, 0.55f, SYNTH_WAVE_SINE),
    NOTE(0.060f, 0.110f, 880.0f, 660.0f, 0.12f, 0.004f, 0.060f, 0.55f, SYNTH_WAVE_SINE),
};

/*
 * A finished run: a quick rising run up C major, then a held chord with a
 * bright fifth on top. It answers the game-over phrase, which walks down.
 */
static const SynthNote FINISH_NOTES[] = {
    NOTE(0.000f, 0.110f, 523.0f, 523.0f, 0.15f, 0.005f, 0.060f, 0.50f, SYNTH_WAVE_SQUARE),
    NOTE(0.080f, 0.110f, 659.0f, 659.0f, 0.15f, 0.005f, 0.060f, 0.50f, SYNTH_WAVE_SQUARE),
    NOTE(0.160f, 0.110f, 784.0f, 784.0f, 0.15f, 0.005f, 0.060f, 0.50f, SYNTH_WAVE_SQUARE),
    NOTE(0.240f, 0.700f, 1046.0f, 1046.0f, 0.14f, 0.008f, 0.350f, 0.55f, SYNTH_WAVE_SQUARE),
    NOTE(0.240f, 0.700f, 523.0f, 523.0f, 0.10f, 0.015f, 0.350f, 0.55f, SYNTH_WAVE_SINE),
    NOTE(0.240f, 0.700f, 659.0f, 659.0f, 0.10f, 0.015f, 0.350f, 0.55f, SYNTH_WAVE_SINE),
    NOTE(0.240f, 0.700f, 784.0f, 784.0f, 0.10f, 0.015f, 0.350f, 0.55f, SYNTH_WAVE_SINE),
    NOTE(0.320f, 0.620f, 1568.0f, 1568.0f, 0.06f, 0.010f, 0.300f, 0.60f, SYNTH_WAVE_SINE),
};

typedef struct SynthSound {
    const SynthNote *notes;
    int count;
    const char *name;
} SynthSound;

#define SOUND(name_) \
    { name_##_NOTES, (int)(sizeof name_##_NOTES / sizeof name_##_NOTES[0]), #name_ }

static const SynthSound SOUNDS[SOUND_COUNT] = {
    SOUND(MOVE),       SOUND(ROTATE),   SOUND(HOLD),      SOUND(HARD_DROP),
    SOUND(LOCK),       SOUND(CLEAR_1),  SOUND(CLEAR_2),   SOUND(CLEAR_3),
    SOUND(CLEAR_4),    SOUND(LEVEL_UP), SOUND(GAME_OVER), SOUND(PAUSE),
    SOUND(FINISH),
};

const char *synth_name(SoundId id)
{
    if ((unsigned)id >= (unsigned)SOUND_COUNT) {
        return "unknown";
    }
    return SOUNDS[id].name;
}

/* Length of the note list in seconds, without the tail. */
static float synth_sound_seconds(SoundId id)
{
    const SynthSound *sound = &SOUNDS[id];
    float end = 0.0f;
    for (int i = 0; i < sound->count; ++i) {
        const SynthNote *note = &sound->notes[i];
        const float note_end = note->start + note->duration;
        if (note_end > end) {
            end = note_end;
        }
    }
    return end;
}

int synth_length(SoundId id, int sample_rate)
{
    if (sample_rate <= 0 || (unsigned)id >= (unsigned)SOUND_COUNT) {
        return 0;
    }

    const float seconds = synth_sound_seconds(id);
    if (seconds <= 0.0f) {
        return 0;
    }

    int frames = (int)((seconds + SYNTH_TAIL_SECONDS) * (float)sample_rate);
    if (frames < 1) {
        frames = 1;
    }
    const int max_frames = (int)(SYNTH_MAX_SECONDS * (float)sample_rate);
    if (frames > max_frames) {
        frames = max_frames;
    }
    return frames;
}

/*
 * sin(2*pi*phase) without libm. Bhaskara I's approximation of sin(pi*u) on
 * [0,1] is 16u(1-u) / (5 - 4u(1-u)), accurate to about 2e-3 — plenty for a
 * short game sound, and it keeps the project free of -lm.
 */
static float synth_sine(float phase)
{
    while (phase >= 1.0f) {
        phase -= 1.0f;
    }
    while (phase < 0.0f) {
        phase += 1.0f;
    }

    float sign = 1.0f;
    if (phase >= 0.5f) {
        phase -= 0.5f;
        sign = -1.0f;
    }

    const float u = phase * 2.0f;
    const float q = u * (1.0f - u);
    return sign * (16.0f * q) / (5.0f - 4.0f * q);
}

/* Starts at -1, peaks at +1 in the middle and returns to -1. */
static float synth_triangle(float phase)
{
    while (phase >= 1.0f) {
        phase -= 1.0f;
    }
    while (phase < 0.0f) {
        phase += 1.0f;
    }

    if (phase < 0.5f) {
        return 4.0f * phase - 1.0f;
    }
    return 3.0f - 4.0f * phase;
}

/*
 * A soft square: three odd harmonics rather than a step, so it is bright
 * without the buzz of a true square. The sum peaks at 0.867, normalised to
 * roughly full scale.
 */
static float synth_square(float phase)
{
    const float value = synth_sine(phase) + synth_sine(3.0f * phase) / 3.0f +
                        synth_sine(5.0f * phase) / 5.0f;
    return value * 1.15f;
}

/* xorshift32 in [-1, 1); the state must never be zero. */
static float synth_noise(uint32_t *state)
{
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return (float)(x >> 8) * (2.0f / 16777216.0f) - 1.0f;
}

/* `source` is the SoundId, or SOUND_COUNT for the music. */
static uint32_t synth_noise_seed(int source, int note_index)
{
    const uint32_t seed = 0x9E3779B9u * ((uint32_t)source + 1u) +
                          0x85EBCA6Bu * ((uint32_t)note_index + 1u) + 0x632BE59Bu;
    return seed != 0 ? seed : 1u;
}

/*
 * Linear attack / release. The release is measured backwards from the note's
 * end, so the envelope is zero on the first and on the last sample and two
 * overlapping notes cannot click at their joins.
 */
static float synth_envelope(const SynthNote *note, float elapsed)
{
    float envelope = 1.0f;
    if (note->attack > 0.0f && elapsed < note->attack) {
        envelope = elapsed / note->attack;
    }

    const float remaining = note->duration - elapsed;
    if (note->release > 0.0f && remaining < note->release) {
        const float fall = remaining / note->release;
        if (fall < envelope) {
            envelope = fall;
        }
    }

    return envelope < 0.0f ? 0.0f : envelope;
}

/*
 * Renders one note and adds it to out. `written` is how much of the sound fits
 * in the caller's buffer; a note past that point is simply cut, which is what
 * makes a short buffer a true prefix of the full render.
 *
 * With `wrap` the buffer is a loop instead: the part of a note that runs past
 * the end is added at the start, exactly where it would sound when the loop
 * comes round again, so the end of the buffer joins its start without a seam.
 */
static void synth_add_note(const SynthNote *note, int note_index, int source, int sample_rate,
                           float *out, int written, bool wrap)
{
    int start = (int)(note->start * (float)sample_rate);
    int count = (int)(note->duration * (float)sample_rate);
    if (count < 1) {
        count = 1;
    }
    if (start < 0) {
        start = 0;
    }
    if (start >= written) {
        return;
    }

    int end = start + count;
    if (end > written && !wrap) {
        end = written;
    }

    float phase = 0.0f;
    float filtered = 0.0f;
    uint32_t noise_state = synth_noise_seed(source, note_index);

    for (int i = start; i < end; ++i) {
        const int index = i - start;
        const float progress = (float)index / (float)count;
        const float elapsed = (float)index / (float)sample_rate;
        const float hz = note->hz_from + (note->hz_to - note->hz_from) * progress;

        float sample;
        switch (note->wave) {
        case SYNTH_WAVE_SINE:
            sample = synth_sine(phase);
            break;
        case SYNTH_WAVE_TRIANGLE:
            sample = synth_triangle(phase);
            break;
        case SYNTH_WAVE_SQUARE:
            sample = synth_square(phase);
            break;
        case SYNTH_WAVE_NOISE:
        default:
            sample = synth_noise(&noise_state);
            break;
        }

        if (note->lowpass > 0.0f) {
            filtered += note->lowpass * (sample - filtered);
            sample = filtered;
        } else {
            filtered = sample;
        }

        out[wrap ? i % written : i] += sample * synth_envelope(note, elapsed) * note->volume;

        phase += hz / (float)sample_rate;
        if (phase >= 1.0f) {
            phase -= 1.0f;
        }
    }
}

/*
 * Fades the first and last couple of milliseconds to zero. The envelope already
 * does this per note; this is a safety net for the whole buffer, and it
 * guarantees that even a truncated render starts at exactly zero.
 */
static void synth_fade_edges(float *out, int written, int full_length, int sample_rate)
{
    int fade = (int)(SYNTH_EDGE_SECONDS * (float)sample_rate);
    if (fade < 1) {
        fade = 1;
    }
    if (fade * 2 > full_length) {
        fade = full_length / 2;
    }
    if (fade < 1) {
        return;
    }

    for (int i = 0; i < written; ++i) {
        if (i < fade) {
            out[i] *= (float)i / (float)fade;
        }
        const int from_end = full_length - 1 - i;
        if (from_end >= 0 && from_end < fade) {
            out[i] *= (float)from_end / (float)fade;
        }
    }
}

int synth_render(SoundId id, int sample_rate, float *out, int capacity)
{
    if (out == NULL || capacity <= 0) {
        return 0;
    }

    const int length = synth_length(id, sample_rate);
    if (length <= 0) {
        return 0;
    }

    const int written = capacity < length ? capacity : length;
    for (int i = 0; i < written; ++i) {
        out[i] = 0.0f;
    }

    const SynthSound *sound = &SOUNDS[id];
    for (int i = 0; i < sound->count; ++i) {
        synth_add_note(&sound->notes[i], i, (int)id, sample_rate, out, written, false);
    }

    synth_fade_edges(out, written, length, sample_rate);
    return written;
}

/* --- music ---------------------------------------------------------------- */

/*
 * The loop is generated from a chord per bar and a melody table rather than
 * listed note by note: the bass, pad, arpeggio and drums follow the chords, so
 * only the melody is written out. Frequencies are equal temperament, A4 = 440.
 */
#define MUSIC_BARS 8
#define MUSIC_BEAT_SECONDS 0.5f /* 120 beats per minute */
#define MUSIC_BAR_SECONDS (4.0f * MUSIC_BEAT_SECONDS)

typedef struct MusicChord {
    float bass;   /* root, in the octave a small speaker can still carry */
    float pad[3]; /* the triad around middle C */
    float arp[4]; /* chord tones an octave or two up, for the arpeggio */
} MusicChord;

#define MUSIC_AM { 110.00f, { 220.00f, 261.63f, 329.63f }, { 440.00f, 523.25f, 659.26f, 880.00f } }
#define MUSIC_F { 87.31f, { 174.61f, 220.00f, 261.63f }, { 349.23f, 440.00f, 523.25f, 698.46f } }

static const MusicChord MUSIC_CHORDS[MUSIC_BARS] = {
    MUSIC_AM,
    MUSIC_F,
    /* C */ { 130.81f, { 196.00f, 261.63f, 329.63f }, { 392.00f, 523.25f, 659.26f, 783.99f } },
    /* G */ { 98.00f, { 196.00f, 246.94f, 293.66f }, { 392.00f, 493.88f, 587.33f, 783.99f } },
    MUSIC_AM,
    MUSIC_F,
    /* Dm */ { 146.83f, { 174.61f, 220.00f, 293.66f }, { 349.23f, 440.00f, 587.33f, 698.46f } },
    /* E, the major dominant that leads back to Am at the top of the loop */
    { 82.41f, { 164.81f, 207.65f, 246.94f }, { 329.63f, 415.30f, 493.88f, 659.26f } },
};

typedef struct MusicMelodyNote {
    int bar;
    float beat;  /* start within the bar */
    float beats; /* length */
    float hz;
} MusicMelodyNote;

/* A call in bars 1-4 and an answer in bars 5-8 that turns towards E. */
static const MusicMelodyNote MUSIC_MELODY[] = {
    { 0, 0.0f, 1.0f, 659.26f }, { 0, 1.0f, 0.5f, 783.99f }, { 0, 1.5f, 0.5f, 659.26f },
    { 0, 2.0f, 1.0f, 587.33f }, { 0, 3.0f, 1.0f, 523.25f },
    { 1, 0.0f, 2.0f, 698.46f }, { 1, 2.0f, 1.0f, 659.26f }, { 1, 3.0f, 1.0f, 523.25f },
    { 2, 0.0f, 1.0f, 783.99f }, { 2, 1.0f, 0.5f, 659.26f }, { 2, 1.5f, 0.5f, 783.99f },
    { 2, 2.0f, 1.0f, 1046.50f }, { 2, 3.0f, 1.0f, 783.99f },
    { 3, 0.0f, 3.0f, 587.33f }, { 3, 3.0f, 1.0f, 493.88f },
    { 4, 0.0f, 1.0f, 659.26f }, { 4, 1.0f, 0.5f, 783.99f }, { 4, 1.5f, 0.5f, 659.26f },
    { 4, 2.0f, 1.0f, 587.33f }, { 4, 3.0f, 1.0f, 523.25f },
    { 5, 0.0f, 2.0f, 880.00f }, { 5, 2.0f, 1.0f, 783.99f }, { 5, 3.0f, 1.0f, 698.46f },
    { 6, 0.0f, 1.0f, 698.46f }, { 6, 1.0f, 0.5f, 659.26f }, { 6, 1.5f, 0.5f, 587.33f },
    { 6, 2.0f, 1.0f, 659.26f }, { 6, 3.0f, 1.0f, 698.46f },
    { 7, 0.0f, 2.0f, 659.26f }, { 7, 2.0f, 1.0f, 493.88f }, { 7, 3.0f, 1.0f, 415.30f },
};

/* Arpeggio order over the four chord tones, one step per sixteenth. */
static const int MUSIC_ARP_STEPS[8] = { 0, 1, 2, 3, 2, 1, 2, 3 };

int synth_music_length(int sample_rate)
{
    if (sample_rate <= 0) {
        return 0;
    }
    return (int)(SYNTH_MUSIC_SECONDS * (float)sample_rate);
}

/* Adds one generated note to the loop; `index` numbers the notes for the noise seeds. */
static void music_add(SynthNote note, int *index, int sample_rate, float *out, int length)
{
    synth_add_note(&note, *index, (int)SOUND_COUNT, sample_rate, out, length, true);
    ++*index;
}

int synth_music_render(int sample_rate, float *out, int capacity)
{
    const int length = synth_music_length(sample_rate);
    if (out == NULL || length <= 0 || capacity < length) {
        return 0;
    }
    for (int i = 0; i < length; ++i) {
        out[i] = 0.0f;
    }

    int index = 0;
    for (int bar = 0; bar < MUSIC_BARS; ++bar) {
        const MusicChord *chord = &MUSIC_CHORDS[bar];
        const float bar_start = (float)bar * MUSIC_BAR_SECONDS;

        /* Pad: the triad held through the bar and a little into the next, so
           the chords overlap instead of stopping between bars. */
        for (int tone = 0; tone < 3; ++tone) {
            const SynthNote pad = NOTE(bar_start, MUSIC_BAR_SECONDS + 0.25f, chord->pad[tone],
                                       chord->pad[tone], 0.035f, 0.12f, 0.30f, 0.25f,
                                       SYNTH_WAVE_SINE);
            music_add(pad, &index, sample_rate, out, length);
        }

        /* Bass: eighth notes jumping between the root and its octave. */
        for (int step = 0; step < 8; ++step) {
            const float hz = step % 2 == 0 ? chord->bass : chord->bass * 2.0f;
            const float volume = step % 2 == 0 ? 0.13f : 0.09f;
            const SynthNote bass = NOTE(bar_start + (float)step * 0.25f, 0.22f, hz, hz, volume,
                                        0.005f, 0.08f, 0.35f, SYNTH_WAVE_TRIANGLE);
            music_add(bass, &index, sample_rate, out, length);
        }

        /* Arpeggio: quiet sixteenths over the chord tones. */
        for (int step = 0; step < 16; ++step) {
            const float hz = chord->arp[MUSIC_ARP_STEPS[step % 8]];
            const SynthNote arp = NOTE(bar_start + (float)step * 0.125f, 0.11f, hz, hz, 0.026f,
                                       0.004f, 0.07f, 0.45f, SYNTH_WAVE_SQUARE);
            music_add(arp, &index, sample_rate, out, length);
        }

        /* Drums: a soft kick on beats 1 and 3, a brushed snare on 2 and 4,
           and a closed hat on every off-beat. */
        for (int beat = 0; beat < 4; ++beat) {
            const float t = bar_start + (float)beat * MUSIC_BEAT_SECONDS;
            if (beat % 2 == 0) {
                const SynthNote kick = NOTE(t, 0.16f, 120.0f, 45.0f, 0.20f, 0.002f, 0.12f, 0.40f,
                                            SYNTH_WAVE_SINE);
                music_add(kick, &index, sample_rate, out, length);
            } else {
                const SynthNote snare = NOTE(t, 0.10f, 0.0f, 0.0f, 0.045f, 0.002f, 0.08f, 0.55f,
                                             SYNTH_WAVE_NOISE);
                music_add(snare, &index, sample_rate, out, length);
            }
            const SynthNote hat = NOTE(t + 0.25f, 0.035f, 0.0f, 0.0f, 0.02f, 0.001f, 0.03f, 0.0f,
                                       SYNTH_WAVE_NOISE);
            music_add(hat, &index, sample_rate, out, length);
        }
    }

    /* Lead: each note a touch shorter than its slot, so repeated pitches
       separate. */
    for (size_t i = 0; i < sizeof MUSIC_MELODY / sizeof MUSIC_MELODY[0]; ++i) {
        const MusicMelodyNote *melody = &MUSIC_MELODY[i];
        const float start = (float)melody->bar * MUSIC_BAR_SECONDS +
                            melody->beat * MUSIC_BEAT_SECONDS;
        const float duration = melody->beats * MUSIC_BEAT_SECONDS - 0.03f;
        const float release = duration * 0.5f < 0.12f ? duration * 0.5f : 0.12f;
        const SynthNote lead = NOTE(start, duration, melody->hz, melody->hz, 0.075f, 0.01f,
                                    release, 0.50f, SYNTH_WAVE_SQUARE);
        music_add(lead, &index, sample_rate, out, length);
    }

    return length;
}
