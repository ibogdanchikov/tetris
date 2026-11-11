/*
 * Harness entry point plus the board, RNG and layout suites. The piece and
 * gameplay suites live in test_piece.c and test_game.c.
 */

#include <string.h>

#include "core/board.h"
#include "core/rng.h"
#include "render/board_layout.h"
#include "render/font.h"
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

static bool layout_rects_overlap(LayoutRect a, LayoutRect b)
{
    return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}

static void check_rect_within(LayoutRect rect, float fb_w, float fb_h)
{
    CHECK(rect.w > 0.0f);
    CHECK(rect.h > 0.0f);
    CHECK(rect.x >= 0.0f);
    CHECK(rect.y >= 0.0f);
    CHECK(rect.x + rect.w <= fb_w);
    CHECK(rect.y + rect.h <= fb_h);
}

/*
 * Every rect is inside the framebuffer, the group is centred, the board and the
 * two panels have a one-cell gap between them and the panel contents sit where
 * the HUD expects them. Call it only with a framebuffer at least 22x22 pixels,
 * the documented minimum for the whole group to fit.
 */
static void check_layout_fits(int fb_w, int fb_h)
{
    const BoardLayout layout = board_layout_compute(fb_w, fb_h);
    const float framebuffer_w = (float)fb_w;
    const float framebuffer_h = (float)fb_h;
    const int group_cells = 2 * LAYOUT_PANEL_CELLS + BOARD_WIDTH + 2;

    CHECK(layout.cell_size >= 1);
    CHECK_EQ_INT(layout.width, layout.cell_size * BOARD_WIDTH);
    CHECK_EQ_INT(layout.height, layout.cell_size * BOARD_VISIBLE_HEIGHT);
    CHECK_EQ_INT(layout.width % layout.cell_size, 0);
    CHECK_EQ_INT(layout.height % layout.cell_size, 0);
    CHECK_EQ_INT(layout.text_pixel, layout.cell_size / 8 > 0 ? layout.cell_size / 8 : 1);

    /* The group plus one cell of margin on each side fits both directions. */
    CHECK(group_cells * layout.cell_size + 2 * layout.cell_size <= fb_w);
    CHECK(layout.height + 2 * layout.cell_size <= fb_h);

    /* The whole group is centred within a pixel of rounding. */
    const long centred_x = (long)(layout.left_panel.x * 2.0f) + group_cells * layout.cell_size;
    const long centred_y = (long)(layout.origin_y * 2.0f) + layout.height;
    CHECK(centred_x - fb_w <= 1 && fb_w - centred_x <= 1);
    CHECK(centred_y - fb_h <= 1 && fb_h - centred_y <= 1);

    const LayoutRect board = { layout.origin_x, layout.origin_y, (float)layout.width,
                               (float)layout.height };
    check_rect_within(board, framebuffer_w, framebuffer_h);
    check_rect_within(layout.left_panel, framebuffer_w, framebuffer_h);
    check_rect_within(layout.right_panel, framebuffer_w, framebuffer_h);
    check_rect_within(layout.hold_box, framebuffer_w, framebuffer_h);
    check_rect_within(layout.stats_area, framebuffer_w, framebuffer_h);
    check_rect_within(layout.next_box, framebuffer_w, framebuffer_h);

    /* One whole cell of gap between the board and each panel. */
    CHECK(layout.origin_x - (layout.left_panel.x + layout.left_panel.w) >=
          (float)layout.cell_size - 0.01f);
    CHECK(layout.right_panel.x - (layout.origin_x + (float)layout.width) >=
          (float)layout.cell_size - 0.01f);

    CHECK(!layout_rects_overlap(board, layout.left_panel));
    CHECK(!layout_rects_overlap(board, layout.right_panel));
    CHECK(!layout_rects_overlap(layout.left_panel, layout.right_panel));

    /* Panels are four cells wide and top-aligned with the board. */
    CHECK_EQ_INT((int)layout.left_panel.w, LAYOUT_PANEL_CELLS * layout.cell_size);
    CHECK_EQ_INT((int)layout.right_panel.w, LAYOUT_PANEL_CELLS * layout.cell_size);
    CHECK(layout.left_panel.y + layout.left_panel.h <=
          layout.origin_y + (float)layout.height + 0.01f);
    CHECK(layout.right_panel.y + layout.right_panel.h <=
          layout.origin_y + (float)layout.height + 0.01f);

    /* Hold box at the top of the left panel, stats in the space below it. */
    CHECK_EQ_INT((int)layout.hold_box.w, LAYOUT_PANEL_CELLS * layout.cell_size);
    CHECK_EQ_INT((int)layout.hold_box.h, LAYOUT_HOLD_BOX_CELLS * layout.cell_size);
    CHECK(layout.hold_box.x >= layout.left_panel.x - 0.01f);
    CHECK(layout.hold_box.y >= layout.left_panel.y - 0.01f);
    CHECK(layout.hold_box.y + layout.hold_box.h <=
          layout.left_panel.y + layout.left_panel.h + 0.01f);
    CHECK(layout.stats_area.y + layout.stats_area.h <= layout.hold_box.y + 0.01f);
    CHECK(layout.stats_area.x >= layout.left_panel.x - 0.01f);
    CHECK(layout.stats_area.x + layout.stats_area.w <=
          layout.left_panel.x + layout.left_panel.w + 0.01f);

    /* Next box at the top of the right panel; slots stack downwards from it. */
    CHECK_EQ_INT((int)layout.next_box.w, LAYOUT_PANEL_CELLS * layout.cell_size);
    CHECK_EQ_INT((int)layout.next_box.h, LAYOUT_NEXT_BOX_CELLS * layout.cell_size);
    CHECK(layout.next_box.y + layout.next_box.h <=
          layout.right_panel.y + layout.right_panel.h + 0.01f);
    for (int i = 0; i < GAME_NEXT_COUNT; ++i) {
        check_rect_within(layout.next_slots[i], framebuffer_w, framebuffer_h);
        CHECK(layout.next_slots[i].x >= layout.next_box.x - 0.01f);
        CHECK(layout.next_slots[i].x + layout.next_slots[i].w <=
              layout.next_box.x + layout.next_box.w + 0.01f);
        CHECK(layout.next_slots[i].y >= layout.next_box.y - 0.01f);
        CHECK(layout.next_slots[i].y + layout.next_slots[i].h <=
              layout.next_box.y + layout.next_box.h + 0.01f);
        if (i > 0) {
            CHECK(layout.next_slots[i].y + layout.next_slots[i].h <=
                  layout.next_slots[i - 1].y + 0.01f);
        }
    }

    /*
     * The widest possible score fits the stats panel from a cell size of 16
     * pixels up (a 352x352 framebuffer): ten 5-pixel glyphs plus gaps need 59
     * pixels and a four-cell panel has 60 after padding. Below that the font
     * simply has no room, so only the shrink helper is checked.
     */
    const int pad = layout.cell_size / 8 > 0 ? layout.cell_size / 8 : 1;
    const int value_width = (int)layout.stats_area.w - 2 * pad;
    const int value_pixel =
        board_layout_fit_text_pixel(value_width, 10, layout.text_pixel);
    CHECK(value_pixel >= 1);
    if (layout.cell_size >= 16) {
        CHECK(font_text_width("4294967295", value_pixel) <= value_width);
    }

    /*
     * Previews are drawn with their own smaller cell. Four-wide pieces keep a
     * visible gap inside the four-cell boxes and two-tall pieces keep one in a
     * slot, so no preview touches a frame or the label above it.
     */
    CHECK(layout.preview_cell_size >= 1);
    CHECK(layout.preview_cell_size <= layout.cell_size);
    CHECK(4 * layout.preview_cell_size <= (int)layout.hold_box.w);
    CHECK(4 * layout.preview_cell_size <= (int)layout.next_box.w);
    CHECK(2 * layout.preview_cell_size <= (int)layout.next_slots[0].h);
    CHECK(layout.next_slots[0].y + layout.next_slots[0].h <=
          layout.next_box.y + layout.next_box.h - (float)layout.cell_size + 0.01f);

    /* From two-pixel cells up the I really has a pixel of padding per side. */
    if (layout.cell_size >= 2) {
        CHECK((int)layout.hold_box.w - 4 * layout.preview_cell_size >= 2);
        CHECK((int)layout.next_box.w - 4 * layout.preview_cell_size >= 2);
        for (int i = 0; i < GAME_NEXT_COUNT; ++i) {
            CHECK(2 * layout.preview_cell_size <= (int)layout.next_slots[i].h - 1);
        }
        CHECK(2 * layout.preview_cell_size <=
              (int)layout.hold_box.h - layout.cell_size);
    }
}

static void test_board_layout(void)
{
    /* 22x22 is the documented minimum: 20 cells plus one cell of margin. */
    check_layout_fits(22, 22);
    check_layout_fits(800, 600);
    check_layout_fits(1280, 720);
    check_layout_fits(1920, 1080);
    check_layout_fits(640, 480);
    check_layout_fits(420, 900);
    check_layout_fits(500, 700);
    check_layout_fits(300, 1000);
    check_layout_fits(3000, 300);

    /*
     * A 1x1 framebuffer cannot hold the group, but every rect must still have a
     * sane size and keep the one-cell gaps.
     */
    const BoardLayout tiny = board_layout_compute(1, 1);
    CHECK_EQ_INT(tiny.cell_size, 1);
    CHECK_EQ_INT(tiny.width, BOARD_WIDTH);
    CHECK_EQ_INT(tiny.height, BOARD_VISIBLE_HEIGHT);
    CHECK_EQ_INT(tiny.left_panel.w, LAYOUT_PANEL_CELLS);
    CHECK_EQ_INT(tiny.right_panel.w, LAYOUT_PANEL_CELLS);
    CHECK_EQ_INT(tiny.hold_box.h, LAYOUT_HOLD_BOX_CELLS);
    CHECK_EQ_INT(tiny.next_box.h, LAYOUT_NEXT_BOX_CELLS);
    CHECK_EQ_INT(tiny.preview_cell_size, 1);
    CHECK(tiny.origin_x - (tiny.left_panel.x + tiny.left_panel.w) == 1.0f);
    CHECK(tiny.right_panel.x - (tiny.origin_x + (float)tiny.width) == 1.0f);
    for (int i = 0; i < GAME_NEXT_COUNT; ++i) {
        CHECK(tiny.next_slots[i].w > 0.0f);
        CHECK(tiny.next_slots[i].h > 0.0f);
    }

    /* Degenerate sizes must not divide by zero or go negative. */
    const BoardLayout zero = board_layout_compute(0, 0);
    CHECK_EQ_INT(zero.cell_size, 1);
    const BoardLayout negative = board_layout_compute(-100, -100);
    CHECK_EQ_INT(negative.cell_size, 1);
}

/*
 * The screen shake translates the layout as a whole: every rect moves by the
 * same offset and nothing is resized, which is what keeps the HUD glued to the
 * board while the group wobbles.
 */
static void test_board_layout_translate(void)
{
    const BoardLayout base = board_layout_compute(800, 600);
    BoardLayout moved = base;
    const float dx = 7.5f;
    const float dy = -3.0f;
    board_layout_translate(&moved, dx, dy);

    CHECK(moved.origin_x == base.origin_x + dx);
    CHECK(moved.origin_y == base.origin_y + dy);
    CHECK_EQ_INT(moved.cell_size, base.cell_size);
    CHECK_EQ_INT(moved.width, base.width);
    CHECK_EQ_INT(moved.height, base.height);

    CHECK(moved.left_panel.x == base.left_panel.x + dx);
    CHECK(moved.left_panel.y == base.left_panel.y + dy);
    CHECK(moved.right_panel.x == base.right_panel.x + dx);
    CHECK(moved.right_panel.y == base.right_panel.y + dy);
    CHECK(moved.hold_box.x == base.hold_box.x + dx);
    CHECK(moved.hold_box.y == base.hold_box.y + dy);
    CHECK(moved.stats_area.x == base.stats_area.x + dx);
    CHECK(moved.stats_area.y == base.stats_area.y + dy);
    CHECK(moved.next_box.x == base.next_box.x + dx);
    CHECK(moved.next_box.y == base.next_box.y + dy);
    CHECK(moved.next_box.w == base.next_box.w);
    CHECK(moved.next_box.h == base.next_box.h);
    for (int i = 0; i < GAME_NEXT_COUNT; ++i) {
        CHECK(moved.next_slots[i].x == base.next_slots[i].x + dx);
        CHECK(moved.next_slots[i].y == base.next_slots[i].y + dy);
        CHECK(moved.next_slots[i].w == base.next_slots[i].w);
        CHECK(moved.next_slots[i].h == base.next_slots[i].h);
    }
}

static void test_layout_fit_text_pixel(void){
    CHECK_EQ_INT(board_layout_fit_text_pixel(1000, 1, 3), 3);
    CHECK_EQ_INT(board_layout_fit_text_pixel(15, 1, 3), 3);
    CHECK_EQ_INT(board_layout_fit_text_pixel(14, 1, 3), 2);
    CHECK_EQ_INT(board_layout_fit_text_pixel(0, 5, 3), 1);
    CHECK_EQ_INT(board_layout_fit_text_pixel(102, 10, 3), 1);
    CHECK_EQ_INT(board_layout_fit_text_pixel(118, 10, 3), 2);
    CHECK_EQ_INT(board_layout_fit_text_pixel(1000, 0, 7), 7);
    CHECK_EQ_INT(board_layout_fit_text_pixel(1000, 1, 0), 1);
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
    test_board_layout_translate();
    test_layout_fit_text_pixel();
    test_piece_suite();
    test_game_suite();
    test_rules_suite();
    test_input_suite();
    test_font_suite();
    test_fx_suite();
    test_audio_suite();

    if (test_failures != 0) {
        fprintf(stderr, "%d check(s) failed\n", test_failures);
        return test_failures;
    }
    printf("all checks passed\n");
    return 0;
}
