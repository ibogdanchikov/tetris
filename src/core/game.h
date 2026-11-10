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

#define GAME_DAS_TICKS 10        /* delayed auto shift before the first repeat */
#define GAME_ARR_TICKS 2         /* auto repeat rate after DAS */
#define GAME_LOCK_DELAY_TICKS 30 /* time a resting piece may still be moved */
#define GAME_MAX_LOCK_RESETS 15  /* how often a move may reset the lock delay */

/* How many pieces the next queue holds; the first one is drawn as the preview. */
#define GAME_NEXT_COUNT 3

/*
 * Gravity is a per-tick increment in 1/65536 of a row, so the whole simulation
 * stays integer. GAME_GRAVITY_ONE_ROW is one full row.
 */
#define GAME_GRAVITY_ONE_ROW 65536u

/* Soft drop is at least this fast, and at least 20 times the level gravity. */
#define GAME_SOFT_DROP_MIN_INCREMENT 32768u
#define GAME_SOFT_DROP_MULTIPLIER 20u

/* Highest level with its own gravity entry; above that the level 20 value is used. */
#define GAME_MAX_GRAVITY_LEVEL 20

/* Why the run ended. GAME_OVER_NONE means the game is still running. */
typedef enum GameOverReason {
    GAME_OVER_NONE = 0,
    GAME_OVER_BLOCK_OUT, /* the new piece could not spawn */
    GAME_OVER_LOCK_OUT,  /* a piece locked with every cell in the hidden rows */
} GameOverReason;

/*
 * What happened during the most recent tick. The bits are meant for
 * presentation (animations, sound) and are not part of the rules; they are
 * cleared at the start of every game_step. A frame that runs several ticks must
 * OR the values together.
 */
typedef enum GameEvent {
    GAME_EVENT_MOVE = 1u << 0,
    GAME_EVENT_ROTATE = 1u << 1,
    GAME_EVENT_KICK = 1u << 2, /* a rotation used a wall kick (index > 0) */
    GAME_EVENT_SOFT_DROP_ROW = 1u << 3,
    GAME_EVENT_HARD_DROP = 1u << 4,
    GAME_EVENT_LOCK = 1u << 5,
    GAME_EVENT_LINE_CLEAR = 1u << 6,
    GAME_EVENT_HOLD = 1u << 7,
    GAME_EVENT_LEVEL_UP = 1u << 8,
    GAME_EVENT_GAME_OVER = 1u << 9,
    GAME_EVENT_SPAWN = 1u << 10,
} GameEvent;

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

    /* Upcoming pieces, next_queue[0] is the one that spawns next. */
    PieceType next_queue[GAME_NEXT_COUNT];
    Bag bag;
    uint64_t rng_state;
    uint64_t tick;

    uint32_t piece_serial;  /* incremented on every spawn; renderers watch it */
    uint32_t gravity_accum; /* 1/65536 of a row owed to gravity */
    uint16_t lock_counter;
    uint8_t lock_resets;

    /* Smallest y (deepest row) the piece has ever reached; a new one refreshes
       the lock delay and its reset budget. */
    int8_t lowest_y;
    bool grounded;

    int8_t shift_dir;       /* -1 left, 1 right, 0 none (DAS state) */
    uint16_t shift_counter; /* ticks until the next auto-repeat move */

    uint32_t lines_cleared;
    uint32_t score;
    uint32_t level;       /* start_level + lines_cleared / 10 */
    uint32_t start_level; /* level at game_init, kept so the formula is explicit */

    uint8_t last_clear_count; /* lines removed by the most recent lock */
    uint8_t cleared_rows[4];  /* row indices as they were before the shift */
    uint8_t cleared_row_count;

    bool has_hold;
    PieceType hold_piece;
    bool hold_used; /* a hold is spent until the next piece locks */

    /* Rotation bookkeeping, kept for T-spin detection in a later milestone. */
    int8_t last_kick_index;      /* 0 = no offset, 1..4 = kick table entry */
    bool last_action_was_rotation;

    bool game_over;
    GameOverReason game_over_reason;
    uint32_t events; /* GAME_EVENT_* bits for the most recent tick */
} GameState;

/*
 * Clears the whole struct to zero, seeds the generator, fills the bag and the
 * next queue and spawns the first piece at level 1. The board starts empty.
 */
void game_init(GameState *game, uint64_t seed);

/*
 * Same as game_init, but the run starts at start_level (0 is treated as 1).
 * The level only affects gravity and the score multiplier; the line goal for
 * the next level is always 10 lines per level.
 */
void game_init_at_level(GameState *game, uint64_t seed, uint32_t start_level);

/*
 * Advances the simulation by exactly one tick.
 *
 * Order inside a tick:
 *   1. clear the event bits,
 *   2. hold from the pressed edge (before rotation, so a piece taken from the
 *      hold slot can be rotated in the same tick),
 *   3. rotation from the pressed edge, with SRS wall kicks; CW and CCW in the
 *      same tick cancel,
 *   4. horizontal movement: a fresh press moves once and arms DAS, holding
 *      repeats every ARR ticks after DAS has elapsed,
 *   5. hard drop from the pressed edge: fall to the ghost row and lock now,
 *   6. gravity or soft drop, whichever is faster,
 *   7. lock delay while the piece rests on the stack,
 *   8. tick counter.
 *
 * When both LEFT and RIGHT are held the most recently pressed one wins; both
 * pressed in the same tick cancel until one is released.
 *
 * A game that is over only advances the tick counter.
 */
void game_step(GameState *game, const GameInput *input);

/*
 * Moves the next piece into play, refills the queue from the bag and resets the
 * per-piece counters and the hold flag. The piece spawns in the guideline
 * columns with its lowest cells in the first hidden row and then drops one row
 * if it fits. If the spawn position collides, ends the game with BLOCK_OUT.
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

/*
 * Gravity increment for a level, in 1/65536 of a row per tick, taken from a
 * static table. Levels above GAME_MAX_GRAVITY_LEVEL use the level 20 value and
 * level 0 is treated as level 1.
 */
uint32_t game_gravity_increment(uint32_t level);

/*
 * The wall kick offsets tried for one rotation, in order: out is filled with
 * the (x, y) translations to add to the basic rotation, and the return value is
 * how many were written. I and the 3x3 pieces always get five entries; O gets
 * one, the identity, because it never kicks. Rotations are taken modulo 4, and
 * a rotation that is not a single quarter turn only gets the identity.
 *
 * This is the same lookup game_step uses, so tests can check the tables against
 * the indexing rather than against a copy of the numbers.
 */
int game_kick_offsets(PieceType type, int from_rotation, int to_rotation,
                      PieceCell out[5]);
