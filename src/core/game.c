#include "core/game.h"

#include <string.h>

#include "core/rng.h"

/*
 * SRS wall kick tables, straight from the Tetris Guideline as published on
 * tetris.wiki ("Super Rotation System"). Each entry is a translation applied to
 * the piece after it has been rotated in place; the first fitting entry wins.
 * The wiki writes them with +y pointing up, which is the convention of this
 * project too (board row 0 is the bottom row), so the tables are copied
 * verbatim.
 *
 * The tables are indexed [rotation before the rotation][0 = clockwise,
 * 1 = counter-clockwise]. Rotating back and forth uses the same five entries in
 * some pairs (R->0 and R->2, L->0 and L->2), which is why several rows repeat.
 *
 * Orientation is pinned by tests, not by this comment:
 *   - a vertical I in column 0 rotating CW lands flat at columns 0-3 (kick
 *     index 2, offset (+2, 0)),
 *   - a flat I on the floor rotating CW climbs two rows (kick index 4, offset
 *     (+1, +2)), which only fits if +y is up,
 *   - a T under an overhang rotates down two rows (kick index 3, offset
 *     (0, -2)), which only fits if -y is down.
 */
static const PieceCell KICKS_JLSTZ[4][2][5] = {
    /* from spawn */
    {
        { { 0, 0 }, { -1, 0 }, { -1, 1 }, { 0, -2 }, { -1, -2 } }, /* 0->R */
        { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, -2 }, { 1, -2 } },    /* 0->L */
    },
    /* from R */
    {
        { { 0, 0 }, { 1, 0 }, { 1, -1 }, { 0, 2 }, { 1, 2 } },  /* R->2 */
        { { 0, 0 }, { 1, 0 }, { 1, -1 }, { 0, 2 }, { 1, 2 } },  /* R->0 */
    },
    /* from 2 */
    {
        { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, -2 }, { 1, -2 } },    /* 2->L */
        { { 0, 0 }, { -1, 0 }, { -1, 1 }, { 0, -2 }, { -1, -2 } }, /* 2->R */
    },
    /* from L */
    {
        { { 0, 0 }, { -1, 0 }, { -1, -1 }, { 0, 2 }, { -1, 2 } }, /* L->0 */
        { { 0, 0 }, { -1, 0 }, { -1, -1 }, { 0, 2 }, { -1, 2 } }, /* L->2 */
    },
};

static const PieceCell KICKS_I[4][2][5] = {
    /* from spawn */
    {
        { { 0, 0 }, { -2, 0 }, { 1, 0 }, { -2, -1 }, { 1, 2 } }, /* 0->R */
        { { 0, 0 }, { -1, 0 }, { 2, 0 }, { -1, 2 }, { 2, -1 } }, /* 0->L */
    },
    /* from R */
    {
        { { 0, 0 }, { -1, 0 }, { 2, 0 }, { -1, 2 }, { 2, -1 } },  /* R->2 */
        { { 0, 0 }, { 2, 0 }, { -1, 0 }, { 2, 1 }, { -1, -2 } },  /* R->0 */
    },
    /* from 2. CW turns state 2 into L (3), CCW into R (1). */
    {
        { { 0, 0 }, { 2, 0 }, { -1, 0 }, { 2, 1 }, { -1, -2 } },  /* 2->L */
        { { 0, 0 }, { 1, 0 }, { -2, 0 }, { 1, -2 }, { -2, 1 } },  /* 2->R */
    },
    /* from L */
    {
        { { 0, 0 }, { 1, 0 }, { -2, 0 }, { 1, -2 }, { -2, 1 } },  /* L->0 */
        { { 0, 0 }, { -2, 0 }, { 1, 0 }, { -2, -1 }, { 1, 2 } },  /* L->2 */
    },
};

/*
 * Gravity per level, in 1/65536 of a row per tick. Generated from the guideline
 * formula
 *
 *     seconds_per_row = (0.8 - (level - 1) * 0.007) ^ (level - 1)
 *     increment      = round(65536 / (seconds_per_row * 60))
 *
 * at 60 ticks per second. Level 1 is 65536 / 60 = 1092.27 -> 1092, so a piece
 * falls one row every 60.015 ticks; in whole ticks that means the first fall of
 * a fresh piece happens on tick 61, not 60.
 */
static const uint32_t GRAVITY_INCREMENT[GAME_MAX_GRAVITY_LEVEL] = {
    1092,   /*  1 */ 1377,   /*  2 */ 1768,   /*  3 */ 2311,   /*  4 */
    3075,   /*  5 */ 4169,   /*  6 */ 5759,   /*  7 */ 8107,   /*  8 */
    11634,  /*  9 */ 17026,  /* 10 */ 25416,  /* 11 */ 38709,  /* 12 */
    60169,  /* 13 */ 95483,  /* 14 */ 154742, /* 15 */ 256187, /* 16 */
    433425, /* 17 */ 749597, /* 18 */ 1325716, /* 19 */ 2398490, /* 20 */
};

/*
 * Line clear points before the level multiplier, by T-spin kind and line count
 * (Tetris Guideline). A T covers at most three rows, so the T-spin rows past
 * three only guard a test board that already had full rows on it.
 */
static const uint32_t LINE_CLEAR_POINTS[3][5] = {
    [GAME_TSPIN_NONE] = { 0, 100, 300, 500, 800 },
    [GAME_TSPIN_MINI] = { 100, 200, 400, 400, 400 },
    [GAME_TSPIN_FULL] = { 400, 800, 1200, 1600, 1600 },
};

/* Perfect clear points before the level multiplier, by line count, and the
   bigger award for a back-to-back four-line perfect clear. */
static const uint32_t PERFECT_CLEAR_POINTS[5] = { 0, 800, 1200, 1800, 2000 };
#define PERFECT_CLEAR_B2B_FOUR_POINTS 3200u

uint32_t game_gravity_increment(uint32_t level)
{
    if (level < 1) {
        level = 1;
    }
    if (level > GAME_MAX_GRAVITY_LEVEL) {
        level = GAME_MAX_GRAVITY_LEVEL;
    }
    return GRAVITY_INCREMENT[level - 1];
}

/* Saturating score addition, so no total can wrap around. */
static uint32_t game_score_add(uint32_t score, uint64_t points)
{
    const uint64_t sum = (uint64_t)score + points;
    return sum > UINT32_MAX ? UINT32_MAX : (uint32_t)sum;
}

void game_piece_cells(const ActivePiece *piece, int out_x[4], int out_y[4])
{
    PieceCell cells[4];
    piece_cells(piece->type, piece->rotation, cells);
    for (int i = 0; i < 4; ++i) {
        out_x[i] = piece->x + cells[i].x;
        out_y[i] = piece->y + cells[i].y;
    }
}

bool game_piece_fits(const Board *board, const ActivePiece *piece)
{
    int xs[4];
    int ys[4];
    game_piece_cells(piece, xs, ys);

    for (int i = 0; i < 4; ++i) {
        if (!board_in_bounds(xs[i], ys[i])) {
            return false;
        }
        if (board_get(board, xs[i], ys[i]) != CELL_EMPTY) {
            return false;
        }
    }
    return true;
}

/* Lowest board row occupied by the piece; the box origin is not always it. */
static int game_piece_lowest_cell_y(const ActivePiece *piece)
{
    int xs[4];
    int ys[4];
    game_piece_cells(piece, xs, ys);

    int lowest = ys[0];
    for (int i = 1; i < 4; ++i) {
        if (ys[i] < lowest) {
            lowest = ys[i];
        }
    }
    return lowest;
}

/* True when the piece could move down one row. */
static bool game_can_fall(const GameState *game)
{
    ActivePiece below = game->piece;
    below.y = (int8_t)(below.y - 1);
    return game_piece_fits(&game->board, &below);
}

/* Moves the piece by (dx, dy) when the target cells are free. */
static bool game_try_move(GameState *game, int dx, int dy)
{
    ActivePiece moved = game->piece;
    moved.x = (int8_t)(moved.x + dx);
    moved.y = (int8_t)(moved.y + dy);

    if (!game_piece_fits(&game->board, &moved)) {
        return false;
    }
    game->piece = moved;
    return true;
}

/*
 * A successful move or rotation while resting on the stack gives the player
 * more time to slide the piece. The number of resets per piece is capped, so a
 * piece cannot be kept alive forever by sliding or spinning it.
 *
 * Leaving the ground does *not* reset anything: only reaching a new lowest row
 * does, and that is handled by game_note_lowest.
 */
static void game_reset_lock_delay(GameState *game)
{
    if (!game->grounded || game->lock_resets >= GAME_MAX_LOCK_RESETS) {
        return;
    }
    game->lock_counter = 0;
    ++game->lock_resets;
}

/*
 * Extended placement: reaching a row the piece has never been in refreshes both
 * the lock delay and its reset budget. That is what lets a piece be manoeuvred
 * into a slot after it has already touched down, while still bounding how long
 * it can be kept alive.
 *
 * "Lowest" is the smallest y: row 0 is the bottom of the board, so descending
 * lowers the number. A rotation that lifts the piece off the ground (for
 * example an S wobbling to a state whose lowest cell sits a row higher) is not
 * a new row and must not refresh anything, or spinning would be a free reset.
 */
static void game_note_lowest(GameState *game)
{
    const int lowest = game_piece_lowest_cell_y(&game->piece);
    if (lowest >= (int)game->lowest_y) {
        return;
    }
    game->lowest_y = (int8_t)lowest;
    game->lock_resets = 0;
    game->lock_counter = 0;
}

/*
 * Wall kick table lookup for one rotation, shared by game_try_rotate and the
 * tests. The table index is derived from the (from, to) pair, not from the
 * direction flag, so a caller cannot pick the wrong half of the table.
 */
int game_kick_offsets(PieceType type, int from_rotation, int to_rotation,
                      PieceCell out[5])
{
    const int from = ((from_rotation % 4) + 4) % 4;
    const int to = ((to_rotation % 4) + 4) % 4;
    const int quarter_turns = (to - from + 4) % 4;

    out[0] = (PieceCell){ 0, 0 };
    if (quarter_turns != 1 && quarter_turns != 3) {
        return 1;
    }

    /* 1 is a clockwise quarter turn, 3 is counter-clockwise. */
    const int counter_clockwise = quarter_turns == 3;

    switch (type) {
    case PIECE_I:
        for (int i = 0; i < 5; ++i) {
            out[i] = KICKS_I[from][counter_clockwise][i];
        }
        return 5;
    case PIECE_O:
        return 1;
    case PIECE_T:
    case PIECE_S:
    case PIECE_Z:
    case PIECE_J:
    case PIECE_L:
        for (int i = 0; i < 5; ++i) {
            out[i] = KICKS_JLSTZ[from][counter_clockwise][i];
        }
        return 5;
    case PIECE_COUNT:
        break;
    }
    return 1;
}

/*
 * SRS rotation: rotate in place, then try the kick offsets for this (from, to)
 * pair in order and take the first one that fits. A rotation that fits nowhere
 * leaves the piece untouched.
 */
static bool game_try_rotate(GameState *game, int direction)
{
    const int from = ((int)game->piece.rotation % 4 + 4) % 4;
    const int to = (from + direction + 4) % 4;

    PieceCell kicks[5];
    const int attempts = game_kick_offsets(game->piece.type, from, to, kicks);

    for (int index = 0; index < attempts; ++index) {
        ActivePiece rotated = game->piece;
        rotated.rotation = (int8_t)to;
        rotated.x = (int8_t)(rotated.x + kicks[index].x);
        rotated.y = (int8_t)(rotated.y + kicks[index].y);
        if (!game_piece_fits(&game->board, &rotated)) {
            continue;
        }

        game->piece = rotated;
        game->last_kick_index = (int8_t)index;
        game->last_action_was_rotation = true;
        game->events |= GAME_EVENT_ROTATE;
        if (index > 0) {
            game->events |= GAME_EVENT_KICK;
        }
        game_reset_lock_delay(game);
        return true;
    }
    return false;
}

/*
 * Direction the held keys ask for, including the both-keys priority. Split out
 * so the line-clear delay can charge DAS without an active piece to move.
 */
static int8_t game_shift_direction(const GameState *game, const GameInput *input)
{
    const bool left = (input->held & ACTION_LEFT) != 0;
    const bool right = (input->held & ACTION_RIGHT) != 0;

    if (left && right) {
        const bool press_left = (input->pressed & ACTION_LEFT) != 0;
        const bool press_right = (input->pressed & ACTION_RIGHT) != 0;
        if (press_left != press_right) {
            return press_left ? -1 : 1;
        }
        if (!press_left) {
            /* Both already held: the one pressed last keeps priority. */
            return game->shift_dir;
        }
        return 0;
    }
    if (left) {
        return -1;
    }
    if (right) {
        return 1;
    }
    return 0;
}

/*
 * Horizontal movement with DAS/ARR. shift_counter counts down to the next
 * move: a fresh press moves at once and loads DAS, and every repeat loads ARR.
 */
static void game_step_shift(GameState *game, const GameInput *input)
{
    const int8_t direction = game_shift_direction(game, input);

    if (direction == 0) {
        game->shift_dir = 0;
        game->shift_counter = 0;
        return;
    }

    if (direction != game->shift_dir) {
        game->shift_dir = direction;
        game->shift_counter = game->das_ticks;
        if (game_try_move(game, direction, 0)) {
            game->events |= GAME_EVENT_MOVE;
            game->last_action_was_rotation = false;
            game_reset_lock_delay(game);
        }
        return;
    }

    if (game->shift_counter > 0) {
        --game->shift_counter;
    }
    if (game->shift_counter == 0) {
        /* ARR 0 repeats without a gap: the piece goes as far as it can. */
        bool moved = game_try_move(game, direction, 0);
        if (moved && game->arr_ticks == 0) {
            while (game_try_move(game, direction, 0)) {
            }
        }
        if (moved) {
            game->events |= GAME_EVENT_MOVE;
            game->last_action_was_rotation = false;
            game_reset_lock_delay(game);
        }
        game->shift_counter = game->arr_ticks;
    }
}

/*
 * Horizontal input while there is no piece: the rows cannot move, but DAS/ARR
 * still run. The counter is reloaded exactly as a real repeat would reload it,
 * so after the delay the new piece continues on the same ARR grid instead of
 * waiting out a fresh DAS.
 */
static void game_step_shift_charge(GameState *game, const GameInput *input)
{
    const int8_t direction = game_shift_direction(game, input);

    if (direction == 0) {
        game->shift_dir = 0;
        game->shift_counter = 0;
        return;
    }

    if (direction != game->shift_dir) {
        game->shift_dir = direction;
        game->shift_counter = game->das_ticks;
        return;
    }

    if (game->shift_counter > 0) {
        --game->shift_counter;
    }
    if (game->shift_counter == 0) {
        game->shift_counter = game->arr_ticks;
    }
}

/*
 * Ends the run from inside a step. The step in progress counts as played, so
 * end_tick is one past the current tick, the value tick will have once the
 * step has finished.
 */
static void game_end(GameState *game, GameOverReason reason)
{
    game->game_over = true;
    game->game_over_reason = reason;
    game->events |= GAME_EVENT_GAME_OVER;
    game->end_tick = game->tick + 1u;
}

/* A corner counts when it is filled or outside the board, walls and floor included. */
static bool game_corner_blocked(const Board *board, int x, int y)
{
    return !board_in_bounds(x, y) || board_get(board, x, y) != CELL_EMPTY;
}

GameTSpin game_tspin_kind(const Board *board, const ActivePiece *piece,
                          bool last_action_was_rotation, int last_kick_index)
{
    if (piece->type != PIECE_T || !last_action_was_rotation) {
        return GAME_TSPIN_NONE;
    }

    /* The T's center is the middle of its 3x3 box in every rotation. */
    const int cx = piece->x + 1;
    const int cy = piece->y + 1;
    const bool top_left = game_corner_blocked(board, cx - 1, cy + 1);
    const bool top_right = game_corner_blocked(board, cx + 1, cy + 1);
    const bool bottom_left = game_corner_blocked(board, cx - 1, cy - 1);
    const bool bottom_right = game_corner_blocked(board, cx + 1, cy - 1);

    const int blocked = (int)top_left + (int)top_right + (int)bottom_left + (int)bottom_right;
    if (blocked < 3) {
        return GAME_TSPIN_NONE;
    }

    /* The two corners on the side the T points to: up, right, down, left. */
    bool front = false;
    switch (((int)piece->rotation % 4 + 4) % 4) {
    case 0:
        front = top_left && top_right;
        break;
    case 1:
        front = top_right && bottom_right;
        break;
    case 2:
        front = bottom_left && bottom_right;
        break;
    default:
        front = top_left && bottom_left;
        break;
    }
    if (front || last_kick_index == 4) {
        return GAME_TSPIN_FULL;
    }
    return GAME_TSPIN_MINI;
}

/* True when every row that is not full is empty, so removing the full rows
   leaves nothing on the board. */
static bool game_clear_leaves_board_empty(const Board *board)
{
    for (int y = 0; y < BOARD_HEIGHT; ++y) {
        if (board_row_full(board, y)) {
            continue;
        }
        for (int x = 0; x < BOARD_WIDTH; ++x) {
            if (board_get(board, x, y) != CELL_EMPTY) {
                return false;
            }
        }
    }
    return true;
}

/*
 * Scores one lock from the table and records it in last_clear. Points use the
 * level that was in force before the clear. Back-to-back multiplies only the
 * line clear points; the combo and the perfect clear are added on top.
 */
static void game_score_lock(GameState *game, int lines, GameTSpin tspin)
{
    /* A tetromino can complete at most four rows; clamping keeps a stray
       pre-filled board from reading past the tables. */
    const int index = lines > 4 ? 4 : lines;
    GameClearResult *result = &game->last_clear;
    memset(result, 0, sizeof *result);
    result->lines = (uint8_t)index;
    result->tspin = (uint8_t)tspin;

    uint64_t points = LINE_CLEAR_POINTS[tspin][index];
    if (lines > 0) {
        const bool difficult = index == 4 || tspin != GAME_TSPIN_NONE;
        if (difficult && game->back_to_back) {
            points = points * GAME_B2B_NUMERATOR / GAME_B2B_DENOMINATOR;
            result->back_to_back = true;
        }
        game->back_to_back = difficult;

        if (game->clear_streak < UINT16_MAX) {
            ++game->clear_streak;
        }
        result->combo = (uint16_t)(game->clear_streak - 1u);
        points += (uint64_t)GAME_COMBO_POINTS * result->combo;

        if (game_clear_leaves_board_empty(&game->board)) {
            result->perfect_clear = true;
            points += result->back_to_back && index == 4 ? PERFECT_CLEAR_B2B_FOUR_POINTS
                                                          : PERFECT_CLEAR_POINTS[index];
        }
    } else {
        game->clear_streak = 0;
    }

    points *= game->level;
    result->points = points > UINT32_MAX ? UINT32_MAX : (uint32_t)points;
    game->score = game_score_add(game->score, points);
}

/*
 * Writes the piece into the board, scores the lock and either waits out the
 * clear delay or spawns the next piece. A piece that locks entirely inside the
 * hidden buffer rows is a lock out and ends the game.
 */
static void game_lock_piece(GameState *game)
{
    int xs[4];
    int ys[4];
    game_piece_cells(&game->piece, xs, ys);

    /* The T's corners are never its own cells, so the rule can look at the
       board before or after the piece is written; before is clearer. */
    const GameTSpin tspin = game_tspin_kind(&game->board, &game->piece,
                                            game->last_action_was_rotation,
                                            game->last_kick_index);

    /* Snapshot for the view's lock effect, before the cells join the board. */
    game->last_locked = game->piece;

    const CellType type = piece_cell_type(game->piece.type);
    bool all_hidden = true;
    for (int i = 0; i < 4; ++i) {
        if (ys[i] < BOARD_VISIBLE_HEIGHT) {
            all_hidden = false;
        }
        board_set(&game->board, xs[i], ys[i], type);
    }

    game->events |= GAME_EVENT_LOCK;

    /*
     * Record the full rows while they are still where they were. They stay on
     * the board during the clear delay, so these indices remain valid for the
     * view until the rows are removed; at most four indices are kept.
     */
    game->cleared_row_count = 0;
    int cleared = 0;
    for (int y = 0; y < BOARD_HEIGHT; ++y) {
        if (!board_row_full(&game->board, y)) {
            continue;
        }
        if (game->cleared_row_count < 4) {
            game->cleared_rows[game->cleared_row_count] = (uint8_t)y;
            ++game->cleared_row_count;
        }
        ++cleared;
    }
    game->last_clear_count = (uint8_t)cleared;

    /* Points use the level that was in force before the clear. */
    game_score_lock(game, cleared, tspin);

    if (cleared > 0) {
        game->events |= GAME_EVENT_LINE_CLEAR;
        game->lines_cleared += (uint32_t)cleared;
        const uint32_t level = game->start_level + game->lines_cleared / 10u;
        if (game->level_up && level > game->level) {
            game->level = level;
            game->events |= GAME_EVENT_LEVEL_UP;
        }
    }

    /*
     * Reaching the line goal finishes the run on this lock, ahead of a lock
     * out: a finished run is not lost on a technicality. Like a lock out it
     * removes the full rows at once, since no piece follows them.
     */
    if (game->line_goal > 0 && game->lines_cleared >= game->line_goal) {
        (void)board_clear_full_rows(&game->board);
        game_end(game, GAME_OVER_LINE_GOAL);
        return;
    }

    /*
     * Lock out is decided at lock time, as before: the piece is entirely in the
     * hidden buffer. It wins over the clear delay, because the game ends here;
     * the completed rows are removed immediately as they used to be, since no
     * piece follows them.
     */
    if (all_hidden) {
        (void)board_clear_full_rows(&game->board);
        game_end(game, GAME_OVER_LOCK_OUT);
        return;
    }

    if (cleared > 0) {
        game->phase = GAME_PHASE_CLEARING;
        game->clear_timer = GAME_LINE_CLEAR_DELAY_TICKS;
        return;
    }

    game_spawn_next(game);
}

/*
 * Puts a piece in its spawn position with fresh per-piece counters. Does not
 * touch the hold flag: the caller decides whether this spawn spends it.
 */
static void game_place_spawned(GameState *game, PieceType type)
{
    game->piece.type = type;
    game->piece.rotation = 0;

    /* Centre the piece's box on the field. The spawn cells start at dx 0, so
       this puts 3-wide pieces in columns 3-5, I in 3-6 and O in 4-5. */
    PieceCell cells[4];
    piece_cells(type, 0, cells);
    int min_x = cells[0].x;
    int max_x = cells[0].x;
    int min_y = cells[0].y;
    for (int i = 1; i < 4; ++i) {
        if (cells[i].x < min_x) {
            min_x = cells[i].x;
        }
        if (cells[i].x > max_x) {
            max_x = cells[i].x;
        }
        if (cells[i].y < min_y) {
            min_y = cells[i].y;
        }
    }
    const int width = max_x - min_x + 1;
    game->piece.x = (int8_t)((BOARD_WIDTH - width) / 2 - min_x);
    /* Lowest cells in the first hidden row. */
    game->piece.y = (int8_t)(BOARD_VISIBLE_HEIGHT - min_y);

    game->gravity_accum = 0;
    game->lock_counter = 0;
    game->lock_resets = 0;
    game->grounded = false;
    game->shift_dir = 0;
    game->shift_counter = 0;
    game->last_kick_index = 0;
    game->last_action_was_rotation = false;
    game->lowest_y = (int8_t)game_piece_lowest_cell_y(&game->piece);
    ++game->piece_serial;
    game->events |= GAME_EVENT_SPAWN;

    if (!game_piece_fits(&game->board, &game->piece)) {
        game_end(game, GAME_OVER_BLOCK_OUT);
        return;
    }

    /* The guideline drops the piece one row right after spawn, so its lowest
       cells show up in the top visible row instead of staying hidden. */
    game_try_move(game, 0, -1);

    game->lowest_y = (int8_t)game_piece_lowest_cell_y(&game->piece);
    game->grounded = !game_can_fall(game);
}

/* Removes the front of the next queue and refills the back from the bag. */
static PieceType game_pop_next_queue(GameState *game)
{
    const PieceType type = game->next_queue[0];
    for (int i = 1; i < GAME_NEXT_COUNT; ++i) {
        game->next_queue[i - 1] = game->next_queue[i];
    }
    game->next_queue[GAME_NEXT_COUNT - 1] = bag_next(&game->bag, &game->rng_state);
    return type;
}

void game_spawn_next(GameState *game)
{
    const PieceType type = game_pop_next_queue(game);
    game->hold_used = false;
    game_place_spawned(game, type);
}

/*
 * Hold swaps the active piece with the hold slot. The first hold takes a piece
 * from the next queue; later ones swap. The incoming piece spawns exactly like
 * a fresh one, and the hold is spent until the new piece locks.
 */
static void game_step_hold(GameState *game)
{
    if (game->hold_used) {
        return;
    }

    PieceType incoming;
    if (game->has_hold) {
        incoming = game->hold_piece;
        game->hold_piece = game->piece.type;
    } else {
        incoming = game_pop_next_queue(game);
        game->has_hold = true;
        game->hold_piece = game->piece.type;
    }

    game->events |= GAME_EVENT_HOLD;
    game->hold_used = true;
    game_place_spawned(game, incoming);
}

GameConfig game_default_config(void)
{
    const GameConfig config = { 1, GAME_DAS_TICKS, GAME_ARR_TICKS, 0, 0, true };
    return config;
}

void game_init_with_config(GameState *game, uint64_t seed, const GameConfig *config)
{
    /* Zeroing everything first makes padding deterministic too, so two states
       can be compared with memcmp. */
    memset(game, 0, sizeof *game);
    rng_seed(&game->rng_state, seed);
    bag_init(&game->bag, &game->rng_state);
    game->start_level = config->start_level < 1 ? 1 : config->start_level;
    game->level = game->start_level;
    game->das_ticks = config->das_ticks;
    game->arr_ticks = config->arr_ticks;
    game->line_goal = config->line_goal;
    game->tick_limit = config->tick_limit;
    game->level_up = config->level_up;
    for (int i = 0; i < GAME_NEXT_COUNT; ++i) {
        game->next_queue[i] = bag_next(&game->bag, &game->rng_state);
    }
    game_spawn_next(game);

    /* No tick has run yet, so there is nothing to report. */
    game->events = 0;
}

void game_init_at_level(GameState *game, uint64_t seed, uint32_t start_level)
{
    GameConfig config = game_default_config();
    config.start_level = start_level;
    game_init_with_config(game, seed, &config);
}

void game_init(GameState *game, uint64_t seed)
{
    const GameConfig config = game_default_config();
    game_init_with_config(game, seed, &config);
}

void game_set_shift_timing(GameState *game, uint16_t das_ticks, uint16_t arr_ticks)
{
    game->das_ticks = das_ticks;
    game->arr_ticks = arr_ticks;
}

int game_ghost_y(const GameState *game)
{
    ActivePiece probe = game->piece;

    /*
     * Drop one row at a time while the piece still fits. The box origin may go
     * negative: the lowest cells of I and of the 3x3 pieces sit above dy 0, so
     * a piece resting on the floor has a box bottom below row 0. The loop ends
     * on its own once the lowest cell would leave the board.
     */
    for (;;) {
        ActivePiece below = probe;
        below.y = (int8_t)(below.y - 1);
        if (!game_piece_fits(&game->board, &below)) {
            break;
        }
        probe = below;
    }
    return probe.y;
}

uint16_t game_clear_progress(const GameState *game)
{
    if (game->phase != GAME_PHASE_CLEARING) {
        return 0;
    }
    return (uint16_t)(GAME_LINE_CLEAR_DELAY_TICKS - (int)game->clear_timer);
}

/* Per-tick gravity increment, including the faster soft drop when it is held. */
static uint32_t game_tick_increment(const GameState *game, bool soft_drop)
{
    const uint32_t level_increment = game_gravity_increment(game->level);
    if (!soft_drop) {
        return level_increment;
    }
    /* At most 2398490 * 20, so this cannot overflow a uint32_t. */
    const uint32_t fast = level_increment * GAME_SOFT_DROP_MULTIPLIER;
    return fast > GAME_SOFT_DROP_MIN_INCREMENT ? fast : GAME_SOFT_DROP_MIN_INCREMENT;
}

/*
 * One tick of the line-clear delay. There is no active piece, so every input is
 * ignored except a held LEFT/RIGHT, which charges DAS for the next piece. When
 * the timer runs out the recorded rows are removed and the next piece spawns;
 * the charged DAS survives that spawn.
 */
static void game_step_line_clear(GameState *game, const GameInput *input)
{
    game_step_shift_charge(game, input);

    if (game->clear_timer > 0) {
        --game->clear_timer;
    }
    if (game->clear_timer > 0) {
        return;
    }

    /*
     * Nothing changed the board during the delay, so this compaction removes
     * exactly the rows recorded at the lock.
     */
    (void)board_clear_full_rows(&game->board);
    game->phase = GAME_PHASE_FALLING;
    game->events |= GAME_EVENT_CLEAR_DONE;

    /* game_place_spawned resets the shift state; a charged DAS must survive. */
    const int8_t shift_dir = game->shift_dir;
    const uint16_t shift_counter = game->shift_counter;
    game_spawn_next(game);
    game->shift_dir = shift_dir;
    game->shift_counter = shift_counter;
}

bool game_run_completed(const GameState *game)
{
    return game->game_over && (game->game_over_reason == GAME_OVER_LINE_GOAL ||
                               game->game_over_reason == GAME_OVER_TIME_UP);
}

uint64_t game_run_ticks(const GameState *game)
{
    return game->game_over ? game->end_tick : game->tick;
}

/* One tick of the rules; game_step adds the tick limit around it. */
static void game_step_tick(GameState *game, const GameInput *input)
{
    game->events = 0;

    if (game->game_over) {
        ++game->tick;
        return;
    }

    /* A line clear in progress owns the tick: no piece falls and no piece
       exists until its timer runs out. */
    if (game->phase == GAME_PHASE_CLEARING) {
        game_step_line_clear(game, input);
        ++game->tick;
        return;
    }

    /* (2) hold, before rotation so a piece can be turned on the tick it arrives. */
    if ((input->pressed & ACTION_HOLD) != 0) {
        game_step_hold(game);
        if (game->game_over) {
            ++game->tick;
            return;
        }
    }

    /* A soft drop press restarts the gravity accumulator, so the first
       soft-dropped row does not inherit a partly elapsed gravity tick. */
    if ((input->pressed & ACTION_SOFT_DROP) != 0) {
        game->gravity_accum = 0;
    }

    /* (3) rotation; CW and CCW in the same tick cancel each other. */
    const bool rotate_cw = (input->pressed & ACTION_ROTATE_CW) != 0;
    const bool rotate_ccw = (input->pressed & ACTION_ROTATE_CCW) != 0;
    if (rotate_cw != rotate_ccw) {
        game_try_rotate(game, rotate_cw ? 1 : -1);
    }

    /* (4) horizontal movement. */
    game_step_shift(game, input);

    /* (5) hard drop: fall to the ghost row and lock in this same tick. */
    if ((input->pressed & ACTION_HARD_DROP) != 0) {
        const int from_y = game->piece.y;
        const int target_y = game_ghost_y(game);

        /* Trail data for the view: where the drop started and how far it went. */
        game->last_hard_drop_from = game->piece;
        game->last_hard_drop_rows = (uint8_t)(from_y - target_y);

        /* Dropping in place keeps a spin: the rotation is still the last
           thing that moved the piece. */
        if (target_y != from_y) {
            game->last_action_was_rotation = false;
        }
        game->piece.y = (int8_t)target_y;
        game->events |= GAME_EVENT_HARD_DROP;
        game->score = game_score_add(game->score,
                                     (uint64_t)(from_y - target_y) * 2u);
        game_lock_piece(game);
        ++game->tick;
        return;
    }

    /* (6) gravity or soft drop, whichever is faster. The accumulator is in
       1/65536 of a row, so levels that fall faster than one row per tick simply
       loop more than once. */
    const bool soft_drop = (input->held & ACTION_SOFT_DROP) != 0;
    game->gravity_accum += game_tick_increment(game, soft_drop);
    while (game->gravity_accum >= GAME_GRAVITY_ONE_ROW) {
        if (!game_can_fall(game)) {
            /* Rows are not banked while the piece is resting. */
            game->gravity_accum = GAME_GRAVITY_ONE_ROW - 1;
            break;
        }
        game->piece.y = (int8_t)(game->piece.y - 1);
        game->gravity_accum -= GAME_GRAVITY_ONE_ROW;
        game->last_action_was_rotation = false;
        if (soft_drop) {
            game->score = game_score_add(game->score, 1);
            game->events |= GAME_EVENT_SOFT_DROP_ROW;
        }
    }

    /* (7) lock delay: the counter only runs while the piece is resting, and a
       piece that leaves the ground keeps whatever it had already counted. */
    game_note_lowest(game);
    game->grounded = !game_can_fall(game);
    if (game->grounded) {
        ++game->lock_counter;
        if (game->lock_counter >= GAME_LOCK_DELAY_TICKS) {
            game_lock_piece(game);
        }
    }

    /* (8) one tick has elapsed. */
    ++game->tick;
}

void game_step(GameState *game, const GameInput *input)
{
    const bool running = !game->game_over;
    game_step_tick(game, input);

    /* The tick limit ends the run on the step that reaches it, whatever the
       phase; tick has already counted that step. */
    if (running && !game->game_over && game->tick_limit > 0 &&
        game->tick >= game->tick_limit) {
        game->game_over = true;
        game->game_over_reason = GAME_OVER_TIME_UP;
        game->events |= GAME_EVENT_GAME_OVER;
        game->end_tick = game->tick;
    }
}
