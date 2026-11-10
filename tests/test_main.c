/*
 * Minimal assert-style test harness: no external framework, no SDL, no GL.
 * Every failed CHECK is printed with its file and line; main returns the
 * number of failures so ctest reports the run as failed.
 */

#include <stdio.h>
#include <string.h>

#include "core/board.h"
#include "core/game.h"
#include "core/rng.h"
#include "render/board_layout.h"

static int test_failures;

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);      \
            ++test_failures;                                                     \
        }                                                                        \
    } while (0)

#define CHECK_EQ_INT(actual, expected)                                           \
    do {                                                                         \
        const long check_actual = (long)(actual);                                \
        const long check_expected = (long)(expected);                            \
        if (check_actual != check_expected) {                                    \
            fprintf(stderr, "FAIL %s:%d: %s == %s (got %ld, expected %ld)\n",    \
                    __FILE__, __LINE__, #actual, #expected, check_actual,        \
                    check_expected);                                             \
            ++test_failures;                                                     \
        }                                                                        \
    } while (0)

static void test_board_clear(void)
{
    Board board;
    memset(&board, 0xAA, sizeof board);

    board_clear(&board);

    int non_empty = 0;
    for (int y = 0; y < BOARD_HEIGHT; ++y) {
        for (int x = 0; x < BOARD_WIDTH; ++x) {
            if (board.cells[y][x] != CELL_EMPTY) {
                ++non_empty;
            }
        }
    }
    CHECK_EQ_INT(non_empty, 0);
}

static void test_board_get_set_roundtrip(void)
{
    Board board;
    board_clear(&board);

    for (int type = CELL_I; type < CELL_TYPE_COUNT; ++type) {
        CHECK(board_set(&board, 3, 4, (CellType)type));
        CHECK_EQ_INT(board_get(&board, 3, 4), type);
    }

    /* A write to one cell must not disturb its neighbours. */
    CHECK(board_set(&board, 0, 0, CELL_T));
    CHECK_EQ_INT(board_get(&board, 1, 0), CELL_EMPTY);
    CHECK_EQ_INT(board_get(&board, 0, 1), CELL_EMPTY);

    /* Corners are addressable, so row 0 really is the bottom row. */
    CHECK(board_set(&board, 0, 0, CELL_L));
    CHECK(board_set(&board, BOARD_WIDTH - 1, BOARD_HEIGHT - 1, CELL_S));
    CHECK_EQ_INT(board_get(&board, 0, 0), CELL_L);
    CHECK_EQ_INT(board_get(&board, BOARD_WIDTH - 1, BOARD_HEIGHT - 1), CELL_S);
}

static void test_board_out_of_bounds(void)
{
    Board board;
    board_clear(&board);
    CHECK(board_set(&board, 5, 5, CELL_Z));

    CHECK_EQ_INT(board_get(&board, -1, 0), CELL_EMPTY);
    CHECK_EQ_INT(board_get(&board, 0, -1), CELL_EMPTY);
    CHECK_EQ_INT(board_get(&board, BOARD_WIDTH, 0), CELL_EMPTY);
    CHECK_EQ_INT(board_get(&board, 0, BOARD_HEIGHT), CELL_EMPTY);
    CHECK_EQ_INT(board_get(&board, 1000, 1000), CELL_EMPTY);

    CHECK(!board_set(&board, -1, 0, CELL_I));
    CHECK(!board_set(&board, 0, -1, CELL_I));
    CHECK(!board_set(&board, BOARD_WIDTH, 0, CELL_I));
    CHECK(!board_set(&board, 0, BOARD_HEIGHT, CELL_I));

    /* Failed writes left the board untouched. */
    CHECK_EQ_INT(board_get(&board, 5, 5), CELL_Z);
    CHECK_EQ_INT(board_get(&board, 0, 0), CELL_EMPTY);

    CHECK(board_in_bounds(0, 0));
    CHECK(board_in_bounds(BOARD_WIDTH - 1, BOARD_HEIGHT - 1));
    CHECK(!board_in_bounds(-1, 0));
    CHECK(!board_in_bounds(BOARD_WIDTH, 0));
}

static void test_game_init_is_deterministic(void)
{
    GameState first;
    GameState second;
    memset(&first, 0x11, sizeof first);
    memset(&second, 0x22, sizeof second);

    game_init(&first, 1234);
    game_init(&second, 1234);

    CHECK_EQ_INT(first.tick, 0);
    CHECK(memcmp(&first, &second, sizeof first) == 0);
}

static void test_game_init_demo_pattern(void)
{
    GameState game;
    game_init(&game, 7);

    /* The demo pattern has to show every colour at least once. */
    int seen[CELL_TYPE_COUNT] = { 0 };
    for (int y = 0; y < BOARD_HEIGHT; ++y) {
        for (int x = 0; x < BOARD_WIDTH; ++x) {
            seen[board_get(&game.board, x, y)] = 1;
        }
    }
    for (int type = CELL_I; type < CELL_TYPE_COUNT; ++type) {
        CHECK_EQ_INT(seen[type], 1);
    }
    CHECK_EQ_INT(seen[CELL_EMPTY], 1);
}

static void test_game_step_advances_ticks(void)
{
    GameState game;
    game_init(&game, 99);

    for (int i = 0; i < 250; ++i) {
        game_step(&game);
        CHECK_EQ_INT(game.tick, i + 1);
    }
}

static void test_game_step_is_deterministic(void)
{
    GameState first;
    GameState second;
    game_init(&first, 42);
    game_init(&second, 42);

    for (int i = 0; i < 500; ++i) {
        game_step(&first);
        game_step(&second);
    }

    CHECK_EQ_INT(first.tick, 500);
    CHECK(memcmp(&first, &second, sizeof first) == 0);
}

static void test_rng_determinism(void)
{
    uint64_t first_state = 0;
    uint64_t second_state = 0;
    rng_seed(&first_state, 2024);
    rng_seed(&second_state, 2024);

    int mismatches = 0;
    for (int i = 0; i < 64; ++i) {
        if (rng_next_u64(&first_state) != rng_next_u64(&second_state)) {
            ++mismatches;
        }
    }
    CHECK_EQ_INT(mismatches, 0);

    uint64_t other_state = 0;
    rng_seed(&other_state, 2025);
    CHECK(rng_next_u64(&other_state) != rng_next_u64(&first_state));

    /* Bounded draws stay inside the requested range. */
    uint64_t bounded_state = 0;
    rng_seed(&bounded_state, 5);
    int out_of_range = 0;
    for (int i = 0; i < 1000; ++i) {
        if (rng_next_bounded(&bounded_state, 7) >= 7) {
            ++out_of_range;
        }
    }
    CHECK_EQ_INT(out_of_range, 0);
}

static void check_layout_fits(int fb_w, int fb_h)
{
    const BoardLayout layout = board_layout_compute(fb_w, fb_h);

    CHECK(layout.cell_size >= 1);
    CHECK_EQ_INT(layout.width, layout.cell_size * BOARD_WIDTH);
    CHECK_EQ_INT(layout.height, layout.cell_size * BOARD_HEIGHT);

    /* Square cells: the pixel size is an exact multiple of the cell size. */
    CHECK_EQ_INT(layout.width % layout.cell_size, 0);
    CHECK_EQ_INT(layout.height % layout.cell_size, 0);

    /* Roughly one cell of margin: the playfield plus two cells must fit. */
    CHECK((float)layout.cell_size * (float)(BOARD_WIDTH + 2) <= (float)fb_w);
    CHECK((float)layout.cell_size * (float)(BOARD_HEIGHT + 2) <= (float)fb_h);

    /* Centred within a pixel of rounding. */
    const long centred_x = (long)(layout.origin_x * 2.0f) + layout.width;
    const long centred_y = (long)(layout.origin_y * 2.0f) + layout.height;
    CHECK(centred_x - fb_w <= 1 && fb_w - centred_x <= 1);
    CHECK(centred_y - fb_h <= 1 && fb_h - centred_y <= 1);
}

static void check_layout_within_framebuffer(int fb_w, int fb_h)
{
    const BoardLayout layout = board_layout_compute(fb_w, fb_h);
    CHECK(layout.origin_x >= 0.0f);
    CHECK(layout.origin_y >= 0.0f);
    CHECK(layout.origin_x + (float)layout.width <= (float)fb_w);
    CHECK(layout.origin_y + (float)layout.height <= (float)fb_h);
}

static void test_board_layout(void)
{
    check_layout_fits(800, 600);
    check_layout_fits(1920, 1080);
    check_layout_fits(300, 1000);
    check_layout_fits(1280, 720);
    check_layout_fits(640, 480);

    check_layout_within_framebuffer(800, 600);
    check_layout_within_framebuffer(1920, 1080);
    check_layout_within_framebuffer(300, 1000);
    check_layout_within_framebuffer(1280, 720);

    /* A 1x1 framebuffer cannot hold a 10x20 playfield, but must stay sane. */
    const BoardLayout tiny = board_layout_compute(1, 1);
    CHECK_EQ_INT(tiny.cell_size, 1);
    CHECK_EQ_INT(tiny.width, BOARD_WIDTH);
    CHECK_EQ_INT(tiny.height, BOARD_HEIGHT);

    /* Degenerate sizes must not divide by zero or go negative. */
    const BoardLayout zero = board_layout_compute(0, 0);
    CHECK_EQ_INT(zero.cell_size, 1);
    const BoardLayout negative = board_layout_compute(-100, -100);
    CHECK_EQ_INT(negative.cell_size, 1);
}

int main(void)
{
    test_board_clear();
    test_board_get_set_roundtrip();
    test_board_out_of_bounds();
    test_game_init_is_deterministic();
    test_game_init_demo_pattern();
    test_game_step_advances_ticks();
    test_game_step_is_deterministic();
    test_rng_determinism();
    test_board_layout();

    if (test_failures != 0) {
        fprintf(stderr, "%d check(s) failed\n", test_failures);
        return test_failures;
    }
    printf("all checks passed\n");
    return 0;
}
