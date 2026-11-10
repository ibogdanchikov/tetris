#include "render/board_layout.h"

#include "render/font.h"

/* One extra cell of margin on each side of the whole group. */
#define LAYOUT_MARGIN_CELLS 2

/* Empty cells between a panel and the playfield. */
#define LAYOUT_GAP_CELLS 1

/* Left panel + gap + playfield + gap + right panel, in cells. */
#define LAYOUT_GROUP_CELLS \
    (LAYOUT_PANEL_CELLS + LAYOUT_GAP_CELLS + BOARD_WIDTH + LAYOUT_GAP_CELLS + \
     LAYOUT_PANEL_CELLS)

/* Rows left for the stats once the hold box and its gap are taken out. */
#define LAYOUT_STATS_CELLS \
    (BOARD_VISIBLE_HEIGHT - LAYOUT_HOLD_BOX_CELLS - LAYOUT_GAP_CELLS)

/* One row for the label, then the rest split evenly between the pieces. */
#define LAYOUT_NEXT_LABEL_CELLS 1
#define LAYOUT_NEXT_SLOT_CELLS \
    ((LAYOUT_NEXT_BOX_CELLS - LAYOUT_NEXT_LABEL_CELLS) / GAME_NEXT_COUNT)

static int min_int(int a, int b)
{
    return a < b ? a : b;
}

BoardLayout board_layout_compute(int fb_w, int fb_h)
{
    const int usable_w = fb_w > 0 ? fb_w : 0;
    const int usable_h = fb_h > 0 ? fb_h : 0;
    int cell = min_int(usable_w / (LAYOUT_GROUP_CELLS + LAYOUT_MARGIN_CELLS),
                       usable_h / (BOARD_VISIBLE_HEIGHT + LAYOUT_MARGIN_CELLS));

    if (cell < 1) {
        cell = 1;
    }

    BoardLayout layout;
    layout.cell_size = cell;
    layout.width = BOARD_WIDTH * cell;
    layout.height = BOARD_VISIBLE_HEIGHT * cell;

    /* Centre the group as a whole, then place everything inside it. */
    const int group_left = (usable_w - LAYOUT_GROUP_CELLS * cell) / 2;
    const int board_left = group_left + (LAYOUT_PANEL_CELLS + LAYOUT_GAP_CELLS) * cell;
    const int right_left = board_left + layout.width + LAYOUT_GAP_CELLS * cell;

    layout.origin_x = (float)board_left;
    layout.origin_y = (float)((usable_h - layout.height) / 2);

    layout.left_panel = (LayoutRect){ (float)group_left, layout.origin_y,
                                      (float)(LAYOUT_PANEL_CELLS * cell), (float)layout.height };
    layout.right_panel = (LayoutRect){ (float)right_left, layout.origin_y,
                                       (float)(LAYOUT_PANEL_CELLS * cell),
                                       (float)layout.height };

    /* Hold box flush with the top of the left panel, stats below it. */
    const float hold_height = (float)(LAYOUT_HOLD_BOX_CELLS * cell);
    layout.hold_box = (LayoutRect){ layout.left_panel.x,
                                    layout.left_panel.y + layout.left_panel.h - hold_height,
                                    (float)(LAYOUT_PANEL_CELLS * cell), hold_height };
    layout.stats_area = (LayoutRect){ layout.left_panel.x, layout.left_panel.y,
                                      (float)(LAYOUT_PANEL_CELLS * cell),
                                      (float)(LAYOUT_STATS_CELLS * cell) };

    /* Next box flush with the top of the right panel; slot 0 is the topmost. */
    const float next_height = (float)(LAYOUT_NEXT_BOX_CELLS * cell);
    layout.next_box = (LayoutRect){ layout.right_panel.x,
                                    layout.right_panel.y + layout.right_panel.h - next_height,
                                    (float)(LAYOUT_PANEL_CELLS * cell), next_height };

    const float slot_height = (float)(LAYOUT_NEXT_SLOT_CELLS * cell);
    for (int i = 0; i < GAME_NEXT_COUNT; ++i) {
        layout.next_slots[i] =
            (LayoutRect){ layout.next_box.x,
                          layout.next_box.y +
                              (float)((GAME_NEXT_COUNT - 1 - i) * LAYOUT_NEXT_SLOT_CELLS * cell),
                          layout.next_box.w, slot_height };
    }

    layout.text_pixel = cell / 8;
    if (layout.text_pixel < 1) {
        layout.text_pixel = 1;
    }

    /*
     * Previews use their own cell size. At three quarters of a cell a four-wide
     * piece is 3 cells across, leaving half a cell of padding on each side of
     * the four-cell box, and a two-tall piece still has room in a three-cell
     * slot with a visible gap above and below.
     */
    layout.preview_cell_size = cell * 3 / 4;
    if (layout.preview_cell_size < 1) {
        layout.preview_cell_size = 1;
    }

    return layout;
}

int board_layout_fit_text_pixel(int width, int glyph_count, int preferred)
{
    if (preferred < 1) {
        preferred = 1;
    }
    if (glyph_count <= 0) {
        return preferred;
    }

    /* Width in font pixels before the glyph pixel size scales it. */
    const int units = FONT_GLYPH_W * glyph_count + (glyph_count - 1) * FONT_GLYPH_SPACING;

    int pixel = preferred;
    while (pixel > 1 && units * pixel > width) {
        --pixel;
    }
    return pixel;
}
