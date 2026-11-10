/*
 * Harness entry point plus the board, RNG and layout suites. The piece and
 * gameplay suites live in test_piece.c and test_game.c.
 */

#include <string.h>

#include "core/board.h"
#include "core/rng.h"
#include "render/board_layout.h"
#include "test_harness.h"

int test_failures;

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

/* Fills a whole row with one colour. */
static void fill_row(Board *board, int y, CellType type)
{
    for (int x = 0; x < BOARD_WIDTH; ++x) {
        board_set(board, x, y, type);
    }
}

static void test_board_row_full(void)
{
    Board board;
    board_clear(&board);

    CHECK(!board_row_full(&board, 0));
    fill_row(&board, 0, CELL_I);
    CHECK(board_row_full(&board, 0));
    CHECK(!board_row_full(&board, 1));

    /* One hole is enough to make a row not full. */
    board_set(&board, 4, 0, CELL_EMPTY);
    CHECK(!board_row_full(&board, 0));

    CHECK(!board_row_full(&board, -1));
    CHECK(!board_row_full(&board, BOARD_HEIGHT));
}

static void test_board_clear_no_rows(void)
{
    Board board;
    board_clear(&board);
    CHECK(board_set(&board, 2, 3, CELL_T));

    CHECK_EQ_INT(board_clear_full_rows(&board), 0);
    CHECK_EQ_INT(board_get(&board, 2, 3), CELL_T);
}

static void test_board_clear_one_row(void)
{
    Board board;
    board_clear(&board);
    fill_row(&board, 5, CELL_O);
    CHECK(board_set(&board, 0, 6, CELL_T));

    CHECK_EQ_INT(board_clear_full_rows(&board), 1);

    /* The marker above the cleared row falls into it, everything above is empty. */
    CHECK_EQ_INT(board_get(&board, 0, 5), CELL_T);
    CHECK_EQ_INT(board_get(&board, 1, 5), CELL_EMPTY);
    CHECK_EQ_INT(board_get(&board, 0, 6), CELL_EMPTY);
    CHECK_EQ_INT(board_get(&board, 0, BOARD_HEIGHT - 1), CELL_EMPTY);
}

static void test_board_clear_two_rows(void)
{
    Board board;
    board_clear(&board);
    fill_row(&board, 2, CELL_J);
    fill_row(&board, 3, CELL_L);
    CHECK(board_set(&board, 0, 4, CELL_S));

    CHECK_EQ_INT(board_clear_full_rows(&board), 2);

    CHECK_EQ_INT(board_get(&board, 0, 2), CELL_S);
    CHECK_EQ_INT(board_get(&board, 0, 3), CELL_EMPTY);
    CHECK_EQ_INT(board_get(&board, 0, 4), CELL_EMPTY);
}

static void test_board_clear_four_rows(void)
{
    Board board;
    board_clear(&board);
    for (int y = 0; y < 4; ++y) {
        fill_row(&board, y, CELL_Z);
    }
    CHECK(board_set(&board, 7, 4, CELL_I));

    CHECK_EQ_INT(board_clear_full_rows(&board), 4);

    CHECK_EQ_INT(board_get(&board, 7, 0), CELL_I);
    for (int y = 1; y < BOARD_HEIGHT; ++y) {
        CHECK_EQ_INT(board_get(&board, 7, y), CELL_EMPTY);
    }
}

static void test_board_clear_non_adjacent_rows(void)
{
    Board board;
    board_clear(&board);
    fill_row(&board, 0, CELL_I);
    fill_row(&board, 2, CELL_O);
    CHECK(board_set(&board, 0, 1, CELL_T));

    CHECK_EQ_INT(board_clear_full_rows(&board), 2);

    /* Row 1 was not full, so it survives and lands on the floor. */
    CHECK_EQ_INT(board_get(&board, 0, 0), CELL_T);
    CHECK_EQ_INT(board_get(&board, 1, 0), CELL_EMPTY);
    CHECK_EQ_INT(board_get(&board, 0, 1), CELL_EMPTY);
}

static void test_board_clear_buffer_row(void)
{
    Board board;
    board_clear(&board);
    fill_row(&board, BOARD_VISIBLE_HEIGHT, CELL_S);
    CHECK(board_set(&board, 0, BOARD_VISIBLE_HEIGHT + 1, CELL_L));

    CHECK_EQ_INT(board_clear_full_rows(&board), 1);

    /* The row above the cleared buffer row drops into the hole it left. */
    CHECK_EQ_INT(board_get(&board, 0, BOARD_VISIBLE_HEIGHT), CELL_L);
    CHECK_EQ_INT(board_get(&board, 0, BOARD_VISIBLE_HEIGHT + 1), CELL_EMPTY);
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
    CHECK_EQ_INT(layout.height, layout.cell_size * BOARD_VISIBLE_HEIGHT);

    /* Square cells: the pixel size is an exact multiple of the cell size. */
    CHECK_EQ_INT(layout.width % layout.cell_size, 0);
    CHECK_EQ_INT(layout.height % layout.cell_size, 0);

    /* The panel is a square 4x4 cells, one cell to the right of the playfield. */
    CHECK_EQ_INT(layout.panel_size, layout.cell_size * 4);
    CHECK(layout.panel_x >= layout.origin_x + (float)layout.width + (float)layout.cell_size);

    /* Roughly one cell of margin: playfield + gap + panel + two cells must fit. */
    const int group_width = layout.width + layout.cell_size + layout.panel_size;
    CHECK((float)(group_width + 2 * layout.cell_size) <= (float)fb_w);
    CHECK((float)layout.cell_size * (float)(BOARD_VISIBLE_HEIGHT + 2) <= (float)fb_h);

    /* The whole group is centred within a pixel of rounding. */
    const long centred_x = (long)(layout.origin_x * 2.0f) + group_width;
    const long centred_y = (long)(layout.origin_y * 2.0f) + layout.height;
    CHECK(centred_x - fb_w <= 1 && fb_w - centred_x <= 1);
    CHECK(centred_y - fb_h <= 1 && fb_h - centred_y <= 1);

    /* The panel is top-aligned with the playfield and inside the framebuffer. */
    CHECK(layout.panel_y >= layout.origin_y);
    CHECK(layout.panel_y + (float)layout.panel_size <= layout.origin_y + (float)layout.height);
}

static void check_layout_within_framebuffer(int fb_w, int fb_h)
{
    const BoardLayout layout = board_layout_compute(fb_w, fb_h);
    CHECK(layout.origin_x >= 0.0f);
    CHECK(layout.origin_y >= 0.0f);
    CHECK(layout.origin_x + (float)layout.width <= (float)fb_w);
    CHECK(layout.origin_y + (float)layout.height <= (float)fb_h);

    CHECK(layout.panel_x >= 0.0f);
    CHECK(layout.panel_y >= 0.0f);
    CHECK(layout.panel_x + (float)layout.panel_size <= (float)fb_w);
    CHECK(layout.panel_y + (float)layout.panel_size <= (float)fb_h);

    /* Panel and playfield must not overlap. */
    CHECK(layout.panel_x >= layout.origin_x + (float)layout.width);
}

static void test_board_layout(void)
{
    check_layout_fits(800, 600);
    check_layout_fits(1920, 1080);
    check_layout_fits(300, 1000);
    check_layout_fits(1280, 720);
    check_layout_fits(640, 480);
    check_layout_fits(420, 900);
    check_layout_fits(500, 700);

    check_layout_within_framebuffer(800, 600);
    check_layout_within_framebuffer(1920, 1080);
    check_layout_within_framebuffer(300, 1000);
    check_layout_within_framebuffer(1280, 720);
    check_layout_within_framebuffer(420, 900);
    check_layout_within_framebuffer(500, 700);

    /* A 1x1 framebuffer cannot hold a 10x20 playfield, but must stay sane. */
    const BoardLayout tiny = board_layout_compute(1, 1);
    CHECK_EQ_INT(tiny.cell_size, 1);
    CHECK_EQ_INT(tiny.width, BOARD_WIDTH);
    CHECK_EQ_INT(tiny.height, BOARD_VISIBLE_HEIGHT);
    CHECK_EQ_INT(tiny.panel_size, 4);

    /* Degenerate sizes must not divide by zero or go negative. */
    const BoardLayout zero = board_layout_compute(0, 0);
    CHECK_EQ_INT(zero.cell_size, 1);
    const BoardLayout negative = board_layout_compute(-100, -100);
    CHECK_EQ_INT(negative.cell_size, 1);
}

/*
 * The panel only fully fits from 17x22 pixels up: 10 playfield columns, a one
 * cell gap, the 4 cell panel and one cell of margin on both sides.
 */
static void test_board_layout_panel_minimum(void)
{
    const BoardLayout minimal = board_layout_compute(17, 22);
    CHECK_EQ_INT(minimal.cell_size, 1);
    CHECK_EQ_INT(minimal.panel_size, 4);
    CHECK(minimal.panel_x + (float)minimal.panel_size <= 17.0f);
    CHECK(minimal.panel_y + (float)minimal.panel_size <= 22.0f);
    CHECK(minimal.panel_x >= minimal.origin_x + (float)minimal.width + 1.0f);

    /* Below the minimum the layout may overflow, but it stays consistent. */
    for (int fb_w = 1; fb_w < 17; ++fb_w) {
        for (int fb_h = 1; fb_h < 22; ++fb_h) {
            const BoardLayout layout = board_layout_compute(fb_w, fb_h);
            CHECK_EQ_INT(layout.cell_size, 1);
            CHECK_EQ_INT(layout.panel_size, 4);
            CHECK_EQ_INT(layout.width, BOARD_WIDTH);
            CHECK(layout.panel_x >= layout.origin_x + (float)layout.width);
        }
    }
}

int main(void)
{
    test_board_clear();
    test_board_get_set_roundtrip();
    test_board_out_of_bounds();
    test_board_row_full();
    test_board_clear_no_rows();
    test_board_clear_one_row();
    test_board_clear_two_rows();
    test_board_clear_four_rows();
    test_board_clear_non_adjacent_rows();
    test_board_clear_buffer_row();
    test_rng_determinism();
    test_board_layout();
    test_board_layout_panel_minimum();
    test_piece_suite();
    test_game_suite();
    test_input_suite();

    if (test_failures != 0) {
        fprintf(stderr, "%d check(s) failed\n", test_failures);
        return test_failures;
    }
    printf("all checks passed\n");
    return 0;
}
