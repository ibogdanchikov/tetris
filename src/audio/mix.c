#include "audio/mix.h"

#include <stddef.h>

bool mix_voice_active(const MixVoice *voice)
{
    return voice->samples != NULL && voice->length > 0 && voice->position < voice->length;
}

/* Retires a voice that ran out of samples so its slot can be reused. */
static void mix_voice_release(MixVoice *voice)
{
    voice->samples = NULL;
    voice->length = 0;
    voice->position = 0;
    voice->gain = 0.0f;
}

int mix_voices(MixVoice *voices, int voice_count, float master_from, float master_to, float *out,
               int frames)
{
    if (voices == NULL || voice_count <= 0) {
        return 0;
    }
    if (voice_count > MIX_MAX_VOICES) {
        voice_count = MIX_MAX_VOICES;
    }

    if (out != NULL && frames > 0) {
        for (int i = 0; i < frames; ++i) {
            out[i] = 0.0f;
        }

        const float master_step = (master_to - master_from) / (float)frames;
        for (int v = 0; v < voice_count; ++v) {
            MixVoice *voice = &voices[v];
            if (!mix_voice_active(voice)) {
                if (voice->samples != NULL) {
                    mix_voice_release(voice);
                }
                continue;
            }

            const float *samples = voice->samples;
            const int length = voice->length;
            int index = voice->position;

            for (int i = 0; i < frames && index < length; ++i, ++index) {
                /* +1 so the block ends exactly on master_to. */
                const float master = master_from + master_step * (float)(i + 1);
                out[i] += samples[index] * voice->gain * master;
            }

            voice->position = index;
            if (index >= length) {
                mix_voice_release(voice);
            }
        }
    }

    int active = 0;
    for (int v = 0; v < voice_count; ++v) {
        if (mix_voice_active(&voices[v])) {
            ++active;
        }
    }
    return active;
}

void mix_add_loop(MixLoop *loop, float gain_from, float gain_to, float *out, int frames)
{
    if (loop == NULL || loop->samples == NULL || loop->length <= 0 || out == NULL ||
        frames <= 0) {
        return;
    }
    if (loop->position < 0 || loop->position >= loop->length) {
        loop->position = 0;
    }

    int position = loop->position;
    if (gain_from != 0.0f || gain_to != 0.0f) {
        const float step = (gain_to - gain_from) / (float)frames;
        for (int i = 0; i < frames; ++i) {
            /* +1 so the block ends exactly on gain_to, like the master ramp. */
            out[i] += loop->samples[position] * (gain_from + step * (float)(i + 1));
            if (++position == loop->length) {
                position = 0;
            }
        }
    } else {
        position = (int)(((long long)position + frames) % loop->length);
    }
    loop->position = position;
}

float mix_soft_clip(float sample)
{
    const float magnitude = sample < 0.0f ? -sample : sample;

    /*
     * The curve already stays inside (-1, 1); this also turns an infinity or a
     * NaN into a full-scale or silent sample instead of propagating it.
     */
    if (!(magnitude < 1000000.0f)) {
        if (sample > 0.0f) {
            return 1.0f;
        }
        return sample < 0.0f ? -1.0f : 0.0f;
    }

    return sample / (1.0f + magnitude);
}

void mix_clip(float *out, int frames)
{
    if (out == NULL) {
        return;
    }
    for (int i = 0; i < frames; ++i) {
        out[i] = mix_soft_clip(out[i]);
    }
}
