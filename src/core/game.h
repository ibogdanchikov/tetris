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

/*
 * Default shift timing. A run can use other values through GameConfig; these
 * are what game_init and game_init_at_level use.
 */
#define GAME_DAS_TICKS 10        /* delayed auto shift before the first repeat */
#define GAME_ARR_TICKS 2         /* auto repeat rate after DAS; 0 = straight to the wall */
#define GAME_LOCK_DELAY_TICKS 30 /* time a resting piece may still be moved */
#define GAME_MAX_LOCK_RESETS 15  /* how often a move may reset the lock delay */

/*
 * How long completed rows stay on the board before they are removed, so the
 * view can animate them. Score, lines and GAME_EVENT_LINE_CLEAR still happen on
 * the lock tick; the rows disappear and the next piece spawns
 * GAME_LINE_CLEAR_DELAY_TICKS ticks later, together with GAME_EVENT_CLEAR_DONE.
 */
#define GAME_LINE_CLEAR_DELAY_TICKS 24

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
 * What the tick is doing. GAME_PHASE_FALLING runs the active piece normally;
 * GAME_PHASE_CLEARING counts out GAME_LINE_CLEAR_DELAY_TICKS while the full rows
 * are still on the board and nothing is falling. Game over stays a separate
 * flag: it already has a reason enum and short-circuits game_step, so folding it
 * into the phase would only duplicate that check.
 */
typedef enum GamePhase {
    GAME_PHASE_FALLING = 0,
    GAME_PHASE_CLEARING,
} GamePhase;

/*
 * What happened during the most recent tick. The bits are meant for
 * presentation (animations, sound) and are not part of the rules; they are
 * cleared at the start of every game_step. A frame that runs several ticks must
 * OR the values together.
 *
 * GAME_EVENT_LOCK reports GameState.last_locked; GAME_EVENT_HARD_DROP reports
 * last_hard_drop_rows and last_hard_drop_from. GAME_EVENT_LINE_CLEAR marks the
 * lock that completed rows, and GAME_EVENT_CLEAR_DONE marks the later tick on
 * which they were removed and the next piece spawned.
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
    GAME_EVENT_CLEAR_DONE = 1u << 11, /* the delayed rows are gone, next piece spawned */
} GameEvent;

/*
 * What kind of T-spin a lock was, by the 3-corner rule (see game_tspin_kind).
 * Every other piece, and a T that did not end on a rotation, is NONE.
 */
typedef enum GameTSpin {
    GAME_TSPIN_NONE = 0,
    GAME_TSPIN_MINI,
    GAME_TSPIN_FULL,
} GameTSpin;

/*
 * Scoring extras, all before the level multiplier.
 *
 * A back-to-back clear (a four-line clear or a T-spin that clears lines, right
 * after another one with no plain line clear in between) gets its line clear
 * points times GAME_B2B_NUMERATOR / GAME_B2B_DENOMINATOR. A combo adds
 * GAME_COMBO_POINTS for every clearing lock in a row after the first. A perfect
 * clear adds its own table on top.
 */
#define GAME_B2B_NUMERATOR 3u
#define GAME_B2B_DENOMINATOR 2u
#define GAME_COMBO_POINTS 50u

/*
 * What the most recent lock scored, set on every GAME_EVENT_LOCK tick so
 * presentation can show the clear without working it out again.
 */
typedef struct GameClearResult {
    uint8_t lines;        /* rows the lock completed, the same as last_clear_count */
    uint8_t tspin;        /* GameTSpin */
    bool back_to_back;    /* the clear got the back-to-back bonus */
    bool perfect_clear;   /* the board is empty once the rows are removed */
    uint16_t combo;       /* 0 = no combo; n = the n-th clearing lock in a row after the first */
    uint32_t points;      /* what the lock scored, level included; drop points are not in it */
} GameClearResult;

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
    uint16_t das_ticks;     /* this run's DAS, from GameConfig */
    uint16_t arr_ticks;     /* this run's ARR, from GameConfig; 0 = straight to the wall */

    uint32_t lines_cleared;
    uint32_t score;
    uint32_t level;       /* start_level + lines_cleared / 10 */
    uint32_t start_level; /* level at game_init, kept so the formula is explicit */

    uint8_t last_clear_count; /* lines completed by the most recent lock */
    /* Row indices as they were at the lock. Valid while GAME_PHASE_CLEARING
       (the rows are still on the board) and afterwards as a record of what was
       removed. At most four indices are kept. */
    uint8_t cleared_rows[4];
    uint8_t cleared_row_count;

    bool has_hold;
    PieceType hold_piece;
    bool hold_used; /* a hold is spent until the next piece locks */

    /*
     * Rotation bookkeeping for T-spin detection. last_action_was_rotation is
     * true when the latest thing that moved the piece was a rotation: a shift,
     * a gravity or soft-drop row and a hard drop that falls at least one row
     * all clear it.
     */
    int8_t last_kick_index;      /* 0 = no offset, 1..4 = kick table entry */
    bool last_action_was_rotation;

    /*
     * Scoring state across locks. back_to_back is armed by a four-line clear
     * or a T-spin that clears lines, and dropped by any other line clear; a
     * lock that clears nothing leaves it alone. clear_streak counts the locks
     * in a row that cleared lines and goes back to 0 on a lock that clears
     * nothing.
     */
    bool back_to_back;
    uint16_t clear_streak;
    GameClearResult last_clear;

    /*
     * Where the tick is: falling or counting out a line clear. While clearing
     * there is no active piece, so the piece fields hold the last locked piece
     * but must not be read as one. game_over is separate, see GamePhase.
     */
    GamePhase phase;
    uint16_t clear_timer; /* ticks left before the cleared rows are removed */

    /*
     * Effects data. These are snapshots of the piece at the moment the lock was
     * written, so their cells are absolute board coordinates.
     *
     * last_locked      set on every GAME_EVENT_LOCK tick.
     * last_hard_drop_* set on every GAME_EVENT_HARD_DROP tick, before the drop;
     *                  rows is the distance travelled and last_hard_drop_from
     *                  the piece as it was at the top. Both are useful for a
     *                  trail; rows is 0 when the piece was already resting.
     */
    ActivePiece last_locked;
    uint8_t last_hard_drop_rows;
    ActivePiece last_hard_drop_from;

    bool game_over;
    GameOverReason game_over_reason;
    uint32_t events; /* GAME_EVENT_* bits for the most recent tick */
} GameState;

/*
 * What a run is started with. The core takes the values as they are; the app
 * layer decides which ranges a player may pick.
 *
 * start_level  level of the first piece; 0 is treated as 1.
 * das_ticks    ticks a held LEFT/RIGHT waits before it starts repeating.
 * arr_ticks    ticks between two repeats; 0 moves the piece as far as it goes
 *              on every repeat tick.
 */
typedef struct GameConfig {
    uint32_t start_level;
    uint16_t das_ticks;
    uint16_t arr_ticks;
} GameConfig;

/* Level 1 with GAME_DAS_TICKS and GAME_ARR_TICKS. */
GameConfig game_default_config(void);

/*
 * Clears the whole struct to zero, seeds the generator, fills the bag and the
 * next queue and spawns the first piece with the config's level and shift
 * timing. The board starts empty.
 */
void game_init_with_config(GameState *game, uint64_t seed, const GameConfig *config);

/* game_init_with_config with game_default_config: level 1, default DAS and ARR. */
void game_init(GameState *game, uint64_t seed);

/*
 * Same as game_init, but the run starts at start_level (0 is treated as 1).
 * The level only affects gravity and the score multiplier; the line goal for
 * the next level is always 10 lines per level.
 */
void game_init_at_level(GameState *game, uint64_t seed, uint32_t start_level);

/*
 * Changes the shift timing of a running game, for a settings change in the
 * middle of a run. A repeat that is already counting down keeps its current
 * count; the next DAS or ARR reload uses the new values.
 */
void game_set_shift_timing(GameState *game, uint16_t das_ticks, uint16_t arr_ticks);

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
 * While phase is GAME_PHASE_CLEARING every input is ignored except LEFT/RIGHT,
 * which keep charging the DAS/ARR counters so the piece that spawns after the
 * delay inherits that progress instead of resetting it. When the timer reaches
 * zero the recorded rows are removed, GAME_EVENT_CLEAR_DONE fires and the next
 * piece spawns (which may end the game with BLOCK_OUT).
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

/*
 * How far the line-clear animation has got, in ticks: 0 on the lock tick, then
 * 1 .. GAME_LINE_CLEAR_DELAY_TICKS - 1 on the delay ticks. Returns 0 outside
 * GAME_PHASE_CLEARING, including the tick that removes the rows. Integer only,
 * so presentation can scale it freely
 * (elapsed = GAME_LINE_CLEAR_DELAY_TICKS - clear_timer).
 */
uint16_t game_clear_progress(const GameState *game);

/* Absolute board cells of the active piece. */
void game_piece_cells(const ActivePiece *piece, int out_x[4], int out_y[4]);

/*
 * The T-spin rule, for a piece about to lock on this board. It is a T-spin only
 * when the piece is a T and the last thing that moved it was a rotation, and
 * at least three of the four cells diagonal to its center are filled or
 * outside the board. It is a full T-spin when both corners on the side the T
 * points to are among them, or when the rotation needed the fifth kick
 * (last_kick_index 4); otherwise it is a mini.
 */
GameTSpin game_tspin_kind(const Board *board, const ActivePiece *piece,
                          bool last_action_was_rotation, int last_kick_index);

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
