#pragma once

#include "core/board.h"
#include "core/game.h"

/*
 * Layout in pixels. Deliberately free of OpenGL so it can be unit tested: the
 * framebuffer is treated as a plain WxH pixel grid with the origin in the
 * bottom-left corner, the same convention the renderer uses.
 *
 * The group, left to right, is: left panel (four cells), one cell gap, the
 * 10x20 playfield, one cell gap and the right panel (four cells). Twenty cells
 * wide plus one cell of margin on each side; twenty cells tall plus one cell of
 * margin above and below. Cells are square and integer-sized, and the whole
 * group is centred, so the panels keep their one-cell gap at every window size.
 *
 * Both panels are top-aligned with the playfield. The left one holds a 4x4 cell
 * HOLD box and the stats below it; the right one holds a 4x10 cell NEXT box with
 * one slot per queued piece.
 *
 * The HUD text is not clipped: from a 352x352 framebuffer up (a cell size of 16)
 * even a ten-digit score fits the four-cell stats panel by shrinking the value.
 * Below that the group still fits from 22x22, but ten 5-pixel glyphs have no
 * room in a four-cell panel and a very long score can run over the board.
 */

/* Width of each side panel, in cells. */
#define LAYOUT_PANEL_CELLS 4

/* The HOLD box is a four by four cell square at the top of the left panel. */
#define LAYOUT_HOLD_BOX_CELLS 4

/* The NEXT box is four cells wide and ten tall: a label row and three slots. */
#define LAYOUT_NEXT_BOX_CELLS 10

typedef struct LayoutRect {
    float x; /* bottom-left corner, in framebuffer pixels */
    float y;
    float w;
    float h;
} LayoutRect;

typedef struct BoardLayout {
    /* Playfield. */
    float origin_x;
    float origin_y;
    int cell_size; /* square cell edge length, always >= 1 */
    int width;     /* playfield size in pixels, BOARD_WIDTH * cell_size */
    int height;    /* playfield size in pixels, BOARD_VISIBLE_HEIGHT * cell_size */

    /* Side panels: LAYOUT_PANEL_CELLS wide and as tall as the playfield. */
    LayoutRect left_panel;
    LayoutRect right_panel;

    /* Left panel contents. The hold box reserves its top cell for a label. */
    LayoutRect hold_box;
    LayoutRect stats_area;

    /* Right panel contents. next_slots[0] is the next piece to spawn. */
    LayoutRect next_box;
    LayoutRect next_slots[GAME_NEXT_COUNT];

    /* Glyph pixel size for labels and values: max(1, cell_size / 8). */
    int text_pixel;

    /*
     * Cell size for the hold and next previews: three quarters of a board cell,
     * so a flat I (four cells wide) keeps about half a board cell of padding
     * inside its four-cell box instead of touching the frame. Always >= 1.
     */
    int preview_cell_size;
} BoardLayout;

/*
 * Fits the playfield and both panels into a fb_w x fb_h framebuffer with one
 * cell of margin on every side, keeps cells square and floor-sized, and centres
 * the group. The group only fully fits when the framebuffer is at least 22 cells
 * wide and 22 cells tall; below that cell_size stays 1 and the group is centred,
 * which means it overflows evenly on both sides.
 */
BoardLayout board_layout_compute(int fb_w, int fb_h);

/*
 * Largest integer glyph pixel size in [1, preferred] at which a string of
 * glyph_count characters fits in width pixels. Returns 1 when even one pixel per
 * font pixel is too wide. Long scores use this to shrink until they fit a
 * four-cell panel.
 */
int board_layout_fit_text_pixel(int width, int glyph_count, int preferred);
