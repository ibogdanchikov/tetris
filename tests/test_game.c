/* Gameplay tests: spawn, movement, DAS, rotation, gravity, lock delay, clears. */

#include <stdbool.h>
#include <string.h>

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
    game->grounded = false;
    game->gravity_counter = 0;
    game->lock_counter = 0;
    game->lock_resets = 0;
    game->shift_dir = 0;
    game->shift_counter = 0;

    /* Mirror what game_step would compute, so the piece starts out grounded. */
    ActivePiece below = game->piece;
    below.y = (int8_t)(below.y - 1);
    game->grounded = !game_piece_fits(&game->board, &below);
}

/*
 * Places a piece so its lowest cells rest on the floor. The box origin is not
 * the lowest cell for every piece, so the y has to be derived from the shape.
 */
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

static void piece_bounds(const ActivePiece *piece, int *min_x, int *max_x, int *min_y, int *max_y)
{
    int xs[4];
    int ys[4];
    game_piece_cells(piece, xs, ys);

    *min_x = xs[0];
    *max_x = xs[0];
    *min_y = ys[0];
    *max_y = ys[0];
    for (int i = 1; i < 4; ++i) {
        if (xs[i] < *min_x) {
            *min_x = xs[i];
        }
        if (xs[i] > *max_x) {
            *max_x = xs[i];
        }
        if (ys[i] < *min_y) {
            *min_y = ys[i];
        }
        if (ys[i] > *max_y) {
            *max_y = ys[i];
        }
    }
}

static int count_filled_cells(const Board *board)
{
    int filled = 0;
    for (int y = 0; y < BOARD_HEIGHT; ++y) {
        for (int x = 0; x < BOARD_WIDTH; ++x) {
            if (board_get(board, x, y) != CELL_EMPTY) {
                ++filled;
            }
        }
    }
    return filled;
}

static void test_game_init_state(void)
{
    GameState first;
    GameState second;
    memset(&first, 0x11, sizeof first);
    memset(&second, 0x22, sizeof second);

    game_init(&first, 1234);
    game_init(&second, 1234);

    CHECK_EQ_INT(first.tick, 0);
    CHECK_EQ_INT(first.piece_serial, 1);
    CHECK_EQ_INT(first.piece.rotation, 0);
    CHECK_EQ_INT(first.lines_cleared, 0);
    CHECK_EQ_INT(first.last_clear_count, 0);
    CHECK(!first.topped_out);

    /* The board starts empty and the state is fully deterministic. */
    CHECK_EQ_INT(count_filled_cells(&first.board), 0);
    CHECK(memcmp(&first, &second, sizeof first) == 0);

    /* Different seeds pick different pieces eventually. */
    GameState other;
    game_init(&other, 4321);
    CHECK(memcmp(&first, &other, sizeof first) != 0);
}

static void test_game_first_piece_is_visible(void)
{
    GameState game;
    game_init(&game, 7);

    int min_x = 0;
    int max_x = 0;
    int min_y = 0;
    int max_y = 0;
    piece_bounds(&game.piece, &min_x, &max_x, &min_y, &max_y);

    /* Lowest cells in the top visible row, nothing above the hidden buffer. */
    CHECK_EQ_INT(min_y, BOARD_VISIBLE_HEIGHT - 1);
    CHECK(max_y <= BOARD_VISIBLE_HEIGHT);
    CHECK(min_x >= 0 && max_x < BOARD_WIDTH);
}

static void check_spawn_columns(PieceType type, int expected_min_x, int expected_max_x)
{
    GameState game;
    game_init(&game, 1);
    game.next_piece = type;
    game_spawn_next(&game);

    CHECK_EQ_INT(game.piece.type, type);
    CHECK_EQ_INT(game.piece.rotation, 0);

    int min_x = 0;
    int max_x = 0;
    int min_y = 0;
    int max_y = 0;
    piece_bounds(&game.piece, &min_x, &max_x, &min_y, &max_y);

    CHECK_EQ_INT(min_x, expected_min_x);
    CHECK_EQ_INT(max_x, expected_max_x);
    CHECK_EQ_INT(min_y, BOARD_VISIBLE_HEIGHT - 1);
    CHECK(!game.topped_out);
}

static void test_game_spawn_columns(void)
{
    check_spawn_columns(PIECE_I, 3, 6);
    check_spawn_columns(PIECE_O, 4, 5);
    check_spawn_columns(PIECE_T, 3, 5);
}

static void test_game_spawn_topped_out(void)
{
    GameState game;
    game_init(&game, 5);

    /* Block the first hidden row and the row below it, right where T spawns. */
    for (int x = 0; x < BOARD_WIDTH; ++x) {
        board_set(&game.board, x, BOARD_VISIBLE_HEIGHT - 1, CELL_T);
        board_set(&game.board, x, BOARD_VISIBLE_HEIGHT, CELL_T);
    }
    game.next_piece = PIECE_T;
    game_spawn_next(&game);

    CHECK(game.topped_out);
    CHECK_EQ_INT(game.piece.type, PIECE_T);

    /* From here on, steps only advance the tick counter. */
    const uint32_t serial = game.piece_serial;
    const GameInput none = input_none();
    for (int i = 0; i < 5; ++i) {
        game_step(&game, &none);
    }
    CHECK_EQ_INT(game.tick, 5);
    CHECK_EQ_INT(game.piece_serial, serial);
    CHECK_EQ_INT(game.piece.x, 3);
    CHECK_EQ_INT(board_get(&game.board, 0, BOARD_VISIBLE_HEIGHT - 1), CELL_T);
}

static void test_game_step_advances_ticks(void)
{
    GameState game;
    game_init(&game, 99);
    const GameInput none = input_none();

    for (int i = 0; i < 250; ++i) {
        game_step(&game, &none);
        CHECK_EQ_INT(game.tick, i + 1);
    }
    CHECK(!game.topped_out);
}

static void test_game_gravity(void)
{
    GameState game;
    game_init(&game, 11);
    const GameInput none = input_none();
    const int start_y = game.piece.y;

    for (int tick = 1; tick <= 130; ++tick) {
        game_step(&game, &none);
        if (tick == 59) {
            CHECK_EQ_INT(game.piece.y, start_y);
        } else if (tick == 60) {
            CHECK_EQ_INT(game.piece.y, start_y - 1);
        } else if (tick == 100) {
            CHECK_EQ_INT(game.piece.y, start_y - 1);
        } else if (tick == 119) {
            CHECK_EQ_INT(game.piece.y, start_y - 1);
        } else if (tick == 120) {
            CHECK_EQ_INT(game.piece.y, start_y - 2);
        }
    }
    CHECK_EQ_INT(game.tick, 130);
}

static void test_game_soft_drop(void)
{
    GameState game;
    game_init(&game, 21);
    const int start_y = game.piece.y;

    const GameInput press = input_press(ACTION_SOFT_DROP);
    const GameInput hold = input_hold(ACTION_SOFT_DROP);

    game_step(&game, &press);
    CHECK_EQ_INT(game.piece.y, start_y);

    for (int tick = 2; tick <= 10; ++tick) {
        game_step(&game, &hold);
        CHECK_EQ_INT(game.piece.y, start_y - tick / 2);
    }
}

/* Board cells with something in them, in arbitrary order. */
static int collect_filled_cells(const Board *board, int xs[], int ys[])
{
    int count = 0;
    for (int y = 0; y < BOARD_HEIGHT; ++y) {
        for (int x = 0; x < BOARD_WIDTH; ++x) {
            if (board_get(board, x, y) != CELL_EMPTY) {
                xs[count] = x;
                ys[count] = y;
                ++count;
            }
        }
    }
    return count;
}

/*
 * Checks that the board holds exactly the four cells of one piece, all of the
 * piece's colour, and that the lowest of them sits in row 0.
 */
static void check_piece_landed_on_floor(const Board *board, PieceType type)
{
    int xs[BOARD_WIDTH * BOARD_HEIGHT];
    int ys[BOARD_WIDTH * BOARD_HEIGHT];
    const int count = collect_filled_cells(board, xs, ys);

    CHECK_EQ_INT(count, 4);
    const CellType cell = piece_cell_type(type);
    int lowest = ys[0];
    for (int i = 0; i < count; ++i) {
        CHECK_EQ_INT(board_get(board, xs[i], ys[i]), cell);
        if (ys[i] < lowest) {
            lowest = ys[i];
        }
    }
    CHECK_EQ_INT(lowest, 0);
}

/* Places a piece high on an empty board so it has room to fall and move. */
static void game_place_high(GameState *game, PieceType type, int rotation, int x)
{
    board_clear(&game->board);
    game_place_piece(game, type, rotation, x, 15);
}

static void test_game_hard_drop(void)
{
    GameState game;
    game_init(&game, 33);
    const GameInput drop = input_press(ACTION_HARD_DROP);

    const PieceType dropped_type = game.piece.type;
    const PieceType preview = game.next_piece;
    const uint32_t serial = game.piece_serial;

    game_step(&game, &drop);

    /* It locked on the same tick and the next piece is already in play. */
    CHECK_EQ_INT(game.tick, 1);
    CHECK_EQ_INT(game.piece_serial, serial + 1);
    CHECK_EQ_INT(game.piece.type, preview);
    CHECK_EQ_INT(count_filled_cells(&game.board), 4);
    CHECK_EQ_INT(game.lines_cleared, 0);
    CHECK_EQ_INT(game.last_clear_count, 0);
    CHECK(!game.topped_out);
    check_piece_landed_on_floor(&game.board, dropped_type);
}

/*
 * Every shape, in every rotation, must hard-drop onto the floor. The box origin
 * is not the lowest cell for I and the 3x3 pieces, so a wrong ghost row leaves
 * such pieces floating above row 0.
 */
static void test_game_hard_drop_lands_on_floor(void)
{
    for (int type = 0; type < PIECE_COUNT; ++type) {
        for (int rotation = 0; rotation < 4; ++rotation) {
            GameState game;
            game_init(&game, 100);
            game_place_high(&game, (PieceType)type, rotation, 3);

            const GameInput drop = input_press(ACTION_HARD_DROP);
            game_step(&game, &drop);

            check_piece_landed_on_floor(&game.board, (PieceType)type);
        }
    }
}

/*
 * Gravity alone has to reach the same resting place as a hard drop. Comparing
 * the two boards after the lock is independent of game_ghost_y.
 */
static void test_game_gravity_and_hard_drop_agree(void)
{
    const GameInput none = input_none();
    const GameInput drop = input_press(ACTION_HARD_DROP);

    for (int type = 0; type < PIECE_COUNT; ++type) {
        for (int rotation = 0; rotation < 4; ++rotation) {
            GameState dropped;
            game_init(&dropped, 200);
            game_place_high(&dropped, (PieceType)type, rotation, 3);
            game_step(&dropped, &drop);

            GameState fallen;
            game_init(&fallen, 200);
            game_place_high(&fallen, (PieceType)type, rotation, 3);

            const uint32_t serial = fallen.piece_serial;
            int ticks = 0;
            while (fallen.piece_serial == serial && ticks < 5000) {
                game_step(&fallen, &none);
                ++ticks;
            }

            CHECK_EQ_INT(fallen.piece_serial, serial + 1);
            check_piece_landed_on_floor(&fallen.board, (PieceType)type);
            CHECK(memcmp(&fallen.board, &dropped.board, sizeof dropped.board) == 0);
        }
    }
}

/* Stacks one cell in the middle of the field and checks the exact landing. */
static void test_game_hard_drop_onto_stack(void)
{
    GameState game;
    game_init(&game, 55);
    board_clear(&game.board);
    CHECK(board_set(&game.board, 4, 5, CELL_J));

    /* T at x = 3: its nub is in column 4, so it rests on top of the blocker. */
    game_place_piece(&game, PIECE_T, 0, 3, 15);
    const GameInput drop = input_press(ACTION_HARD_DROP);
    game_step(&game, &drop);

    const int expected_x[4] = { 4, 3, 4, 5 };
    const int expected_y[4] = { 7, 6, 6, 6 };
    for (int i = 0; i < 4; ++i) {
        CHECK_EQ_INT(board_get(&game.board, expected_x[i], expected_y[i]), CELL_T);
    }
    CHECK_EQ_INT(board_get(&game.board, 4, 5), CELL_J);
    CHECK_EQ_INT(count_filled_cells(&game.board), 5);

    /* O has its lowest cells on the box bottom, so it lands one row higher. */
    board_clear(&game.board);
    CHECK(board_set(&game.board, 4, 5, CELL_J));
    game_place_piece(&game, PIECE_O, 0, 4, 15);
    game_step(&game, &drop);

    const int o_x[4] = { 4, 5, 4, 5 };
    const int o_y[4] = { 6, 6, 7, 7 };
    for (int i = 0; i < 4; ++i) {
        CHECK_EQ_INT(board_get(&game.board, o_x[i], o_y[i]), CELL_O);
    }
    CHECK_EQ_INT(board_get(&game.board, 4, 5), CELL_J);
    CHECK_EQ_INT(count_filled_cells(&game.board), 5);
}

static void test_game_walls_block_movement(void)
{
    GameState game;
    game_init(&game, 4);
    const GameInput left = input_press(ACTION_LEFT);
    const GameInput right = input_press(ACTION_RIGHT);

    /* T is three wide: at x = 0 it cannot move left, at x = 7 not right. */
    game_place_piece(&game, PIECE_T, 0, 0, 5);
    game_step(&game, &left);
    CHECK_EQ_INT(game.piece.x, 0);

    game_place_piece(&game, PIECE_T, 0, BOARD_WIDTH - 3, 5);
    game_step(&game, &right);
    CHECK_EQ_INT(game.piece.x, BOARD_WIDTH - 3);
}

/*
 * Pushes a piece until it stops at a wall and checks that its extreme cell is in
 * the edge column. Returns the final box origin, which is negative for some
 * rotations at the left wall.
 */
static int check_wall_push(PieceType type, int rotation, uint32_t action, bool expect_left)
{
    GameState game;
    game_init(&game, 7);
    game_place_high(&game, type, rotation, 3);

    const GameInput press = input_press(action);
    const GameInput hold = input_hold(action);
    game_step(&game, &press);
    for (int tick = 0; tick < 40; ++tick) {
        game_step(&game, &hold);
    }

    int xs[4];
    int ys[4];
    game_piece_cells(&game.piece, xs, ys);
    for (int i = 0; i < 4; ++i) {
        CHECK(xs[i] >= 0 && xs[i] < BOARD_WIDTH);
        CHECK(ys[i] >= 0 && ys[i] < BOARD_HEIGHT);
    }

    int edge = xs[0];
    for (int i = 1; i < 4; ++i) {
        if (expect_left ? xs[i] < edge : xs[i] > edge) {
            edge = xs[i];
        }
    }
    CHECK_EQ_INT(edge, expect_left ? 0 : BOARD_WIDTH - 1);
    CHECK(!game.topped_out);

    return game.piece.x;
}

/*
 * Every shape in every rotation must reach both walls, which also exercises
 * negative box origins (for example a vertical I at the left wall).
 */
static void test_game_walls_stop_at_the_edges(void)
{
    bool saw_negative_origin = false;
    for (int type = 0; type < PIECE_COUNT; ++type) {
        for (int rotation = 0; rotation < 4; ++rotation) {
            const int left_x = check_wall_push((PieceType)type, rotation, ACTION_LEFT, true);
            if (left_x < 0) {
                saw_negative_origin = true;
            }
            check_wall_push((PieceType)type, rotation, ACTION_RIGHT, false);
        }
    }
    CHECK(saw_negative_origin);
}

static void test_game_stack_blocks_movement(void)
{
    GameState game;
    game_init(&game, 4);
    const GameInput left = input_press(ACTION_LEFT);
    const GameInput right = input_press(ACTION_RIGHT);

    /* T at x = 3, y = 5 fills (3..5, 6) plus (4, 7). */
    game_place_piece(&game, PIECE_T, 0, 3, 5);
    CHECK(board_set(&game.board, 2, 6, CELL_J));
    game_step(&game, &left);
    CHECK_EQ_INT(game.piece.x, 3);

    game_place_piece(&game, PIECE_T, 0, 3, 5);
    CHECK(board_set(&game.board, 6, 6, CELL_J));
    game_step(&game, &right);
    CHECK_EQ_INT(game.piece.x, 3);
}

static void test_game_das_and_arr(void)
{
    GameState game;
    game_init(&game, 6);
    game_place_piece(&game, PIECE_O, 0, 4, 5);

    const GameInput press = input_press(ACTION_LEFT);
    const GameInput hold = input_hold(ACTION_LEFT);
    const GameInput none = input_none();

    /* A fresh press moves at once and arms DAS. */
    game_step(&game, &press);
    CHECK_EQ_INT(game.piece.x, 3);

    /* No repeat until DAS has elapsed (ticks 2..10). */
    for (int tick = 2; tick <= 10; ++tick) {
        game_step(&game, &hold);
        CHECK_EQ_INT(game.piece.x, 3);
    }

    /* Then one column every ARR ticks. */
    game_step(&game, &hold);
    CHECK_EQ_INT(game.piece.x, 2);
    game_step(&game, &hold);
    CHECK_EQ_INT(game.piece.x, 2);
    game_step(&game, &hold);
    CHECK_EQ_INT(game.piece.x, 1);

    /* Releasing resets, so the next press moves immediately again. */
    game_step(&game, &none);
    CHECK_EQ_INT(game.piece.x, 1);
    game_step(&game, &press);
    CHECK_EQ_INT(game.piece.x, 0);
}

static void test_game_both_directions(void)
{
    GameState game;
    game_init(&game, 6);
    game_place_piece(&game, PIECE_O, 0, 4, 5);

    const GameInput both = input_press(ACTION_LEFT | ACTION_RIGHT);
    const GameInput both_held = input_hold(ACTION_LEFT | ACTION_RIGHT);

    /* Pressing both in the same tick cancels the move. */
    game_step(&game, &both);
    CHECK_EQ_INT(game.piece.x, 4);

    /* Both still held: still no move. */
    game_step(&game, &both_held);
    CHECK_EQ_INT(game.piece.x, 4);

    /* Releasing right leaves left in charge, so it moves left. */
    const GameInput left_only = input_hold(ACTION_LEFT);
    game_step(&game, &left_only);
    CHECK_EQ_INT(game.piece.x, 3);

    /* With both down, the most recent press wins: right moves it back. */
    const GameInput right_on_top = { ACTION_LEFT | ACTION_RIGHT, ACTION_RIGHT };
    game_step(&game, &right_on_top);
    CHECK_EQ_INT(game.piece.x, 4);

    /* Both held, no new press: the last direction keeps priority, no repeat yet. */
    game_step(&game, &both_held);
    CHECK_EQ_INT(game.piece.x, 4);

    /* Left pressed while right is still down wins again. */
    const GameInput left_on_top = { ACTION_LEFT | ACTION_RIGHT, ACTION_LEFT };
    game_step(&game, &left_on_top);
    CHECK_EQ_INT(game.piece.x, 3);
}

static void test_game_rotation(void)
{
    GameState game;
    game_init(&game, 3);
    const GameInput cw = input_press(ACTION_ROTATE_CW);
    const GameInput ccw = input_press(ACTION_ROTATE_CCW);

    /* A free rotation keeps the box in place. */
    game_place_piece(&game, PIECE_T, 0, 3, 5);
    game_step(&game, &cw);
    CHECK_EQ_INT(game.piece.rotation, 1);
    CHECK_EQ_INT(game.piece.x, 3);
    CHECK_EQ_INT(game.piece.y, 5);

    game_step(&game, &ccw);
    CHECK_EQ_INT(game.piece.rotation, 0);

    /* CW and CCW in the same tick cancel. */
    const GameInput both_rotations = input_press(ACTION_ROTATE_CW | ACTION_ROTATE_CCW);
    game_step(&game, &both_rotations);
    CHECK_EQ_INT(game.piece.rotation, 0);
}

static void test_game_rotation_is_rejected_when_blocked(void)
{
    GameState game;
    game_init(&game, 3);
    const GameInput cw = input_press(ACTION_ROTATE_CW);

    /* T rotated clockwise would need (4, 5), which is occupied. */
    game_place_piece(&game, PIECE_T, 0, 3, 5);
    CHECK(board_set(&game.board, 4, 5, CELL_S));
    game_step(&game, &cw);
    CHECK_EQ_INT(game.piece.rotation, 0);
    CHECK_EQ_INT(game.piece.x, 3);
    CHECK_EQ_INT(game.piece.y, 5);
}

static void test_game_rotation_has_no_kicks(void)
{
    GameState game;
    game_init(&game, 3);
    const GameInput cw = input_press(ACTION_ROTATE_CW);

    /* Vertical I in the rightmost column: rotating flat would need column 10. */
    game_place_piece(&game, PIECE_I, 1, 7, 3);
    game_step(&game, &cw);
    CHECK_EQ_INT(game.piece.rotation, 1);
    CHECK_EQ_INT(game.piece.x, 7);
    CHECK_EQ_INT(game.piece.y, 3);
}

static void test_game_lock_delay(void)
{
    GameState game;
    game_init(&game, 8);
    game_place_piece_on_floor(&game, PIECE_T, 0, 3);
    const GameInput none = input_none();

    for (int tick = 1; tick < GAME_LOCK_DELAY_TICKS; ++tick) {
        game_step(&game, &none);
        CHECK_EQ_INT(game.piece_serial, 1);
        CHECK_EQ_INT(game.lock_counter, tick);
    }

    game_step(&game, &none);
    CHECK_EQ_INT(game.piece_serial, 2);
    CHECK_EQ_INT(game.lock_counter, 0);
}

static void test_game_move_resets_lock_delay(void)
{
    GameState game;
    game_init(&game, 8);
    game_place_piece_on_floor(&game, PIECE_T, 0, 3);
    const GameInput none = input_none();

    for (int tick = 0; tick < 20; ++tick) {
        game_step(&game, &none);
    }
    CHECK_EQ_INT(game.lock_counter, 20);

    /* A move while grounded restarts the count. */
    const GameInput left = input_press(ACTION_LEFT);
    game_step(&game, &left);
    CHECK_EQ_INT(game.piece_serial, 1);
    CHECK_EQ_INT(game.lock_resets, 1);
    CHECK_EQ_INT(game.lock_counter, 1);

    for (int tick = 0; tick < GAME_LOCK_DELAY_TICKS - 2; ++tick) {
        game_step(&game, &none);
        CHECK_EQ_INT(game.piece_serial, 1);
    }
    game_step(&game, &none);
    CHECK_EQ_INT(game.piece_serial, 2);
}

static void test_game_lock_resets_are_capped(void)
{
    GameState game;
    game_init(&game, 8);
    game_place_piece_on_floor(&game, PIECE_T, 0, 3);

    /*
     * Alternate left and right every tick. Each direction change moves the
     * piece and would reset the lock delay, but only GAME_MAX_LOCK_RESETS times.
     */
    for (int tick = 1; tick <= GAME_MAX_LOCK_RESETS + GAME_LOCK_DELAY_TICKS - 2; ++tick) {
        const uint32_t action = (tick % 2) == 1 ? ACTION_LEFT : ACTION_RIGHT;
        const GameInput press = input_press(action);
        game_step(&game, &press);
        CHECK_EQ_INT(game.piece_serial, 1);
    }
    CHECK_EQ_INT(game.lock_resets, GAME_MAX_LOCK_RESETS);
    CHECK_EQ_INT(game.lock_counter, GAME_LOCK_DELAY_TICKS - 1);

    const GameInput final_press = input_press(ACTION_LEFT);
    game_step(&game, &final_press);
    CHECK_EQ_INT(game.piece_serial, 2);
}

static void test_game_line_clear_with_i(void)
{
    GameState game;
    game_init(&game, 123);

    /* Four rows with a one-wide well in column 9. */
    board_clear(&game.board);
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < BOARD_WIDTH - 1; ++x) {
            board_set(&game.board, x, y, CELL_J);
        }
    }

    /* A vertical I above the well, dropped in. */
    game_place_piece(&game, PIECE_I, 1, 7, 10);
    CHECK_EQ_INT(game_ghost_y(&game), 0);

    const GameInput drop = input_press(ACTION_HARD_DROP);
    game_step(&game, &drop);

    CHECK_EQ_INT(game.lines_cleared, 4);
    CHECK_EQ_INT(game.last_clear_count, 4);
    CHECK_EQ_INT(count_filled_cells(&game.board), 0);
    CHECK_EQ_INT(game.piece_serial, 2);
}

static void test_game_piece_cells_helper(void)
{
    GameState game;
    game_init(&game, 2);

    /* T at (3, 5): nub on top, bar below it. */
    game_place_piece(&game, PIECE_T, 0, 3, 5);
    int xs[4];
    int ys[4];
    game_piece_cells(&game.piece, xs, ys);

    int expected_x[4] = { 4, 3, 4, 5 };
    int expected_y[4] = { 7, 6, 6, 6 };
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

typedef struct TestInputRng {
    uint64_t rng;
    uint32_t held;
} TestInputRng;

/* Pseudo-random but reproducible player: flips one action now and then. */
static GameInput test_random_input(TestInputRng *generator)
{
    const uint64_t roll = rng_next_u64(&generator->rng);
    const uint32_t previous = generator->held;
    uint32_t held = previous;
    if ((roll & 7u) == 0) {
        held ^= 1u << (uint32_t)((roll >> 3) % 6u);
    }
    generator->held = held;

    const GameInput input = { held, held & ~previous };
    return input;
}

static void test_game_replay_is_deterministic(void)
{
    GameState first;
    GameState second;
    game_init(&first, 2026);
    game_init(&second, 2026);

    TestInputRng first_generator = { 99, 0 };
    TestInputRng second_generator = { 99, 0 };

    for (int tick = 0; tick < 5000; ++tick) {
        const GameInput first_input = test_random_input(&first_generator);
        const GameInput second_input = test_random_input(&second_generator);
        CHECK(memcmp(&first_input, &second_input, sizeof first_input) == 0);
        game_step(&first, &first_input);
        game_step(&second, &second_input);
    }

    CHECK_EQ_INT(first.tick, 5000);
    CHECK(memcmp(&first, &second, sizeof first) == 0);

    /* The run must have done something, not just idled. */
    CHECK(first.piece_serial > 10);
    CHECK(first.lines_cleared > 0 || first.topped_out);
}

void test_game_suite(void)
{
    test_game_init_state();
    test_game_first_piece_is_visible();
    test_game_spawn_columns();
    test_game_spawn_topped_out();
    test_game_step_advances_ticks();
    test_game_gravity();
    test_game_soft_drop();
    test_game_hard_drop();
    test_game_hard_drop_lands_on_floor();
    test_game_gravity_and_hard_drop_agree();
    test_game_hard_drop_onto_stack();
    test_game_walls_block_movement();
    test_game_walls_stop_at_the_edges();
    test_game_stack_blocks_movement();
    test_game_das_and_arr();
    test_game_both_directions();
    test_game_rotation();
    test_game_rotation_is_rejected_when_blocked();
    test_game_rotation_has_no_kicks();
    test_game_lock_delay();
    test_game_move_resets_lock_delay();
    test_game_lock_resets_are_capped();
    test_game_line_clear_with_i();
    test_game_piece_cells_helper();
    test_game_replay_is_deterministic();
}
