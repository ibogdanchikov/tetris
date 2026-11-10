#include "render/board_view.h"

/* Classic tetromino colours, indexed by CellType. */
static const Color BOARD_VIEW_PALETTE[CELL_TYPE_COUNT] = {
    [CELL_EMPTY] = { 0.00f, 0.00f, 0.00f, 0.00f }, /* unused */
    [CELL_I] = { 0.00f, 0.94f, 0.94f, 1.00f },     /* cyan */
    [CELL_O] = { 0.94f, 0.94f, 0.00f, 1.00f },     /* yellow */
    [CELL_T] = { 0.63f, 0.13f, 0.94f, 1.00f },     /* purple */
    [CELL_S] = { 0.13f, 0.88f, 0.13f, 1.00f },     /* green */
    [CELL_Z] = { 0.94f, 0.13f, 0.13f, 1.00f },     /* red */
    [CELL_J] = { 0.13f, 0.25f, 0.94f, 1.00f },     /* blue */
    [CELL_L] = { 0.94f, 0.50f, 0.06f, 1.00f },     /* orange */
};

static const Color BOARD_VIEW_FRAME_COLOR = { 0.32f, 0.32f, 0.38f, 1.0f };
static const Color BOARD_VIEW_GRID_COLOR = { 0.13f, 0.13f, 0.16f, 1.0f };
static const Color BOARD_VIEW_EMPTY_COLOR = { 0.06f, 0.06f, 0.08f, 1.0f };

/* Inset of a cell's fill inside its grid slot, so the grid lines stay visible. */
#define BOARD_VIEW_CELL_INSET 0.06f

void board_view_draw(Renderer *renderer, const BoardLayout *layout, const Board *board, float alpha)
{
    (void)alpha; /* interpolation is meaningless while nothing moves */

    const float cell = (float)layout->cell_size;
    const float frame = cell * 0.25f;

    /* Frame behind the playfield, then the grid background the cell fills sit on. */
    renderer_push_rect(renderer, layout->origin_x - frame, layout->origin_y - frame,
                       (float)layout->width + 2.0f * frame,
                       (float)layout->height + 2.0f * frame, BOARD_VIEW_FRAME_COLOR);
    renderer_push_rect(renderer, layout->origin_x, layout->origin_y, (float)layout->width,
                       (float)layout->height, BOARD_VIEW_GRID_COLOR);

    const float inset = cell * BOARD_VIEW_CELL_INSET;
    const float inner = cell - 2.0f * inset;

    for (int y = 0; y < BOARD_HEIGHT; ++y) {
        for (int x = 0; x < BOARD_WIDTH; ++x) {
            const CellType type = board_get(board, x, y);
            const Color color = type == CELL_EMPTY ? BOARD_VIEW_EMPTY_COLOR
                                                   : BOARD_VIEW_PALETTE[type];
            const float px = layout->origin_x + (float)x * cell + inset;
            const float py = layout->origin_y + (float)y * cell + inset;
            renderer_push_rect(renderer, px, py, inner, inner, color);
        }
    }
}
