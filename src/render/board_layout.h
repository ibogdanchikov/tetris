#pragma once

#include "core/board.h"

/*
 * Playfield layout in pixels. Deliberately free of OpenGL so it can be unit
 * tested: the framebuffer is treated as a plain WxH pixel grid with the origin
 * in the bottom-left corner (the same convention the renderer uses, so board
 * row 0 sits at the bottom of the window).
 */
typedef struct BoardLayout {
    float origin_x; /* bottom-left corner of the playfield, in pixels */
    float origin_y;
    int cell_size;  /* square cell edge length, always >= 1 */
    int width;      /* playfield size in pixels, BOARD_WIDTH * cell_size */
    int height;     /* playfield size in pixels, BOARD_HEIGHT * cell_size */
} BoardLayout;

/*
 * Fits the playfield into a fb_w x fb_h framebuffer with roughly one cell of
 * margin on every side, keeps cells square and floor-sized, and centres the
 * result. The playfield only fully fits when the framebuffer is at least
 * BOARD_WIDTH x BOARD_HEIGHT pixels; below that cell_size stays 1 and the
 * playfield is centred, which means it overflows evenly on both sides.
 */
BoardLayout board_layout_compute(int fb_w, int fb_h);
