#pragma once

#include <stdint.h>

#include "audio/synth.h"
#include "core/game.h"

/*
 * Turns one frame's OR-ed GAME_EVENT_* bits into the sounds to play. SDL-free
 * and side-effect-free so the precedence rules are unit tested, and so main.c
 * stays a thin loop: map once per frame, then hand each request to the audio
 * layer.
 *
 * Precedence exists because one action can set several event bits:
 *
 *  - a line clear replaces the lock click (the clear arpeggio already says the
 *    piece landed); the arpeggio is picked from GameState.last_clear_count,
 *    with anything above four using the tetris sound,
 *  - a hard drop always locks in the same tick, so the thud replaces the lock
 *    click and is played before the clear sound when both happen,
 *  - game over suppresses the lock and clear sounds: the phrase is the point,
 *    and a lock-out tick would otherwise stack two sounds on top of it; a run
 *    that reached its goal (game_run_completed) plays SOUND_FINISH instead of
 *    the game-over phrase,
 *  - level up, move, rotate and hold are added on top, because they can
 *    genuinely happen alongside a drop or a lock.
 */

/* Worst case is hard drop + clear + level up + move + rotate + hold. */
#define SOUND_MAP_MAX_REQUESTS 8

typedef struct SoundRequest {
    SoundId id;
    float gain; /* per-sound balance, multiplied by the synth and master gains */
} SoundRequest;

/*
 * Writes at most max requests to out and returns how many were written. Zero
 * events produce no requests. A NULL game is treated as a single-line clear when
 * GAME_EVENT_LINE_CLEAR is set. Requests are in play order: hard drop first,
 * then the clear sound, then everything added on top.
 */
int sound_map_events(uint32_t events, const GameState *game, SoundRequest out[], int max);
