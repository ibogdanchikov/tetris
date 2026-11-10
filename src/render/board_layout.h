#pragma once

#include "core/board.h"

/*
 * Playfield layout in pixels. Deliberately free of OpenGL so it can be unit
 * tested: the framebuffer is treated as a plain WxH pixel grid with the origin
 * in the bottom-left corner (the same convention the renderer uses, so board
 * row 0 sits at the bottom of the window).
 *
 * The playfield is not centred on its own: it is the left part of a group made
 * of the playfield, a one-cell gap and the square next-piece panel on its
 * right. The whole group is centred, which keeps the two boxes from drifting
 * apart when the window aspect ratio changes.
 */
typedef struct BoardLayout {
    float origin_x; /* bottom-left corner of the playfield, in pixels */
    float origin_y;
    int cell_size;  /* square cell edge length, always >= 1 */
    int width;      /* playfield size in pixels, BOARD_WIDTH * cell_size */
    int height;     /* playfield size in pixels, BOARD_VISIBLE_HEIGHT * cell_size */

    float panel_x;  /* bottom-left corner of the next-piece panel, in pixels */
    float panel_y;
    int panel_size; /* panel edge length; a 4x4 cell square */
} BoardLayout;

/*
 * Fits the playfield and the panel into a fb_w x fb_h framebuffer with roughly
 * one cell of margin on every side, keeps cells square and floor-sized, and
 * centres the group horizontally. The panel is top-aligned with the playfield.
 *
 * The group only fully fits when the framebuffer is at least
 * BOARD_WIDTH + 1 + 4 + 2 cells wide and BOARD_VISIBLE_HEIGHT + 2 cells high;
 * below that cell_size stays 1 and the group is centred, which means it
 * overflows evenly on both sides. The hidden buffer rows are never laid out:
 * they are not drawn.
 */
BoardLayout board_layout_compute(int fb_w, int fb_h);
