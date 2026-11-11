#include "app/sound_map.h"

#include <stddef.h>

/*
 * Balance between effects. The synth levels are already conservative and the
 * mixer soft-clips, so these only have to make the quiet actions quiet and keep
 * everything else at its designed level.
 */
#define SOUND_GAIN_MOVE 0.55f
#define SOUND_GAIN_ROTATE 0.70f
#define SOUND_GAIN_HOLD 0.70f
#define SOUND_GAIN_DROP 1.00f
#define SOUND_GAIN_LOCK 0.90f
#define SOUND_GAIN_CLEAR 1.00f
#define SOUND_GAIN_LEVEL_UP 0.85f
#define SOUND_GAIN_GAME_OVER 1.00f
#define SOUND_GAIN_PAUSE 0.90f

typedef struct SoundMapper {
    SoundRequest *out;
    int max;
    int count;
} SoundMapper;

/*
 * GAME_EVENT_SOFT_DROP_ROW and GAME_EVENT_SPAWN have no sound on purpose: soft
 * drop fires once per row and would buzz, and a spawn is silent by design. KICK
 * is folded into the rotate sound and CLEAR_DONE into the clear sound, which
 * already played at the lock.
 */

static void sound_map_push(SoundMapper *mapper, SoundId id, float gain)
{
    if (mapper->count >= mapper->max) {
        return;
    }
    mapper->out[mapper->count].id = id;
    mapper->out[mapper->count].gain = gain;
    ++mapper->count;
}

/* The clear sound for a line count: one note per line, four or more is a tetris. */
static SoundId sound_map_clear_id(int lines)
{
    if (lines <= 1) {
        return SOUND_CLEAR_1;
    }
    if (lines == 2) {
        return SOUND_CLEAR_2;
    }
    if (lines == 3) {
        return SOUND_CLEAR_3;
    }
    return SOUND_CLEAR_4;
}

int sound_map_events(uint32_t events, const GameState *game, SoundRequest out[], int max)
{
    if (out == NULL || max <= 0) {
        return 0;
    }

    SoundMapper mapper = { out, max, 0 };
    const bool game_over = (events & GAME_EVENT_GAME_OVER) != 0;
    const bool hard_drop = (events & GAME_EVENT_HARD_DROP) != 0;

    if (hard_drop) {
        sound_map_push(&mapper, SOUND_HARD_DROP, SOUND_GAIN_DROP);
    }

    /*
     * A hard drop locks the piece in the same tick, so its GAME_EVENT_LOCK must
     * not add a click under the thud. A line clear always replaces the lock
     * click with its arpeggio.
     */
    if (!game_over) {
        if ((events & GAME_EVENT_LINE_CLEAR) != 0) {
            const int lines = game != NULL ? (int)game->last_clear_count : 1;
            sound_map_push(&mapper, sound_map_clear_id(lines), SOUND_GAIN_CLEAR);
        } else if ((events & GAME_EVENT_LOCK) != 0 && !hard_drop) {
            sound_map_push(&mapper, SOUND_LOCK, SOUND_GAIN_LOCK);
        }
    }

    if ((events & GAME_EVENT_LEVEL_UP) != 0) {
        sound_map_push(&mapper, SOUND_LEVEL_UP, SOUND_GAIN_LEVEL_UP);
    }
    if (game_over) {
        sound_map_push(&mapper, SOUND_GAME_OVER, SOUND_GAIN_GAME_OVER);
    }

    if ((events & GAME_EVENT_MOVE) != 0) {
        sound_map_push(&mapper, SOUND_MOVE, SOUND_GAIN_MOVE);
    }
    if ((events & GAME_EVENT_ROTATE) != 0) {
        sound_map_push(&mapper, SOUND_ROTATE, SOUND_GAIN_ROTATE);
    }
    if ((events & GAME_EVENT_HOLD) != 0) {
        sound_map_push(&mapper, SOUND_HOLD, SOUND_GAIN_HOLD);
    }

    return mapper.count;
}
