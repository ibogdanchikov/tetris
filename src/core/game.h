#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "core/bag.h"
#include "core/board.h"
#include "core/input.h"
#include "core/piece.h"

/*
 * Pure game state: no SDL, no OpenGL, no clock, no allocation, no floating
 * point. The simulation advances in fixed ticks; the caller decides how many
 * ticks to run. Every duration below is in ticks at 60 ticks per second.
 */

#define GAME_GRAVITY_TICKS 60    /* one row per second (levels arrive later) */
#define GAME_SOFT_DROP_TICKS 2   /* one row per 2 ticks while soft drop is held */
#define GAME_DAS_TICKS 10        /* delayed auto shift before the first repeat */
#define GAME_ARR_TICKS 2         /* auto repeat rate after DAS */
#define GAME_LOCK_DELAY_TICKS 30 /* time a resting piece may still be moved */
#define GAME_MAX_LOCK_RESETS 15  /* how often a move may reset the lock delay */

/* The falling piece. x, y is the bottom-left corner of its SRS bounding box. */
typedef struct ActivePiece {
    PieceType type;
    int8_t rotation;
    int8_t x;
    int8_t y;
} ActivePiece;

typedef struct GameState {
    Board board;
    ActivePiece piece;
    PieceType next_piece; /* one-piece preview, drawn when the piece spawns */
    Bag bag;
    uint64_t rng_state;
    uint64_t tick;
    uint32_t piece_serial;   /* incremented on every spawn; renderers watch it */
    uint16_t gravity_counter;
    uint16_t lock_counter;
    uint8_t lock_resets;
    bool grounded;
    int8_t shift_dir;        /* -1 left, 1 right, 0 none (DAS state) */
    uint16_t shift_counter;  /* ticks until the next auto-repeat move */
    uint32_t lines_cleared;
    uint8_t last_clear_count; /* lines removed by the most recent lock */
    bool topped_out;
} GameState;

/*
 * Clears the whole struct to zero, seeds the generator, fills the bag, draws
 * the preview and spawns the first piece. The board starts empty.
 */
void game_init(GameState *game, uint64_t seed);

/*
 * Advances the simulation by exactly one tick.
 *
 * Order inside a tick:
 *   1. rotation from the pressed edge; CW and CCW in the same tick cancel,
 *   2. horizontal movement: a fresh press moves once and arms DAS, holding
 *      repeats every ARR ticks after DAS has elapsed,
 *   3. hard drop from the pressed edge: fall to the ghost row and lock now,
 *   4. gravity or soft drop, whichever is faster,
 *   5. lock delay while the piece rests on the stack,
 *   6. tick counter.
 *
 * When both LEFT and RIGHT are held the most recently pressed one wins; both
 * pressed in the same tick cancel until one is released. There are no wall
 * kicks yet, so a rotation that would collide is simply rejected.
 *
 * A topped-out game only advances the tick counter.
 */
void game_step(GameState *game, const GameInput *input);

/*
 * Moves the next piece into play, draws a new preview from the bag and resets
 * the per-piece counters. The piece spawns in the guideline columns with its
 * lowest cells in the first hidden row and then drops one row if it fits.
 * If the spawn position collides, sets topped_out.
 */
void game_spawn_next(GameState *game);

/* True when every cell of the piece is inside the board and empty. */
bool game_piece_fits(const Board *board, const ActivePiece *piece);

/*
 * Row the active piece would come to rest at if dropped straight down.
 * This is the bounding box origin, which can be negative: the lowest cells of
 * I and of the 3x3 pieces are not on the box bottom.
 */
int game_ghost_y(const GameState *game);

/* Absolute board cells of the active piece. */
void game_piece_cells(const ActivePiece *piece, int out_x[4], int out_y[4]);
