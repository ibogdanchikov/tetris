#include "render/board_view.h"

#include "core/piece.h"
#include "fx/fx.h"

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

/*
 * The ghost is an outline of the piece colour with a very faint fill. The border
 * is a tenth of a cell with a two-pixel floor, so it stays readable when the
 * board shrinks to a fourteen-pixel cell.
 */
#define BOARD_VIEW_GHOST_ALPHA 0.70f
#define BOARD_VIEW_GHOST_BORDER_CELLS 0.10f
#define BOARD_VIEW_GHOST_BORDER_MIN_PX 2.0f
#define BOARD_VIEW_GHOST_FILL_ALPHA 0.10f

/* Solid frame around a box is a quarter cell wide. */
#define BOARD_VIEW_FRAME_CELLS 0.25f

/* How far the level-up pulse pushes the frame colour towards white. */
#define BOARD_VIEW_FRAME_PULSE_MAX 0.85f

/* Mixes a colour towards white, used by the clears and the frame pulse. */
static Color board_view_brighten(Color color, float amount)
{
    return (Color){ color.r + (1.0f - color.r) * amount,
                    color.g + (1.0f - color.g) * amount,
                    color.b + (1.0f - color.b) * amount, color.a };
}

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
static void board_view_push_cell_styled(Renderer *renderer, float x, float y, float cell,
                                        Color color, RectStyle style, float p0, float p1,
                                        float p2)
{
    const float inset = cell * BOARD_VIEW_CELL_INSET;
    const float inner = cell - 2.0f * inset;
    renderer_push_styled(renderer, x + inset, y + inset, inner, inner, color, style, p0, p1, p2);
}

/* Flat variant, for the empty grid cells. */
static void board_view_push_cell(Renderer *renderer, float x, float y, float cell, Color color)
{
    board_view_push_cell_styled(renderer, x, y, cell, color, RECT_STYLE_FLAT, 0.0f, 0.0f, 0.0f);
}

/* Bevel width passed to STYLE_BLOCK: 0 lets the shader use 15% of the cell. */
static void board_view_push_block(Renderer *renderer, float x, float y, float cell, Color color)
{
    board_view_push_cell_styled(renderer, x, y, cell, color, RECT_STYLE_BLOCK, 0.0f, 0.0f, 0.0f);
}

static void board_view_draw_frame(Renderer *renderer, float x, float y, int w, int h, float cell,
                                  float pulse)
{
    const float frame = cell * BOARD_VIEW_FRAME_CELLS;
    const Color frame_color = board_view_brighten(BOARD_VIEW_FRAME_COLOR,
                                                  pulse * BOARD_VIEW_FRAME_PULSE_MAX);
    renderer_push_rect(renderer, x - frame, y - frame, (float)w + 2.0f * frame,
                       (float)h + 2.0f * frame, frame_color);
    renderer_push_rect(renderer, x, y, (float)w, (float)h, BOARD_VIEW_GRID_COLOR);
}

/* True while this row is one of the full rows waiting out the clear delay. */
static bool board_view_row_clearing(const GameState *game, int y)
{
    if (game->phase != GAME_PHASE_CLEARING) {
        return false;
    }
    for (int i = 0; i < game->cleared_row_count; ++i) {
        if ((int)game->cleared_rows[i] == y) {
            return true;
        }
    }
    return false;
}

/*
 * Grid background plus every non-empty cell; hidden buffer rows are not drawn.
 * Cells of a clearing row keep their empty background here: the animated blocks
 * are drawn on top of it, so a vanished cell leaves a normal empty slot behind.
 */
static void board_view_draw_locked(Renderer *renderer, const BoardLayout *layout,
                                   const GameState *game)
{
    const float cell = (float)layout->cell_size;

    for (int y = 0; y < BOARD_VISIBLE_HEIGHT; ++y) {
        const bool clearing = board_view_row_clearing(game, y);
        for (int x = 0; x < BOARD_WIDTH; ++x) {
            const CellType type = board_get(&game->board, x, y);
            if (type == CELL_EMPTY || clearing) {
                board_view_push_cell(renderer, board_view_cell_x(layout, x),
                                     board_view_cell_y(layout, y), cell, BOARD_VIEW_EMPTY_COLOR);
            } else {
                board_view_push_block(renderer, board_view_cell_x(layout, x),
                                      board_view_cell_y(layout, y), cell,
                                      BOARD_VIEW_PALETTE[type]);
            }
        }
    }
}

/*
 * Outline of the active piece at its landing row. It is drawn before the
 * piece, and cells shared with the piece are skipped so the two never stack up
 * into a brighter blob. The faint fill keeps the shape readable without
 * pretending the cells are already locked.
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
    float border = cell * BOARD_VIEW_GHOST_BORDER_CELLS;
    if (border < BOARD_VIEW_GHOST_BORDER_MIN_PX) {
        border = BOARD_VIEW_GHOST_BORDER_MIN_PX;
    }

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
        board_view_push_cell_styled(renderer, board_view_cell_x(layout, ghost_x[i]),
                                    board_view_cell_y(layout, ghost_y[i]), cell, color,
                                    RECT_STYLE_OUTLINE, border, BOARD_VIEW_GHOST_FILL_ALPHA, 0.0f);
    }
}

/*
 * The line-clear animation: the flash picks up the row over the first quarter of
 * the delay, then each column's cells shrink away from the centre outwards. The
 * maths (including the sweep timing) is in fx_clear_visual; this only turns the
 * scaling into rectangles around each cell's centre.
 */
static void board_view_draw_clearing(Renderer *renderer, const BoardLayout *layout,
                                     const GameState *game, float alpha)
{
    const float progress =
        ((float)game_clear_progress(game) + alpha) / (float)GAME_LINE_CLEAR_DELAY_TICKS;
    FxClearVisual visual;
    fx_clear_visual(progress, &visual);

    const float cell = (float)layout->cell_size;
    const float inset = cell * BOARD_VIEW_CELL_INSET;
    const float inner = cell - 2.0f * inset;

    for (int i = 0; i < game->cleared_row_count; ++i) {
        const int y = game->cleared_rows[i];
        if (y >= BOARD_VISIBLE_HEIGHT) {
            continue;
        }
        for (int x = 0; x < BOARD_WIDTH; ++x) {
            const float visible = visual.visible[x];
            const CellType type = board_get(&game->board, x, y);
            if (type == CELL_EMPTY || visible <= 0.0f) {
                continue;
            }

            const float size = inner * visible;
            const float centre_x = board_view_cell_x(layout, x) + cell * 0.5f;
            const float centre_y = board_view_cell_y(layout, y) + cell * 0.5f;
            renderer_push_styled(renderer, centre_x - size * 0.5f, centre_y - size * 0.5f,
                                 size, size,
                                 board_view_brighten(BOARD_VIEW_PALETTE[type], visual.flash),
                                 RECT_STYLE_BLOCK, 0.0f, 0.0f, 0.0f);
        }
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
        board_view_push_block(renderer, board_view_cell_x(layout, xs[i]),
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
        board_view_push_block(renderer, left + (float)(cells[i].x - min_x) * cell,
                              bottom + (float)(cells[i].y - min_y) * cell, cell, color);
    }
}

void board_view_draw(Renderer *renderer, const BoardLayout *layout, const GameState *game,
                     float alpha, float frame_pulse)
{
    /* Playfield first: frame, grid and locked cells, then the falling piece. */
    board_view_draw_frame(renderer, layout->origin_x, layout->origin_y, layout->width,
                          layout->height, (float)layout->cell_size, frame_pulse);
    board_view_draw_locked(renderer, layout, game);

    /* While rows are clearing they are still on the board, and there is no
       active piece to draw until the delay ends. */
    if (game->phase == GAME_PHASE_CLEARING) {
        board_view_draw_clearing(renderer, layout, game, alpha);
        return;
    }

    /* Both the ghost and the piece may reach into the hidden buffer rows; the
       clip keeps those cells from drawing over the frame. */
    renderer_set_clip(renderer, (int)layout->origin_x, (int)layout->origin_y, layout->width,
                      layout->height);
    board_view_draw_ghost(renderer, layout, game);
    board_view_draw_piece(renderer, layout, game);
    renderer_clear_clip(renderer);
}
