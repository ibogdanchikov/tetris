#pragma once

#include <stdint.h>

#include "core/board.h"

/*
 * Pure game state: no SDL, no OpenGL, no clock, no allocation. The simulation
 * advances in fixed ticks; the caller decides how many ticks to run.
 */
typedef struct GameState {
    Board board;
    uint64_t tick;
    uint64_t rng_state;
} GameState;

/* Clears the board, installs the demo pattern and seeds the generator. */
void game_init(GameState *game, uint64_t seed);

/* Advances the simulation by exactly one fixed tick. */
void game_step(GameState *game);
