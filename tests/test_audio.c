/*
 * Audio suites: synthesis, voice mixing and the event-to-sound mapping. All
 * three modules are SDL-free, so this runs without a device or a window.
 */

#include <string.h>

#include "app/sound_map.h"
#include "audio/mix.h"
#include "audio/synth.h"
#include "core/game.h"
#include "test_harness.h"

/* Two seconds at 48 kHz plus slack: every synth test uses this one buffer. */
#define TEST_AUDIO_FRAMES (48000 * 2 + 64)

static float audio_buffer[TEST_AUDIO_FRAMES];
static float audio_compare[TEST_AUDIO_FRAMES];

static bool audio_finite(float value)
{
    return value == value && value < 1.0e9f && value > -1.0e9f;
}

static bool audio_near_zero(float value, float epsilon)
{
    return value <= epsilon && value >= -epsilon;
}

/* Peak absolute sample in a rendered buffer. */
static float audio_peak(const float *samples, int frames)
{
    float peak = 0.0f;
    for (int i = 0; i < frames; ++i) {
        const float magnitude = samples[i] < 0.0f ? -samples[i] : samples[i];
        if (magnitude > peak) {
            peak = magnitude;
        }
    }
    return peak;
}

static void test_synth_lengths(void)
{
    static const int rates[] = { 48000, 44100 };

    for (int id = 0; id < SOUND_COUNT; ++id) {
        CHECK(strcmp(synth_name((SoundId)id), "unknown") != 0);

        for (int r = 0; r < 2; ++r) {
            const int rate = rates[r];
            const int length = synth_length((SoundId)id, rate);
            CHECK(length > 0);
            CHECK(length <= 2 * rate);
            CHECK(length <= TEST_AUDIO_FRAMES);
        }
    }

    /* Unknown ids and silly rates are refused rather than guessed. */
    CHECK_EQ_INT(synth_length(SOUND_COUNT, 48000), 0);
    CHECK_EQ_INT(synth_length((SoundId)-1, 48000), 0);
    CHECK_EQ_INT(synth_length(SOUND_MOVE, 0), 0);
    CHECK_EQ_INT(synth_length(SOUND_MOVE, -48000), 0);
}

static void test_synth_waveforms(void)
{
    static const int rates[] = { 48000, 44100 };

    for (int id = 0; id < SOUND_COUNT; ++id) {
        for (int r = 0; r < 2; ++r) {
            const int rate = rates[r];
            const int length = synth_length((SoundId)id, rate);
            const float duration = (float)length / (float)rate;

            const int written = synth_render((SoundId)id, rate, audio_buffer, TEST_AUDIO_FRAMES);
            CHECK_EQ_INT(written, length);
            CHECK(duration <= SYNTH_MAX_SECONDS);

            int non_finite = 0;
            for (int i = 0; i < written; ++i) {
                if (!audio_finite(audio_buffer[i])) {
                    ++non_finite;
                }
            }
            CHECK_EQ_INT(non_finite, 0);

            const float peak = audio_peak(audio_buffer, written);
            CHECK(peak > 0.01f); /* the sound is not silence */
            CHECK(peak <= SYNTH_MAX_PEAK + 0.001f);

            /* No click: both ends start and stop at (near) zero. */
            for (int i = 0; i < 4 && i < written; ++i) {
                CHECK(audio_near_zero(audio_buffer[i], 0.01f));
                CHECK(audio_near_zero(audio_buffer[written - 1 - i], 0.01f));
            }
        }
    }
}

static void test_synth_deterministic(void)
{
    const int length = synth_length(SOUND_GAME_OVER, 48000);
    CHECK(length > 0);

    const int first = synth_render(SOUND_GAME_OVER, 48000, audio_buffer, TEST_AUDIO_FRAMES);
    memset(audio_compare, 0x5A, (size_t)length * sizeof(float));
    const int second = synth_render(SOUND_GAME_OVER, 48000, audio_compare, TEST_AUDIO_FRAMES);

    CHECK_EQ_INT(first, length);
    CHECK_EQ_INT(second, length);
    CHECK(memcmp(audio_buffer, audio_compare, (size_t)length * sizeof(float)) == 0);
}

static void test_synth_short_capacity(void)
{
    const int length = synth_length(SOUND_CLEAR_4, 48000);
    const int capacity = length / 3;
    CHECK(capacity > 0);

    const int full = synth_render(SOUND_CLEAR_4, 48000, audio_compare, TEST_AUDIO_FRAMES);
    CHECK_EQ_INT(full, length);

    /* Poison the buffer so a write past the capacity would show up. */
    const float poison = 12345.0f;
    for (int i = 0; i < TEST_AUDIO_FRAMES; ++i) {
        audio_buffer[i] = poison;
    }

    const int written = synth_render(SOUND_CLEAR_4, 48000, audio_buffer, capacity);
    CHECK_EQ_INT(written, capacity);
    CHECK(audio_buffer[capacity] == poison);
    CHECK(memcmp(audio_buffer, audio_compare, (size_t)capacity * sizeof(float)) == 0);

    /* Nothing is written for an empty or absent buffer. */
    CHECK_EQ_INT(synth_render(SOUND_CLEAR_4, 48000, audio_buffer, 0), 0);
    CHECK_EQ_INT(synth_render(SOUND_CLEAR_4, 48000, NULL, 100), 0);
    CHECK_EQ_INT(synth_render(SOUND_COUNT, 48000, audio_buffer, 100), 0);
}

static bool mix_voice_is_clear(const MixVoice *voice)
{
    return voice->samples == NULL && voice->length == 0 && voice->position == 0;
}

static void test_mix_silence(void)
{
    MixVoice voices[MIX_MAX_VOICES];
    memset(voices, 0, sizeof voices);

    float out[16];
    for (int i = 0; i < 16; ++i) {
        out[i] = 1.0f;
    }

    CHECK_EQ_INT(mix_voices(voices, MIX_MAX_VOICES, 1.0f, 1.0f, out, 16), 0);
    for (int i = 0; i < 16; ++i) {
        CHECK(out[i] == 0.0f);
    }
}

static void test_mix_single_voice(void)
{
    static const float samples[5] = { 0.1f, -0.2f, 0.3f, -0.4f, 0.5f };
    MixVoice voices[1] = { { samples, 5, 0, 1.0f } };

    float out[4] = { 99.0f, 99.0f, 99.0f, 99.0f };
    CHECK_EQ_INT(mix_voices(voices, 1, 1.0f, 1.0f, out, 3), 1);
    CHECK(out[0] == samples[0]);
    CHECK(out[1] == samples[1]);
    CHECK(out[2] == samples[2]);
    CHECK_EQ_INT(voices[0].position, 3);

    /* The tail finishes inside the next block; the rest of it is silence. */
    CHECK_EQ_INT(mix_voices(voices, 1, 1.0f, 1.0f, out, 4), 0);
    CHECK(out[0] == samples[3]);
    CHECK(out[1] == samples[4]);
    CHECK(out[2] == 0.0f);
    CHECK(out[3] == 0.0f);
    CHECK(mix_voice_is_clear(&voices[0]));
}

static void test_mix_two_voices_and_gain(void)
{
    static const float a[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    static const float b[2] = { 0.5f, -0.5f };
    MixVoice voices[2] = {
        { a, 4, 0, 0.5f },
        { b, 2, 0, 1.0f },
    };

    float out[4];
    /* The short voice ends mid-block and is dropped; the long one finishes
       exactly on the last sample and is dropped too. */
    CHECK_EQ_INT(mix_voices(voices, 2, 1.0f, 1.0f, out, 4), 0);
    CHECK(out[0] == 1.0f); /* 0.5 * 1 + 0.5 * 1 */
    CHECK(out[1] == 0.0f); /* 0.5 * 1 - 0.5 * 1 */
    CHECK(out[2] == 0.5f);
    CHECK(out[3] == 0.5f);
    CHECK(mix_voice_is_clear(&voices[0]));
    CHECK(mix_voice_is_clear(&voices[1]));
}

static void test_mix_master_ramp(void)
{
    static const float ones[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    MixVoice voices[1] = { { ones, 4, 0, 1.0f } };

    float out[4];
    CHECK_EQ_INT(mix_voices(voices, 1, 0.0f, 1.0f, out, 4), 0);
    /* The ramp reaches master_to on the last sample of the block. */
    CHECK(audio_near_zero(out[0] - 0.25f, 0.0001f));
    CHECK(audio_near_zero(out[1] - 0.50f, 0.0001f));
    CHECK(audio_near_zero(out[2] - 0.75f, 0.0001f));
    CHECK(audio_near_zero(out[3] - 1.00f, 0.0001f));
}

static void test_mix_soft_clip(void)
{
    CHECK(mix_soft_clip(0.0f) == 0.0f);
    CHECK(audio_near_zero(mix_soft_clip(0.25f) - 0.2f, 0.0001f));

    /* Odd symmetry, monotonic, and linear-ish at the levels the game uses. */
    CHECK(audio_near_zero(mix_soft_clip(-0.5f) + mix_soft_clip(0.5f), 0.0001f));
    CHECK(mix_soft_clip(1.0f) > mix_soft_clip(0.5f));
    CHECK(mix_soft_clip(0.5f) > mix_soft_clip(0.25f));
    CHECK(audio_near_zero(mix_soft_clip(0.01f) - 0.0099f, 0.0002f));

    /* Never past full scale, however hard the mix is pushed. */
    static const float loud[] = { 0.5f, 1.0f, 4.0f, 100.0f, 1000000.0f, 1.0e30f };
    for (int i = 0; i < 6; ++i) {
        CHECK(mix_soft_clip(loud[i]) <= 1.0f);
        CHECK(mix_soft_clip(-loud[i]) >= -1.0f);
        CHECK(mix_soft_clip(loud[i]) > 0.0f);
    }

    float buffer[4] = { 0.0f, 0.5f, -0.5f, 2.0f };
    mix_clip(buffer, 4);
    CHECK(buffer[0] == 0.0f);
    CHECK(audio_near_zero(buffer[1] - 1.0f / 3.0f, 0.0001f));
    CHECK(audio_near_zero(buffer[2] + 1.0f / 3.0f, 0.0001f));
    CHECK(audio_near_zero(buffer[3] - 2.0f / 3.0f, 0.0001f));
}

/* Maps events with a game whose last lock cleared `lines` rows. */
static int sound_map_ids(uint32_t events, int lines, SoundId ids[], int max)
{
    GameState game;
    memset(&game, 0, sizeof game);
    game.last_clear_count = (uint8_t)lines;

    SoundRequest requests[SOUND_MAP_MAX_REQUESTS];
    const int count = sound_map_events(events, &game, requests, max);
    for (int i = 0; i < count; ++i) {
        ids[i] = requests[i].id;
        CHECK(requests[i].gain > 0.0f);
        CHECK(requests[i].gain <= 1.0f);
    }
    return count;
}

static void test_sound_map_precedence(void)
{
    SoundId ids[SOUND_MAP_MAX_REQUESTS];

    CHECK_EQ_INT(sound_map_ids(0, 0, ids, SOUND_MAP_MAX_REQUESTS), 0);

    /* A plain lock is a click; a clear replaces it. */
    CHECK_EQ_INT(sound_map_ids(GAME_EVENT_LOCK, 0, ids, SOUND_MAP_MAX_REQUESTS), 1);
    CHECK_EQ_INT(ids[0], SOUND_LOCK);

    CHECK_EQ_INT(sound_map_ids(GAME_EVENT_LOCK | GAME_EVENT_LINE_CLEAR, 2, ids,
                               SOUND_MAP_MAX_REQUESTS),
                 1);
    CHECK_EQ_INT(ids[0], SOUND_CLEAR_2);

    /* One arpeggio per line count, tetris and beyond using the bright one. */
    CHECK_EQ_INT(sound_map_ids(GAME_EVENT_LINE_CLEAR, 1, ids, SOUND_MAP_MAX_REQUESTS), 1);
    CHECK_EQ_INT(ids[0], SOUND_CLEAR_1);
    CHECK_EQ_INT(sound_map_ids(GAME_EVENT_LINE_CLEAR, 2, ids, SOUND_MAP_MAX_REQUESTS), 1);
    CHECK_EQ_INT(ids[0], SOUND_CLEAR_2);
    CHECK_EQ_INT(sound_map_ids(GAME_EVENT_LINE_CLEAR, 3, ids, SOUND_MAP_MAX_REQUESTS), 1);
    CHECK_EQ_INT(ids[0], SOUND_CLEAR_3);
    CHECK_EQ_INT(sound_map_ids(GAME_EVENT_LINE_CLEAR, 4, ids, SOUND_MAP_MAX_REQUESTS), 1);
    CHECK_EQ_INT(ids[0], SOUND_CLEAR_4);
    CHECK_EQ_INT(sound_map_ids(GAME_EVENT_LINE_CLEAR, 9, ids, SOUND_MAP_MAX_REQUESTS), 1);
    CHECK_EQ_INT(ids[0], SOUND_CLEAR_4);

    /* A hard drop locks in the same tick: thud only, never a lock click. */
    CHECK_EQ_INT(sound_map_ids(GAME_EVENT_HARD_DROP | GAME_EVENT_LOCK, 0, ids,
                               SOUND_MAP_MAX_REQUESTS),
                 1);
    CHECK_EQ_INT(ids[0], SOUND_HARD_DROP);

    /* A hard drop that clears plays the thud and then the clear. */
    CHECK_EQ_INT(sound_map_ids(GAME_EVENT_HARD_DROP | GAME_EVENT_LOCK | GAME_EVENT_LINE_CLEAR, 1,
                               ids, SOUND_MAP_MAX_REQUESTS),
                 2);
    CHECK_EQ_INT(ids[0], SOUND_HARD_DROP);
    CHECK_EQ_INT(ids[1], SOUND_CLEAR_1);

    /* Level up is added, not replacing anything. */
    CHECK_EQ_INT(sound_map_ids(GAME_EVENT_LOCK | GAME_EVENT_LINE_CLEAR | GAME_EVENT_LEVEL_UP, 3,
                               ids, SOUND_MAP_MAX_REQUESTS),
                 2);
    CHECK_EQ_INT(ids[0], SOUND_CLEAR_3);
    CHECK_EQ_INT(ids[1], SOUND_LEVEL_UP);

    /* Game over keeps its phrase clean, but a thud that caused it still plays. */
    CHECK_EQ_INT(sound_map_ids(GAME_EVENT_GAME_OVER | GAME_EVENT_LOCK, 0, ids,
                               SOUND_MAP_MAX_REQUESTS),
                 1);
    CHECK_EQ_INT(ids[0], SOUND_GAME_OVER);
    CHECK_EQ_INT(sound_map_ids(GAME_EVENT_GAME_OVER | GAME_EVENT_LINE_CLEAR, 4, ids,
                               SOUND_MAP_MAX_REQUESTS),
                 1);
    CHECK_EQ_INT(ids[0], SOUND_GAME_OVER);
    CHECK_EQ_INT(sound_map_ids(GAME_EVENT_HARD_DROP | GAME_EVENT_GAME_OVER | GAME_EVENT_LOCK, 0,
                               ids, SOUND_MAP_MAX_REQUESTS),
                 2);
    CHECK_EQ_INT(ids[0], SOUND_HARD_DROP);
    CHECK_EQ_INT(ids[1], SOUND_GAME_OVER);

    /* Ordinary actions can happen together and each keeps its own sound. */
    CHECK_EQ_INT(sound_map_ids(GAME_EVENT_MOVE | GAME_EVENT_ROTATE | GAME_EVENT_HOLD, 0, ids,
                               SOUND_MAP_MAX_REQUESTS),
                 3);
    CHECK_EQ_INT(ids[0], SOUND_MOVE);
    CHECK_EQ_INT(ids[1], SOUND_ROTATE);
    CHECK_EQ_INT(ids[2], SOUND_HOLD);
}

static void test_sound_map_limits(void)
{
    SoundId ids[SOUND_MAP_MAX_REQUESTS];

    /* A small buffer is filled to its limit, not overrun. */
    CHECK_EQ_INT(sound_map_ids(GAME_EVENT_HARD_DROP | GAME_EVENT_LINE_CLEAR | GAME_EVENT_LOCK |
                                   GAME_EVENT_LEVEL_UP | GAME_EVENT_MOVE,
                               1, ids, 2),
                 2);
    CHECK_EQ_INT(ids[0], SOUND_HARD_DROP);
    CHECK_EQ_INT(ids[1], SOUND_CLEAR_1);

    /* The worst case still fits the documented request capacity. */
    const uint32_t everything = GAME_EVENT_HARD_DROP | GAME_EVENT_LINE_CLEAR | GAME_EVENT_LOCK |
                                GAME_EVENT_LEVEL_UP | GAME_EVENT_MOVE | GAME_EVENT_ROTATE |
                                GAME_EVENT_HOLD;
    CHECK_EQ_INT(sound_map_ids(everything, 4, ids, SOUND_MAP_MAX_REQUESTS), 6);
    CHECK_EQ_INT(sound_map_ids(everything, 4, ids, 0), 0);
    CHECK_EQ_INT(sound_map_events(everything, NULL, NULL, SOUND_MAP_MAX_REQUESTS), 0);
}

void test_audio_suite(void)
{
    test_synth_lengths();
    test_synth_waveforms();
    test_synth_deterministic();
    test_synth_short_capacity();
    test_mix_silence();
    test_mix_single_voice();
    test_mix_two_voices_and_gain();
    test_mix_master_ramp();
    test_mix_soft_clip();
    test_sound_map_precedence();
    test_sound_map_limits();
}
