#include "render/board_layout.h"

/* One extra cell of margin on each side of the whole group. */
#define LAYOUT_MARGIN_CELLS 2

/* The panel is a 4x4 cell square: wide and tall enough for a flat I. */
#define LAYOUT_PANEL_CELLS 4

/* Empty cells between the playfield and the panel. */
#define LAYOUT_PANEL_GAP_CELLS 1

/* Playfield + gap + panel, in cells. */
#define LAYOUT_GROUP_CELLS \
    (BOARD_WIDTH + LAYOUT_PANEL_GAP_CELLS + LAYOUT_PANEL_CELLS)

static int min_int(int a, int b)
{
    return a < b ? a : b;
}

BoardLayout board_layout_compute(int fb_w, int fb_h)
{
    int usable_w = fb_w > 0 ? fb_w : 0;
    int usable_h = fb_h > 0 ? fb_h : 0;
    int cell = min_int(usable_w / (LAYOUT_GROUP_CELLS + LAYOUT_MARGIN_CELLS),
                       usable_h / (BOARD_VISIBLE_HEIGHT + LAYOUT_MARGIN_CELLS));

    if (cell < 1) {
        cell = 1;
    }

    BoardLayout layout;
    layout.cell_size = cell;
    layout.width = BOARD_WIDTH * cell;
    layout.height = BOARD_VISIBLE_HEIGHT * cell;
    layout.panel_size = LAYOUT_PANEL_CELLS * cell;

    /* Centre the group as a whole, then place both boxes inside it. */
    const int group_left = (usable_w - LAYOUT_GROUP_CELLS * cell) / 2;
    layout.origin_x = (float)group_left;
    layout.origin_y = (float)((usable_h - layout.height) / 2);
    layout.panel_x = (float)(group_left + layout.width + LAYOUT_PANEL_GAP_CELLS * cell);
    layout.panel_y = layout.origin_y + (float)(layout.height - layout.panel_size);
    return layout;
}
