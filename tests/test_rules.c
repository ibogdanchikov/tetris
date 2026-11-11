/*
 * Milestone 3 rules: SRS wall kicks, hold, the next queue, level gravity,
 * scoring, lock out and the per-tick event bits.
 */

#include <stdbool.h>
#include <string.h>

#include "core/bag.h"
#include "core/game.h"
#include "core/rng.h"
#include "test_harness.h"

static GameInput input_none(void)
{
    const GameInput input = { 0, 0 };
    return input;
}

/* A fresh press: the action is down and this tick carries its edge. */
static GameInput input_press(uint32_t actions)
{
    const GameInput input = { actions, actions };
    return input;
}

/* Already down, no edge. */
static GameInput input_hold(uint32_t actions)
{
    const GameInput input = { actions, 0 };
    return input;
}

static int lowest_cell_y(const ActivePiece *piece)
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

/*
 * Drops a piece into a known place and clears the per-piece timers. The state
 * is plain data on purpose, so tests can set it up directly.
 */
static void game_place_piece(GameState *game, PieceType type, int rotation, int x, int y)
{
    game->piece.type = type;
    game->piece.rotation = (int8_t)rotation;
    game->piece.x = (int8_t)x;
    game->piece.y = (int8_t)y;
    game->gravity_accum = 0;
    game->lock_counter = 0;
    game->lock_resets = 0;
    game->shift_dir = 0;
    game->shift_counter = 0;
    game->last_kick_index = 0;
    game->last_action_was_rotation = false;
    game->lowest_y = (int8_t)lowest_cell_y(&game->piece);

    ActivePiece below = game->piece;
    below.y = (int8_t)(below.y - 1);
    game->grounded = !game_piece_fits(&game->board, &below);
}

/* Places a piece so its lowest cells rest on the floor. */
static void game_place_piece_on_floor(GameState *game, PieceType type, int rotation, int x)
{
    PieceCell cells[4];
    piece_cells(type, rotation, cells);
    int min_dy = cells[0].y;
    for (int i = 1; i < 4; ++i) {
        if (cells[i].y < min_dy) {
            min_dy = cells[i].y;
        }
    }
    game_place_piece(game, type, rotation, x, -min_dy);
}

static void fill_row_except(Board *board, int y, int hole_x, CellType type)
{
    for (int x = 0; x < BOARD_WIDTH; ++x) {
        if (x != hole_x) {
            board_set(board, x, y, type);
        }
    }
}

/* True when the four cells of the piece are exactly the expected set. */
static void check_piece_cells(const ActivePiece *piece, const int expected_x[4],
                              const int expected_y[4])
{
    int xs[4];
    int ys[4];
    game_piece_cells(piece, xs, ys);

    for (int i = 0; i < 4; ++i) {
        bool found = false;
        for (int j = 0; j < 4; ++j) {
            if (xs[j] == expected_x[i] && ys[j] == expected_y[i]) {
                found = true;
                break;
            }
        }
        CHECK(found);
    }
}

/* --- SRS wall kicks ------------------------------------------------------ */

/*
 * A vertical I hugging the left wall cannot rotate flat in place: the flat
 * shape would stick out through column -1. SRS test 3, (-2 -> +2) on x, fits.
 */
static void test_kick_i_left_wall(void)
{
    GameState game;
    game_init(&game, 1);
    board_clear(&game.board);
    game_place_piece(&game, PIECE_I, 1, -2, 5);

    const GameInput cw = input_press(ACTION_ROTATE_CW);
    game_step(&game, &cw);

    CHECK_EQ_INT(game.piece.rotation, 2);
    CHECK_EQ_INT(game.piece.x, 0);
    CHECK_EQ_INT(game.piece.y, 5);
    CHECK_EQ_INT(game.last_kick_index, 2);
    CHECK(game.last_action_was_rotation);
    CHECK((game.events & GAME_EVENT_ROTATE) != 0);
    CHECK((game.events & GAME_EVENT_KICK) != 0);

    const int expected_x[4] = { 0, 1, 2, 3 };
    const int expected_y[4] = { 6, 6, 6, 6 };
    check_piece_cells(&game.piece, expected_x, expected_y);
}

/* The mirror case at the right wall uses SRS test 2, (-1, 0). */
static void test_kick_i_right_wall(void)
{
    GameState game;
    game_init(&game, 1);
    board_clear(&game.board);
    game_place_piece(&game, PIECE_I, 1, 7, 5);

    const GameInput cw = input_press(ACTION_ROTATE_CW);
    game_step(&game, &cw);

    CHECK_EQ_INT(game.piece.rotation, 2);
    CHECK_EQ_INT(game.piece.x, 6);
    CHECK_EQ_INT(game.piece.y, 5);
    CHECK_EQ_INT(game.last_kick_index, 1);

    const int expected_x[4] = { 6, 7, 8, 9 };
    const int expected_y[4] = { 6, 6, 6, 6 };
    check_piece_cells(&game.piece, expected_x, expected_y);
}

/*
 * Orientation check, positive y: a flat I on the floor rotating clockwise has
 * to climb two rows. Only test 5, (+1, +2), fits, and only if +y means up.
 */
static void test_kick_i_floor_climbs_up(void)
{
    GameState game;
    game_init(&game, 1);
    board_clear(&game.board);
    game_place_piece(&game, PIECE_I, 0, 3, -2);
    CHECK_EQ_INT(lowest_cell_y(&game.piece), 0);

    const GameInput cw = input_press(ACTION_ROTATE_CW);
    game_step(&game, &cw);

    CHECK_EQ_INT(game.piece.rotation, 1);
    CHECK_EQ_INT(game.piece.x, 4);
    CHECK_EQ_INT(game.piece.y, 0);
    CHECK_EQ_INT(game.last_kick_index, 4);

    const int expected_x[4] = { 6, 6, 6, 6 };
    const int expected_y[4] = { 0, 1, 2, 3 };
    check_piece_cells(&game.piece, expected_x, expected_y);
}

/*
 * Orientation check, negative y: a T under a two-cell overhang cannot rotate
 * clockwise in place (the new top cell is covered), so it takes test 4,
 * (0, -2), and drops into the notch. This is the classic T-spin kick.
 */
static void test_kick_t_spin_ish_down(void)
{
    GameState game;
    game_init(&game, 1);
    board_clear(&game.board);

    /* T state 2 at box (3, 3): bar in row 4, nub at (4, 3). */
    CHECK(board_set(&game.board, 4, 5, CELL_J));
    CHECK(board_set(&game.board, 5, 5, CELL_J));
    game_place_piece(&game, PIECE_T, 2, 3, 3);

    const GameInput cw = input_press(ACTION_ROTATE_CW);
    game_step(&game, &cw);

    CHECK_EQ_INT(game.piece.rotation, 3);
    CHECK_EQ_INT(game.piece.x, 3);
    CHECK_EQ_INT(game.piece.y, 1);
    CHECK_EQ_INT(game.last_kick_index, 3);

    const int expected_x[4] = { 4, 3, 4, 4 };
    const int expected_y[4] = { 3, 2, 2, 1 };
    check_piece_cells(&game.piece, expected_x, expected_y);
}

/* O has no kick table: it rotates in place inside a tight 2x2 hole. */
static void test_kick_o_never_kicks(void)
{
    GameState game;
    game_init(&game, 1);
    board_clear(&game.board);
    game_place_piece(&game, PIECE_O, 0, 4, 5);

    /* Wall the O in completely. */
    for (int y = 4; y <= 7; ++y) {
        for (int x = 3; x <= 6; ++x) {
            const bool own = (x == 4 || x == 5) && (y == 5 || y == 6);
            if (!own) {
                CHECK(board_set(&game.board, x, y, CELL_S));
            }
        }
    }

    const GameInput cw = input_press(ACTION_ROTATE_CW);
    game_step(&game, &cw);

    CHECK_EQ_INT(game.piece.rotation, 1);
    CHECK_EQ_INT(game.piece.x, 4);
    CHECK_EQ_INT(game.piece.y, 5);
    CHECK_EQ_INT(game.last_kick_index, 0);
    CHECK((game.events & GAME_EVENT_ROTATE) != 0);
    CHECK((game.events & GAME_EVENT_KICK) == 0);
}

/*
 * Structural check of both kick tables, through the same lookup the game uses.
 * For adjacent states A and B, trying offset i of A -> B and then offset i of
 * B -> A must undo each other, so kicks(A->B)[i] == -kicks(B->A)[i]. A pair of
 * rows swapped between the two rotation directions breaks this immediately.
 */
static void test_kick_tables_are_reversible(void)
{
    for (int type = 0; type < PIECE_COUNT; ++type) {
        for (int from = 0; from < 4; ++from) {
            /* 1 is a clockwise quarter turn, 3 counter-clockwise. */
            for (int step = 1; step <= 3; step += 2) {
                const int to = (from + step) % 4;

                PieceCell forward[5];
                PieceCell backward[5];
                const int forward_count =
                    game_kick_offsets((PieceType)type, from, to, forward);
                const int backward_count =
                    game_kick_offsets((PieceType)type, to, from, backward);

                CHECK_EQ_INT(forward_count, backward_count);
                for (int i = 0; i < forward_count; ++i) {
                    CHECK_EQ_INT(forward[i].x, -backward[i].x);
                    CHECK_EQ_INT(forward[i].y, -backward[i].y);
                }
            }
        }
    }
}

/*
 * I in state 2 lying on the floor at columns 3-6, so box (3, -1).
 *
 * CW (2 -> 3) tries 2->L: (0,0) (+2,0) (-1,0) (+2,+1) (-1,-2).
 *   (0,0) and (-1,0) leave a cell at row -1; (+2,0) does too.
 *   (+2,+1) puts the box at (5, 0): cells (6,0) (6,1) (6,2) (6,3), which fits.
 *   -> kick index 3.
 */
static void test_kick_i_from_state_2_clockwise(void)
{
    GameState game;
    game_init(&game, 1);
    board_clear(&game.board);
    game_place_piece(&game, PIECE_I, 2, 3, -1);
    CHECK_EQ_INT(lowest_cell_y(&game.piece), 0);

    const GameInput cw = input_press(ACTION_ROTATE_CW);
    game_step(&game, &cw);

    CHECK_EQ_INT(game.piece.rotation, 3);
    CHECK_EQ_INT(game.piece.x, 5);
    CHECK_EQ_INT(game.piece.y, 0);
    CHECK_EQ_INT(game.last_kick_index, 3);

    const int expected_x[4] = { 6, 6, 6, 6 };
    const int expected_y[4] = { 0, 1, 2, 3 };
    check_piece_cells(&game.piece, expected_x, expected_y);
}

/*
 * The same start, rotating the other way.
 *
 * CCW (2 -> 1) tries 2->R: (0,0) (+1,0) (-2,0) (+1,-2) (-2,+1).
 *   (0,0), (+1,0) and (-2,0) leave a cell at row -1; (+1,-2) goes below that.
 *   (-2,+1) puts the box at (1, 0): cells (3,0) (3,1) (3,2) (3,3), which fits.
 *   -> kick index 4.
 */
static void test_kick_i_from_state_2_counter_clockwise(void)
{
    GameState game;
    game_init(&game, 1);
    board_clear(&game.board);
    game_place_piece(&game, PIECE_I, 2, 3, -1);
    CHECK_EQ_INT(lowest_cell_y(&game.piece), 0);

    const GameInput ccw = input_press(ACTION_ROTATE_CCW);
    game_step(&game, &ccw);

    CHECK_EQ_INT(game.piece.rotation, 1);
    CHECK_EQ_INT(game.piece.x, 1);
    CHECK_EQ_INT(game.piece.y, 0);
    CHECK_EQ_INT(game.last_kick_index, 4);

    const int expected_x[4] = { 3, 3, 3, 3 };
    const int expected_y[4] = { 0, 1, 2, 3 };
    check_piece_cells(&game.piece, expected_x, expected_y);
}

/* Individual entries, read the way the wiki writes them. */
static void test_kick_table_entries(void)
{
    PieceCell out[5];

    /* I 2->R (counter-clockwise from state 2), fourth try: (+1, -2). */
    CHECK_EQ_INT(game_kick_offsets(PIECE_I, 2, 1, out), 5);
    CHECK_EQ_INT(out[3].x, 1);
    CHECK_EQ_INT(out[3].y, -2);

    /* JLSTZ 2->L (clockwise from state 2), third try: (+1, +1). */
    CHECK_EQ_INT(game_kick_offsets(PIECE_T, 2, 3, out), 5);
    CHECK_EQ_INT(out[2].x, 1);
    CHECK_EQ_INT(out[2].y, 1);

    /* Every rotation of every kicking piece starts with the basic rotation. */
    for (int type = 0; type < PIECE_COUNT; ++type) {
        for (int from = 0; from < 4; ++from) {
            const int count =
                game_kick_offsets((PieceType)type, from, (from + 1) % 4, out);
            CHECK(count == 1 || count == 5);
            CHECK_EQ_INT(out[0].x, 0);
            CHECK_EQ_INT(out[0].y, 0);
        }
    }

    /* O has a single identity offset, which is the whole point of its entry. */
    CHECK_EQ_INT(game_kick_offsets(PIECE_O, 2, 1, out), 1);
    CHECK_EQ_INT(out[0].x, 0);
    CHECK_EQ_INT(out[0].y, 0);

    /* A half turn is not something the game asks for, so only identity comes back. */
    CHECK_EQ_INT(game_kick_offsets(PIECE_I, 0, 2, out), 1);
}

/* A move clears the rotation flag; a rotation sets it again. */
static void test_kick_flags(void)
{
    GameState game;
    game_init(&game, 1);
    board_clear(&game.board);
    game_place_piece(&game, PIECE_T, 0, 3, 5);

    const GameInput cw = input_press(ACTION_ROTATE_CW);
    game_step(&game, &cw);
    CHECK(game.last_action_was_rotation);
    CHECK_EQ_INT(game.last_kick_index, 0);

    const GameInput left = input_press(ACTION_LEFT);
    game_step(&game, &left);
    CHECK(!game.last_action_was_rotation);
    CHECK_EQ_INT(game.piece.x, 2);
}

/* --- Hold --------------------------------------------------------------- */

static void test_hold_takes_from_next_queue(void)
{
    GameState game;
    game_init(&game, 31);

    const PieceType active = game.piece.type;
    const PieceType next0 = game.next_queue[0];
    const PieceType next1 = game.next_queue[1];
    const PieceType next2 = game.next_queue[2];

    const GameInput hold = input_press(ACTION_HOLD);
    game_step(&game, &hold);

    CHECK(game.has_hold);
    CHECK_EQ_INT(game.hold_piece, active);
    CHECK_EQ_INT(game.piece.type, next0);
    CHECK_EQ_INT(game.next_queue[0], next1);
    CHECK_EQ_INT(game.next_queue[1], next2);
    CHECK(game.hold_used);
    CHECK_EQ_INT(game.piece_serial, 2);
    CHECK_EQ_INT(game.piece.rotation, 0);
    CHECK((game.events & GAME_EVENT_HOLD) != 0);
    CHECK((game.events & GAME_EVENT_SPAWN) != 0);

    /* The incoming piece sits exactly where a fresh spawn of that type would. */
    GameState reference;
    game_init(&reference, 31);
    reference.next_queue[0] = game.piece.type;
    game_spawn_next(&reference);
    CHECK_EQ_INT(game.piece.x, reference.piece.x);
    CHECK_EQ_INT(game.piece.y, reference.piece.y);
}

static void test_hold_is_spent_until_lock(void)
{
    GameState game;
    game_init(&game, 31);
    const GameInput hold = input_press(ACTION_HOLD);

    game_step(&game, &hold);
    const PieceType active = game.piece.type;
    const PieceType held = game.hold_piece;

    /* A second hold on the same piece is ignored and reports nothing. */
    game_step(&game, &hold);
    CHECK_EQ_INT(game.piece.type, active);
    CHECK_EQ_INT(game.hold_piece, held);
    CHECK((game.events & GAME_EVENT_HOLD) == 0);

    /* Locking frees the hold again. */
    const GameInput drop = input_press(ACTION_HARD_DROP);
    game_step(&game, &drop);
    CHECK(!game.hold_used);

    const PieceType active_after_lock = game.piece.type;
    game_step(&game, &hold);
    CHECK_EQ_INT(game.piece.type, held);
    CHECK_EQ_INT(game.hold_piece, active_after_lock);
    CHECK(game.hold_used);
    CHECK((game.events & GAME_EVENT_HOLD) != 0);
}

/* A held piece comes back as a fresh spawn: rotation, gravity and lock reset. */
static void test_hold_resets_piece_state(void)
{
    GameState game;
    game_init(&game, 41);

    game.gravity_accum = 60000;
    game.lock_counter = 20;
    game.lock_resets = 7;
    game.piece.rotation = 2;

    const GameInput hold = input_press(ACTION_HOLD);
    game_step(&game, &hold);

    CHECK_EQ_INT(game.piece.rotation, 0);
    CHECK_EQ_INT(game.lock_counter, 0);
    CHECK_EQ_INT(game.lock_resets, 0);
    /* Only this tick's gravity, none of the accumulated 60000. */
    CHECK_EQ_INT(game.gravity_accum, game_gravity_increment(game.level));
}

static void test_hold_into_blocked_spawn_is_block_out(void)
{
    GameState game;
    game_init(&game, 51);

    /* Block the spawn area after the current piece is already in play. */
    for (int x = 0; x < BOARD_WIDTH; ++x) {
        board_set(&game.board, x, BOARD_VISIBLE_HEIGHT - 1, CELL_T);
        board_set(&game.board, x, BOARD_VISIBLE_HEIGHT, CELL_T);
    }

    const GameInput hold = input_press(ACTION_HOLD);
    game_step(&game, &hold);

    CHECK(game.game_over);
    CHECK_EQ_INT(game.game_over_reason, GAME_OVER_BLOCK_OUT);
}

/* --- Next queue --------------------------------------------------------- */

static void test_next_queue_matches_bag(void)
{
    GameState game;
    game_init(&game, 4242);

    uint64_t rng = 0;
    rng_seed(&rng, 4242);
    Bag bag;
    bag_init(&bag, &rng);

    CHECK_EQ_INT(game.piece.type, bag_next(&bag, &rng));
    for (int i = 0; i < GAME_NEXT_COUNT; ++i) {
        CHECK_EQ_INT(game.next_queue[i], bag_next(&bag, &rng));
    }
}

/* The spawn order, queue included, is still one permutation per seven pieces. */
static void test_spawns_follow_seven_bag(void)
{
    GameState game;
    game_init(&game, 99);

    int counts[PIECE_COUNT] = { 0 };
    const GameInput drop = input_press(ACTION_HARD_DROP);

    for (int i = 0; i < PIECE_COUNT; ++i) {
        CHECK((int)game.piece.type >= 0 && (int)game.piece.type < PIECE_COUNT);
        ++counts[game.piece.type];
        board_clear(&game.board);
        game_step(&game, &drop);
    }

    for (int type = 0; type < PIECE_COUNT; ++type) {
        CHECK_EQ_INT(counts[type], 1);
    }
}

/* --- Gravity ------------------------------------------------------------ */

static void test_gravity_table(void)
{
    /* 65536 / 60 rounded: a shade over one row per 60 ticks. */
    CHECK_EQ_INT(game_gravity_increment(1), 1092);
    CHECK_EQ_INT(game_gravity_increment(0), 1092);

    for (uint32_t level = 2; level <= GAME_MAX_GRAVITY_LEVEL; ++level) {
        CHECK(game_gravity_increment(level) >= game_gravity_increment(level - 1));
    }

    CHECK(game_gravity_increment(20) > GAME_GRAVITY_ONE_ROW);
    CHECK(game_gravity_increment(GAME_MAX_GRAVITY_LEVEL + 1) ==
          game_gravity_increment(GAME_MAX_GRAVITY_LEVEL));

    /* Soft drop is the larger of 20x level gravity and the minimum. */
    CHECK(game_gravity_increment(1) * GAME_SOFT_DROP_MULTIPLIER <
          GAME_SOFT_DROP_MIN_INCREMENT);
    CHECK(game_gravity_increment(20) * GAME_SOFT_DROP_MULTIPLIER >
          GAME_SOFT_DROP_MIN_INCREMENT);
}

/* At level 20 a piece crosses the whole board in a single tick. */
static void test_gravity_level_20_falls_in_one_tick(void)
{
    GameState game;
    game_init_at_level(&game, 7, 20);
    CHECK_EQ_INT(game.level, 20);
    CHECK_EQ_INT(game.start_level, 20);
    CHECK_EQ_INT(lowest_cell_y(&game.piece), BOARD_VISIBLE_HEIGHT - 1);

    const GameInput none = input_none();
    game_step(&game, &none);

    CHECK_EQ_INT(game.piece_serial, 1); /* not locked yet */
    CHECK_EQ_INT(lowest_cell_y(&game.piece), 0);
    CHECK_EQ_INT(game.piece.y, game_ghost_y(&game));
}

static void test_init_level_is_clamped(void)
{
    GameState game;
    game_init_at_level(&game, 3, 0);
    CHECK_EQ_INT(game.level, 1);
    CHECK_EQ_INT(game.start_level, 1);
}

static void test_soft_drop_speed_and_points(void)
{
    GameState game;
    game_init(&game, 21);
    const int start_y = game.piece.y;

    const GameInput press = input_press(ACTION_SOFT_DROP);
    const GameInput hold = input_hold(ACTION_SOFT_DROP);

    game_step(&game, &press);
    CHECK_EQ_INT(game.piece.y, start_y);
    CHECK_EQ_INT(game.score, 0);

    for (int tick = 2; tick <= 10; ++tick) {
        game_step(&game, &hold);
        CHECK_EQ_INT(game.piece.y, start_y - tick / 2);
    }

    /* Five rows, one point each. */
    CHECK_EQ_INT(game.score, 5);
    CHECK((game.events & GAME_EVENT_SOFT_DROP_ROW) != 0);
}

/* --- Scoring ------------------------------------------------------------ */

static void test_hard_drop_points(void)
{
    GameState game;
    game_init(&game, 1);
    board_clear(&game.board);
    game_place_piece(&game, PIECE_T, 0, 3, 15);

    const int from_y = game.piece.y;
    const int ghost_y = game_ghost_y(&game);
    CHECK(ghost_y < from_y);

    const GameInput drop = input_press(ACTION_HARD_DROP);
    game_step(&game, &drop);

    CHECK_EQ_INT(game.score, 2 * (from_y - ghost_y));
    CHECK((game.events & GAME_EVENT_HARD_DROP) != 0);
}

/* Drops a vertical I into a one-wide well and returns the score it added. */
static uint32_t clear_rows_with_i(GameState *game, int rows)
{
    board_clear(&game->board);
    for (int y = 0; y < rows; ++y) {
        fill_row_except(&game->board, y, BOARD_WIDTH - 1, CELL_J);
    }
    game_place_piece(game, PIECE_I, 1, BOARD_WIDTH - 3, 0);

    const uint32_t before = game->score;
    const GameInput drop = input_press(ACTION_HARD_DROP);
    game_step(game, &drop);
    return game->score - before;
}

static void test_line_clear_points_at_level_1(void)
{
    static const uint32_t expected[5] = { 0, 100, 300, 500, 800 };

    for (int rows = 1; rows <= 4; ++rows) {
        GameState game;
        game_init(&game, 1);
        const uint32_t points = clear_rows_with_i(&game, rows);

        CHECK_EQ_INT(game.lines_cleared, rows);
        CHECK_EQ_INT(game.level, 1);
        CHECK_EQ_INT(points, expected[rows]);
        CHECK_EQ_INT(game.last_clear_count, rows);
        CHECK((game.events & GAME_EVENT_LINE_CLEAR) != 0);
    }
}

static void test_line_clear_points_use_the_old_level(void)
{
    GameState game;
    game_init_at_level(&game, 1, 5);
    const uint32_t points = clear_rows_with_i(&game, 4);

    /* 800 for a tetris, times the level in force before the clear. */
    CHECK_EQ_INT(points, 800u * 5u);
    CHECK_EQ_INT(game.level, 5);
}

static void test_level_up_at_ten_lines(void)
{
    GameState game;
    game_init(&game, 1);

    /* Nine lines already banked: this tetris takes the run to ten. */
    game.lines_cleared = 6;
    game.level = 1;

    const uint32_t points = clear_rows_with_i(&game, 4);

    CHECK_EQ_INT(game.lines_cleared, 10);
    CHECK_EQ_INT(game.level, 2);
    CHECK_EQ_INT(points, 800); /* scored at level 1 */
    CHECK((game.events & GAME_EVENT_LEVEL_UP) != 0);
}

/* --- Game over ---------------------------------------------------------- */

static void test_lock_out(void)
{
    GameState game;
    game_init(&game, 1);
    board_clear(&game.board);

    /* Twenty full rows would clear; leave column 9 open so nothing does. */
    for (int y = 0; y < BOARD_VISIBLE_HEIGHT; ++y) {
        fill_row_except(&game.board, y, BOARD_WIDTH - 1, CELL_J);
    }

    /* An O that cannot fall any further and sits entirely in the buffer. */
    game_place_piece(&game, PIECE_O, 0, 4, BOARD_VISIBLE_HEIGHT);

    const GameInput drop = input_press(ACTION_HARD_DROP);
    game_step(&game, &drop);

    CHECK(game.game_over);
    CHECK_EQ_INT(game.game_over_reason, GAME_OVER_LOCK_OUT);
    CHECK((game.events & GAME_EVENT_LOCK) != 0);
    CHECK((game.events & GAME_EVENT_GAME_OVER) != 0);
    CHECK_EQ_INT(game.last_clear_count, 0);

    /* Nothing but the tick advances afterwards. */
    const uint32_t serial = game.piece_serial;
    const uint64_t tick = game.tick;
    const GameInput none = input_none();
    for (int i = 0; i < 5; ++i) {
        game_step(&game, &none);
    }
    CHECK_EQ_INT(game.tick, tick + 5);
    CHECK_EQ_INT(game.piece_serial, serial);
    CHECK_EQ_INT(game.events, 0);
}

/* A piece that locks partly in the visible field is not a lock out. */
static void test_partial_hidden_lock_is_not_lock_out(void)
{
    GameState game;
    game_init(&game, 1);
    board_clear(&game.board);

    for (int y = 0; y < BOARD_VISIBLE_HEIGHT - 1; ++y) {
        fill_row_except(&game.board, y, BOARD_WIDTH - 1, CELL_J);
    }

    /* The O rests on row 18 with its top half in the buffer. It sits on the
       left so the next spawn (columns 3-5) is still free. */
    game_place_piece(&game, PIECE_O, 0, 0, BOARD_VISIBLE_HEIGHT - 1);
    CHECK_EQ_INT(lowest_cell_y(&game.piece), BOARD_VISIBLE_HEIGHT - 1);

    const GameInput drop = input_press(ACTION_HARD_DROP);
    game_step(&game, &drop);

    CHECK(!game.game_over);
    CHECK_EQ_INT(game.game_over_reason, GAME_OVER_NONE);
    CHECK_EQ_INT(game.piece_serial, 2);
}

/* --- Lock delay --------------------------------------------------------- */

/*
 * Spinning an S on flat ground with kicks used to be a way to stall forever.
 * The reset budget is finite, so the piece has to lock well inside
 * GAME_MAX_LOCK_RESETS * GAME_LOCK_DELAY_TICKS plus a margin.
 */
static void test_lock_delay_cannot_be_stalled_by_spinning(void)
{
    const int limit = GAME_MAX_LOCK_RESETS * GAME_LOCK_DELAY_TICKS + 60;

    GameState game;
    game_init(&game, 1);
    board_clear(&game.board);
    game_place_piece_on_floor(&game, PIECE_S, 0, 3);

    const GameInput cw = input_press(ACTION_ROTATE_CW);
    int ticks = 0;
    while (game.piece_serial == 1 && ticks < limit) {
        game_step(&game, &cw);
        ++ticks;
    }
    CHECK_EQ_INT(game.piece_serial, 2);
    CHECK(ticks < limit);

    /* The same for a T: on flat ground its rotations need kicks too, so the
       bound is the same. */
    game_init(&game, 1);
    board_clear(&game.board);
    game_place_piece_on_floor(&game, PIECE_T, 0, 3);

    ticks = 0;
    while (game.piece_serial == 1 && ticks < limit) {
        game_step(&game, &cw);
        ++ticks;
    }
    CHECK_EQ_INT(game.piece_serial, 2);
    CHECK(ticks < limit);
}

/*
 * Sliding off a ledge leaves the ground without reaching a new lowest row, so
 * the lock counter must keep its value instead of restarting.
 */
static void test_leaving_the_ground_keeps_the_lock_counter(void)
{
    GameState game;
    game_init(&game, 1);
    board_clear(&game.board);

    /* A ten row tall, five column wide ledge on the left. */
    for (int y = 0; y < 10; ++y) {
        for (int x = 0; x < 5; ++x) {
            CHECK(board_set(&game.board, x, y, CELL_J));
        }
    }

    /* A flat I lying on top of it, fully supported. State 0 sits two rows above
       its box origin, so box y = 8 puts the bar in row 10. */
    game_place_piece(&game, PIECE_I, 0, 1, 8);
    CHECK_EQ_INT(lowest_cell_y(&game.piece), 10);
    CHECK(game.grounded);

    /* No resets left, and the timer is already part way through. */
    game.lock_resets = GAME_MAX_LOCK_RESETS;
    game.lock_counter = 20;

    const GameInput right = input_press(ACTION_RIGHT);
    const GameInput none = input_none();

    /* Each step is a press and a release so DAS does not swallow the move. */
    game_step(&game, &right); /* x = 2, still supported */
    CHECK_EQ_INT(game.piece.x, 2);
    CHECK_EQ_INT(game.lock_counter, 21); /* counted, not reset */

    game_step(&game, &none);
    game_step(&game, &right); /* x = 3 */
    game_step(&game, &none);
    game_step(&game, &right); /* x = 4 */
    game_step(&game, &none);
    CHECK_EQ_INT(game.piece.x, 4);
    CHECK_EQ_INT(game.lock_counter, 26);

    game_step(&game, &right); /* x = 5, hanging over the edge */
    CHECK_EQ_INT(game.piece.x, 5);
    CHECK(!game.grounded);
    CHECK_EQ_INT(game.lock_counter, 26); /* airborne: not reset, not advanced */
    CHECK_EQ_INT(game.piece_serial, 1);
}

/* --- Events ------------------------------------------------------------- */

static void test_events_are_per_tick(void)
{
    GameState game;
    game_init(&game, 1);
    const GameInput none = input_none();

    /* A plain move reports only a move. */
    const GameInput left = input_press(ACTION_LEFT);
    game_step(&game, &left);
    CHECK_EQ_INT(game.events, GAME_EVENT_MOVE);

    /* The next tick did nothing, so nothing is reported. */
    game_step(&game, &none);
    CHECK_EQ_INT(game.events, 0);

    /* A free rotation is a rotation without a kick. */
    const GameInput cw = input_press(ACTION_ROTATE_CW);
    game_step(&game, &cw);
    CHECK_EQ_INT(game.events, GAME_EVENT_ROTATE);

    game_step(&game, &none);
    CHECK_EQ_INT(game.events, 0);
}

static void test_events_on_hard_drop(void)
{
    GameState game;
    game_init(&game, 2);

    const GameInput drop = input_press(ACTION_HARD_DROP);
    game_step(&game, &drop);

    CHECK((game.events & GAME_EVENT_HARD_DROP) != 0);
    CHECK((game.events & GAME_EVENT_LOCK) != 0);
    CHECK((game.events & GAME_EVENT_SPAWN) != 0);

    const GameInput none = input_none();
    game_step(&game, &none);
    CHECK_EQ_INT(game.events, 0);
}

/* --- Cleared rows ------------------------------------------------------- */

static void test_cleared_rows_are_recorded_before_the_shift(void)
{
    GameState game;
    game_init(&game, 1);
    board_clear(&game.board);

    /* Non-adjacent rows 1 and 3 become full once the I fills column 9. */
    fill_row_except(&game.board, 1, BOARD_WIDTH - 1, CELL_J);
    fill_row_except(&game.board, 3, BOARD_WIDTH - 1, CELL_J);
    game_place_piece(&game, PIECE_I, 1, BOARD_WIDTH - 3, 0);

    const GameInput drop = input_press(ACTION_HARD_DROP);
    game_step(&game, &drop);

    CHECK_EQ_INT(game.last_clear_count, 2);
    CHECK_EQ_INT(game.cleared_row_count, 2);
    CHECK_EQ_INT(game.cleared_rows[0], 1);
    CHECK_EQ_INT(game.cleared_rows[1], 3);

    /* The recorded rows are still full while the clear delay runs. */
    CHECK_EQ_INT(game.phase, GAME_PHASE_CLEARING);
    CHECK(board_row_full(&game.board, 1));
    CHECK(board_row_full(&game.board, 3));

    const GameInput none = input_none();
    for (int tick = 0; tick < GAME_LINE_CLEAR_DELAY_TICKS; ++tick) {
        game_step(&game, &none);
    }
    CHECK_EQ_INT(game.phase, GAME_PHASE_FALLING);
    CHECK(!board_row_full(&game.board, 1));
    CHECK(!board_row_full(&game.board, 3));

    /* A lock that clears nothing resets the record. */
    board_clear(&game.board);
    game_step(&game, &drop);
    CHECK_EQ_INT(game.last_clear_count, 0);
    CHECK_EQ_INT(game.cleared_row_count, 0);
    CHECK_EQ_INT(game.phase, GAME_PHASE_FALLING);
}

/* --- Line clear delay --------------------------------------------------- */

/*
 * Fills the given rows except one hole in column 9 and puts a vertical I in the
 * well, hard-dropping it with input. The rows are scored on this tick but stay
 * on the board until the delay ends.
 */
static void drop_i_into_well(GameState *game, int rows, const GameInput *input)
{
    board_clear(&game->board);
    for (int y = 0; y < rows; ++y) {
        fill_row_except(&game->board, y, BOARD_WIDTH - 1, CELL_J);
    }
    game_place_piece(game, PIECE_I, 1, BOARD_WIDTH - 3, 0);
    game_step(game, input);
}

static void test_line_clear_delay_exact_length(void)
{
    GameState game;
    game_init(&game, 1);
    const GameInput drop = input_press(ACTION_HARD_DROP);
    const GameInput none = input_none();

    drop_i_into_well(&game, 4, &drop);

    /* Lock tick: score, lines and the event happen now; rows and piece stay. */
    const uint32_t serial = game.piece_serial;
    CHECK_EQ_INT(game.phase, GAME_PHASE_CLEARING);
    CHECK_EQ_INT(game.clear_timer, GAME_LINE_CLEAR_DELAY_TICKS);
    CHECK_EQ_INT(game_clear_progress(&game), 0);
    CHECK_EQ_INT(game.lines_cleared, 4);
    CHECK_EQ_INT(game.last_clear_count, 4);
    CHECK_EQ_INT(game.piece_serial, serial);
    CHECK((game.events & GAME_EVENT_HARD_DROP) != 0);
    CHECK((game.events & GAME_EVENT_LOCK) != 0);
    CHECK((game.events & GAME_EVENT_LINE_CLEAR) != 0);
    CHECK((game.events & GAME_EVENT_CLEAR_DONE) == 0);
    CHECK((game.events & GAME_EVENT_SPAWN) == 0);
    for (int y = 0; y < 4; ++y) {
        CHECK(board_row_full(&game.board, y));
    }

    /* Rows vanish on exactly the last tick of the delay, not before. */
    for (int tick = 1; tick < GAME_LINE_CLEAR_DELAY_TICKS; ++tick) {
        game_step(&game, &none);
        CHECK_EQ_INT(game.phase, GAME_PHASE_CLEARING);
        CHECK_EQ_INT(game.piece_serial, serial);
        CHECK_EQ_INT(game_clear_progress(&game), tick);
        CHECK_EQ_INT(game.clear_timer, GAME_LINE_CLEAR_DELAY_TICKS - tick);
        CHECK((game.events & GAME_EVENT_CLEAR_DONE) == 0);
        CHECK_EQ_INT(game.events, 0);
        for (int y = 0; y < 4; ++y) {
            CHECK(board_row_full(&game.board, y));
        }
    }

    game_step(&game, &none);

    CHECK_EQ_INT(game.phase, GAME_PHASE_FALLING);
    CHECK_EQ_INT(game.clear_timer, 0);
    CHECK_EQ_INT(game.piece_serial, serial + 1);
    CHECK((game.events & GAME_EVENT_CLEAR_DONE) != 0);
    CHECK((game.events & GAME_EVENT_SPAWN) != 0);
    for (int y = 0; y < BOARD_HEIGHT; ++y) {
        for (int x = 0; x < BOARD_WIDTH; ++x) {
            CHECK_EQ_INT(board_get(&game.board, x, y), CELL_EMPTY);
        }
    }

    /* CLEAR_DONE is an edge: it fires once. */
    game_step(&game, &none);
    CHECK_EQ_INT(game.events, 0);
    CHECK_EQ_INT(game_clear_progress(&game), 0);
}

static void test_lock_without_clear_spawns_immediately(void)
{
    GameState game;
    game_init(&game, 2);
    const uint32_t serial = game.piece_serial;
    const GameInput drop = input_press(ACTION_HARD_DROP);

    game_step(&game, &drop);

    CHECK_EQ_INT(game.phase, GAME_PHASE_FALLING);
    CHECK_EQ_INT(game.clear_timer, 0);
    CHECK_EQ_INT(game_clear_progress(&game), 0);
    CHECK_EQ_INT(game.piece_serial, serial + 1);
    CHECK((game.events & GAME_EVENT_SPAWN) != 0);
    CHECK((game.events & GAME_EVENT_LINE_CLEAR) == 0);
    CHECK((game.events & GAME_EVENT_CLEAR_DONE) == 0);
}

static void test_input_ignored_during_line_clear(void)
{
    GameState game;
    game_init(&game, 3);
    const GameInput drop = input_press(ACTION_HARD_DROP);
    drop_i_into_well(&game, 1, &drop);

    CHECK_EQ_INT(game.phase, GAME_PHASE_CLEARING);
    const uint32_t score = game.score;
    const uint32_t serial = game.piece_serial;
    const uint16_t timer = game.clear_timer;
    const ActivePiece piece = game.piece;
    const PieceType next0 = game.next_queue[0];
    const bool has_hold = game.has_hold;
    const bool hold_used = game.hold_used;

    /* Rotation, hold, hard drop and soft drop all do nothing. */
    const GameInput ignored =
        input_press(ACTION_ROTATE_CW | ACTION_HOLD | ACTION_HARD_DROP | ACTION_SOFT_DROP);
    game_step(&game, &ignored);

    CHECK_EQ_INT(game.phase, GAME_PHASE_CLEARING);
    CHECK_EQ_INT(game.score, score);
    CHECK_EQ_INT(game.piece_serial, serial);
    CHECK_EQ_INT(game.clear_timer, timer - 1);
    CHECK(memcmp(&game.piece, &piece, sizeof piece) == 0);
    CHECK_EQ_INT(game.next_queue[0], next0);
    CHECK_EQ_INT(game.has_hold, has_hold);
    CHECK_EQ_INT(game.hold_used, hold_used);
    CHECK_EQ_INT(game.events, 0);
    CHECK(board_row_full(&game.board, 0));

    /* A second hard drop still adds no points and spawns nothing. */
    const GameInput second_drop = input_press(ACTION_HARD_DROP);
    game_step(&game, &second_drop);
    CHECK_EQ_INT(game.score, score);
    CHECK_EQ_INT(game.piece_serial, serial);
    CHECK_EQ_INT(game.phase, GAME_PHASE_CLEARING);
}

static void test_held_direction_charges_das_through_the_delay(void)
{
    GameState game;
    game_init(&game, 4);

    /*
     * LEFT on the lock tick cannot move the I out of the one-wide well, but it
     * arms DAS. Holding it through the delay must charge that DAS.
     */
    const GameInput lock = input_press(ACTION_HARD_DROP | ACTION_LEFT);
    drop_i_into_well(&game, 1, &lock);

    CHECK_EQ_INT(game.phase, GAME_PHASE_CLEARING);
    CHECK_EQ_INT(game.shift_dir, -1);

    const GameInput left = input_hold(ACTION_LEFT);
    for (int tick = 0; tick < GAME_LINE_CLEAR_DELAY_TICKS; ++tick) {
        CHECK_EQ_INT(game.piece_serial, 1);
        game_step(&game, &left);
    }

    CHECK_EQ_INT(game.phase, GAME_PHASE_FALLING);
    CHECK_EQ_INT(game.piece_serial, 2);
    CHECK_EQ_INT(game.shift_dir, -1);

    /*
     * The new piece inherits the charged DAS: its first repeat comes within
     * GAME_ARR_TICKS ticks (here on the ARR grid), not after a fresh DAS wait.
     */
    const int start_x = game.piece.x;
    int moves = 0;
    for (int tick = 0; tick < GAME_ARR_TICKS; ++tick) {
        game_step(&game, &left);
        CHECK(game.piece.x == start_x || game.piece.x == start_x - 1);
        if (game.piece.x != start_x) {
            ++moves;
        }
    }
    CHECK_EQ_INT(moves, 1);
    CHECK_EQ_INT(game.piece.x, start_x - 1);
    CHECK(game_clear_progress(&game) == 0);
}

static void test_block_out_after_the_clear_delay(void)
{
    GameState game;
    game_init(&game, 5);
    const GameInput drop = input_press(ACTION_HARD_DROP);
    drop_i_into_well(&game, 1, &drop);
    CHECK_EQ_INT(game.phase, GAME_PHASE_CLEARING);

    /*
     * Block the spawn area with rows that have a hole, so they are not full and
     * survive the one-row compaction.
     */
    for (int y = BOARD_VISIBLE_HEIGHT - 1; y < BOARD_HEIGHT; ++y) {
        fill_row_except(&game.board, y, 0, CELL_T);
    }

    const GameInput none = input_none();
    for (int tick = 0; tick < GAME_LINE_CLEAR_DELAY_TICKS; ++tick) {
        game_step(&game, &none);
    }

    CHECK(game.game_over);
    CHECK_EQ_INT(game.game_over_reason, GAME_OVER_BLOCK_OUT);
    CHECK_EQ_INT(game.phase, GAME_PHASE_FALLING);
    CHECK((game.events & GAME_EVENT_CLEAR_DONE) != 0);
    CHECK((game.events & GAME_EVENT_GAME_OVER) != 0);

    /* From here on only the tick counter moves. */
    const uint32_t serial = game.piece_serial;
    game_step(&game, &none);
    CHECK_EQ_INT(game.piece_serial, serial);
    CHECK_EQ_INT(game.events, 0);
}

/*
 * A lock out is decided on the lock tick, as before, and wins over the delay.
 * The completed rows are removed at once because no piece follows them.
 */
static void test_lock_out_wins_over_the_delay(void)
{
    GameState game;
    game_init(&game, 6);
    board_clear(&game.board);

    /* Support at row 19 so the O rests fully inside the buffer rows 20 and 21. */
    for (int y = 0; y < BOARD_VISIBLE_HEIGHT; ++y) {
        fill_row_except(&game.board, y, BOARD_WIDTH - 1, CELL_J);
    }
    for (int y = BOARD_VISIBLE_HEIGHT; y < BOARD_VISIBLE_HEIGHT + 2; ++y) {
        for (int x = 0; x < BOARD_WIDTH; ++x) {
            if (x != 4 && x != 5) {
                CHECK(board_set(&game.board, x, y, CELL_J));
            }
        }
    }
    game_place_piece(&game, PIECE_O, 0, 4, BOARD_VISIBLE_HEIGHT);
    CHECK_EQ_INT(game_ghost_y(&game), BOARD_VISIBLE_HEIGHT);

    const GameInput drop = input_press(ACTION_HARD_DROP);
    game_step(&game, &drop);

    CHECK(game.game_over);
    CHECK_EQ_INT(game.game_over_reason, GAME_OVER_LOCK_OUT);
    CHECK_EQ_INT(game.phase, GAME_PHASE_FALLING);
    CHECK_EQ_INT(game.last_clear_count, 2);
    CHECK_EQ_INT(game.lines_cleared, 2);
    CHECK_EQ_INT(game.clear_timer, 0);
    CHECK((game.events & GAME_EVENT_LINE_CLEAR) != 0);
    CHECK((game.events & GAME_EVENT_CLEAR_DONE) == 0);
    CHECK((game.events & GAME_EVENT_SPAWN) == 0);
    CHECK_EQ_INT(game.piece_serial, 1);

    /* The two buffer rows are gone immediately, exactly as before the delay. */
    CHECK(!board_row_full(&game.board, BOARD_VISIBLE_HEIGHT));
    CHECK(!board_row_full(&game.board, BOARD_VISIBLE_HEIGHT + 1));
}

static void test_last_locked_and_hard_drop_are_recorded(void)
{
    GameState game;
    game_init(&game, 7);
    board_clear(&game.board);
    CHECK(board_set(&game.board, 4, 5, CELL_J));
    game_place_piece(&game, PIECE_T, 0, 3, 15);

    const ActivePiece before = game.piece;
    const int ghost_y = game_ghost_y(&game);
    CHECK(ghost_y < before.y);

    const GameInput drop = input_press(ACTION_HARD_DROP);
    game_step(&game, &drop);

    CHECK((game.events & GAME_EVENT_LOCK) != 0);
    CHECK_EQ_INT(game.last_hard_drop_rows, before.y - ghost_y);
    CHECK_EQ_INT(game.last_hard_drop_from.type, before.type);
    CHECK_EQ_INT(game.last_hard_drop_from.rotation, before.rotation);
    CHECK_EQ_INT(game.last_hard_drop_from.x, before.x);
    CHECK_EQ_INT(game.last_hard_drop_from.y, before.y);

    CHECK_EQ_INT(game.last_locked.type, before.type);
    CHECK_EQ_INT(game.last_locked.rotation, before.rotation);
    CHECK_EQ_INT(game.last_locked.x, before.x);
    CHECK_EQ_INT(game.last_locked.y, ghost_y);

    /* last_locked addresses the cells the lock wrote, in the same colour. */
    int xs[4];
    int ys[4];
    game_piece_cells(&game.last_locked, xs, ys);
    for (int i = 0; i < 4; ++i) {
        CHECK_EQ_INT(board_get(&game.board, xs[i], ys[i]), CELL_T);
    }
}

static void test_last_locked_on_a_lock_delay_lock(void)
{
    GameState game;
    game_init(&game, 8);
    board_clear(&game.board);
    game_place_piece_on_floor(&game, PIECE_T, 0, 3);
    const ActivePiece before = game.piece;

    const GameInput none = input_none();
    for (int tick = 0; tick < GAME_LOCK_DELAY_TICKS; ++tick) {
        game_step(&game, &none);
    }

    CHECK((game.events & GAME_EVENT_LOCK) != 0);
    CHECK_EQ_INT(game.last_locked.type, before.type);
    CHECK_EQ_INT(game.last_locked.rotation, before.rotation);
    CHECK_EQ_INT(game.last_locked.x, before.x);
    CHECK_EQ_INT(game.last_locked.y, before.y);
}

void test_rules_suite(void)
{
    test_kick_i_left_wall();
    test_kick_i_right_wall();
    test_kick_i_floor_climbs_up();
    test_kick_t_spin_ish_down();
    test_kick_o_never_kicks();
    test_kick_tables_are_reversible();
    test_kick_i_from_state_2_clockwise();
    test_kick_i_from_state_2_counter_clockwise();
    test_kick_table_entries();
    test_kick_flags();

    test_hold_takes_from_next_queue();
    test_hold_is_spent_until_lock();
    test_hold_resets_piece_state();
    test_hold_into_blocked_spawn_is_block_out();

    test_next_queue_matches_bag();
    test_spawns_follow_seven_bag();

    test_gravity_table();
    test_gravity_level_20_falls_in_one_tick();
    test_init_level_is_clamped();
    test_soft_drop_speed_and_points();

    test_hard_drop_points();
    test_line_clear_points_at_level_1();
    test_line_clear_points_use_the_old_level();
    test_level_up_at_ten_lines();

    test_lock_out();
    test_partial_hidden_lock_is_not_lock_out();

    test_lock_delay_cannot_be_stalled_by_spinning();
    test_leaving_the_ground_keeps_the_lock_counter();

    test_events_are_per_tick();
    test_events_on_hard_drop();

    test_cleared_rows_are_recorded_before_the_shift();

    test_line_clear_delay_exact_length();
    test_lock_without_clear_spawns_immediately();
    test_input_ignored_during_line_clear();
    test_held_direction_charges_das_through_the_delay();
    test_block_out_after_the_clear_delay();
    test_lock_out_wins_over_the_delay();
    test_last_locked_and_hard_drop_are_recorded();
    test_last_locked_on_a_lock_delay_lock();
}
