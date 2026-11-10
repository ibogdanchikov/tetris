#include "render/board_layout.h"

/* One extra cell of margin on each side: +2 cells over the playfield. */
#define LAYOUT_MARGIN_CELLS 2

static int min_int(int a, int b)
{
    return a < b ? a : b;
}

BoardLayout board_layout_compute(int fb_w, int fb_h)
{
    int usable_w = fb_w > 0 ? fb_w : 0;
    int usable_h = fb_h > 0 ? fb_h : 0;
    int cell = min_int(usable_w / (BOARD_WIDTH + LAYOUT_MARGIN_CELLS),
                       usable_h / (BOARD_HEIGHT + LAYOUT_MARGIN_CELLS));

    if (cell < 1) {
        cell = 1;
    }

    BoardLayout layout;
    layout.cell_size = cell;
    layout.width = BOARD_WIDTH * cell;
    layout.height = BOARD_HEIGHT * cell;
    layout.origin_x = (float)((usable_w - layout.width) / 2);
    layout.origin_y = (float)((usable_h - layout.height) / 2);
    return layout;
}
