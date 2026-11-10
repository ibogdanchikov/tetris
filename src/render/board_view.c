#include "render/board_view.h"

#include "core/piece.h"

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

/* The ghost is the piece colour, faded enough to read as "not there yet". */
#define BOARD_VIEW_GHOST_ALPHA 0.25f

/* Solid frame around a box is a quarter cell wide. */
#define BOARD_VIEW_FRAME_CELLS 0.25f

/* Bottom-left pixel of board cell (x, y). */
static float board_view_cell_x(const BoardLayout *layout, int x)
{
    return layout->origin_x + (float)x * (float)layout->cell_size;
}

static float board_view_cell_y(const BoardLayout *layout, int y)
{
    return layout->origin_y + (float)y * (float)layout->cell_size;
}

/* Draws one cell's fill at a pixel position, leaving room for the grid lines. */
static void board_view_push_cell(Renderer *renderer, float x, float y, float cell, Color color)
{
    const float inset = cell * BOARD_VIEW_CELL_INSET;
    const float inner = cell - 2.0f * inset;
    renderer_push_rect(renderer, x + inset, y + inset, inner, inner, color);
}

static void board_view_draw_frame(Renderer *renderer, float x, float y, int w, int h, float cell)
{
    const float frame = cell * BOARD_VIEW_FRAME_CELLS;
    renderer_push_rect(renderer, x - frame, y - frame, (float)w + 2.0f * frame,
                       (float)h + 2.0f * frame, BOARD_VIEW_FRAME_COLOR);
    renderer_push_rect(renderer, x, y, (float)w, (float)h, BOARD_VIEW_GRID_COLOR);
}

/* Grid background plus every non-empty cell; hidden buffer rows are not drawn. */
static void board_view_draw_locked(Renderer *renderer, const BoardLayout *layout,
                                   const Board *board)
{
    const float cell = (float)layout->cell_size;

    for (int y = 0; y < BOARD_VISIBLE_HEIGHT; ++y) {
        for (int x = 0; x < BOARD_WIDTH; ++x) {
            const CellType type = board_get(board, x, y);
            const Color color = type == CELL_EMPTY ? BOARD_VIEW_EMPTY_COLOR
                                                   : BOARD_VIEW_PALETTE[type];
            board_view_push_cell(renderer, board_view_cell_x(layout, x),
                                 board_view_cell_y(layout, y), cell, color);
        }
    }
}

/*
 * Faint copy of the active piece at its landing row. It is drawn before the
 * piece, and cells shared with the piece are skipped so the two never stack up
 * into a brighter blob.
 */
static void board_view_draw_ghost(Renderer *renderer, const BoardLayout *layout,
                                  const GameState *game)
{
    int active_x[4];
    int active_y[4];
    game_piece_cells(&game->piece, active_x, active_y);

    ActivePiece ghost = game->piece;
    ghost.y = (int8_t)game_ghost_y(game);

    int ghost_x[4];
    int ghost_y[4];
    game_piece_cells(&ghost, ghost_x, ghost_y);

    const Color base = BOARD_VIEW_PALETTE[piece_cell_type(game->piece.type)];
    const Color color = { base.r, base.g, base.b, BOARD_VIEW_GHOST_ALPHA };
    const float cell = (float)layout->cell_size;

    for (int i = 0; i < 4; ++i) {
        bool overlaps_piece = false;
        for (int j = 0; j < 4; ++j) {
            if (ghost_x[i] == active_x[j] && ghost_y[i] == active_y[j]) {
                overlaps_piece = true;
                break;
            }
        }
        if (overlaps_piece) {
            continue;
        }
        board_view_push_cell(renderer, board_view_cell_x(layout, ghost_x[i]),
                             board_view_cell_y(layout, ghost_y[i]), cell, color);
    }
}

static void board_view_draw_piece(Renderer *renderer, const BoardLayout *layout,
                                  const GameState *game)
{
    int xs[4];
    int ys[4];
    game_piece_cells(&game->piece, xs, ys);

    const Color color = BOARD_VIEW_PALETTE[piece_cell_type(game->piece.type)];
    const float cell = (float)layout->cell_size;

    for (int i = 0; i < 4; ++i) {
        board_view_push_cell(renderer, board_view_cell_x(layout, xs[i]),
                             board_view_cell_y(layout, ys[i]), cell, color);
    }
}

Color board_view_cell_color(CellType type)
{
    if ((unsigned)type >= (unsigned)CELL_TYPE_COUNT) {
        return BOARD_VIEW_PALETTE[CELL_EMPTY];
    }
    return BOARD_VIEW_PALETTE[type];
}

void board_view_draw_preview(Renderer *renderer, LayoutRect box, int cell_size, PieceType type,
                             Color color)
{
    PieceCell cells[4];
    piece_cells(type, 0, cells);

    int min_x = cells[0].x;
    int max_x = cells[0].x;
    int min_y = cells[0].y;
    int max_y = cells[0].y;
    for (int i = 1; i < 4; ++i) {
        if (cells[i].x < min_x) {
            min_x = cells[i].x;
        }
        if (cells[i].x > max_x) {
            max_x = cells[i].x;
        }
        if (cells[i].y < min_y) {
            min_y = cells[i].y;
        }
        if (cells[i].y > max_y) {
            max_y = cells[i].y;
        }
    }

    const float cell = (float)cell_size;
    const float left = box.x + (box.w - (float)(max_x - min_x + 1) * cell) * 0.5f;
    const float bottom = box.y + (box.h - (float)(max_y - min_y + 1) * cell) * 0.5f;

    for (int i = 0; i < 4; ++i) {
        board_view_push_cell(renderer, left + (float)(cells[i].x - min_x) * cell,
                             bottom + (float)(cells[i].y - min_y) * cell, cell, color);
    }
}

void board_view_draw(Renderer *renderer, const BoardLayout *layout, const GameState *game,
                     float alpha)
{
    (void)alpha; /* interpolation is meaningless while nothing moves */

    /* Playfield first: frame, grid and locked cells, then the falling piece. */
    board_view_draw_frame(renderer, layout->origin_x, layout->origin_y, layout->width,
                          layout->height, (float)layout->cell_size);
    board_view_draw_locked(renderer, layout, &game->board);

    /* Both the ghost and the piece may reach into the hidden buffer rows; the
       clip keeps those cells from drawing over the frame. */
    renderer_set_clip(renderer, (int)layout->origin_x, (int)layout->origin_y, layout->width,
                      layout->height);
    board_view_draw_ghost(renderer, layout, game);
    board_view_draw_piece(renderer, layout, game);
    renderer_clear_clip(renderer);
}
