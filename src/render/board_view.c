#include "render/board_view.h"

#include <stddef.h>

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
 * The ghost is a hologram of the piece's outline in the piece color, added on
 * top of the board. The edge is a twelfth of a cell with a two-pixel floor, so
 * it stays readable when the board shrinks to a fourteen-pixel cell.
 */
#define BOARD_VIEW_GHOST_ALPHA 0.85f
#define BOARD_VIEW_GHOST_BORDER_CELLS 0.08f
#define BOARD_VIEW_GHOST_BORDER_MIN_PX 2.0f

/* A beam of light falls from each column of the piece to the ghost. */
#define BOARD_VIEW_BEAM_ALPHA 0.26f

/*
 * A soft halo of the piece color around the active piece, a glow per cell.
 * The hardness is the RECT_STYLE_GLOW falloff exponent; below the default 2.0
 * the light reaches further out.
 */
#define BOARD_VIEW_HALO_CELLS 3.0f
#define BOARD_VIEW_HALO_ALPHA 0.40f
#define BOARD_VIEW_HALO_HARDNESS 1.6f

/*
 * Sparkle seeds for RECT_STYLE_BLOCK. Locked cells use their board index, the
 * active piece and the HUD previews start at their own bases, so no two blocks
 * on screen share a seed for long.
 */
#define BOARD_VIEW_SPARKLE_PIECE_SEED 500.0f
#define BOARD_VIEW_SPARKLE_PREVIEW_SEED 1000.0f

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

/*
 * Bevel width passed to STYLE_BLOCK: 0 lets the shader use 12% of the cell.
 * sparkle_seed 0 keeps the block from twinkling.
 */
static void board_view_push_block(Renderer *renderer, float x, float y, float cell, Color color,
                                  float sparkle_seed)
{
    board_view_push_cell_styled(renderer, x, y, cell, color, RECT_STYLE_BLOCK, 0.0f, sparkle_seed,
                                0.0f);
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
                                      BOARD_VIEW_PALETTE[type],
                                      (float)(1 + x + y * BOARD_WIDTH));
            }
        }
    }
}

/* True when cell (x, y) is one of the four given cells. */
static bool board_view_cells_contain(const int xs[4], const int ys[4], int x, int y)
{
    for (int i = 0; i < 4; ++i) {
        if (xs[i] == x && ys[i] == y) {
            return true;
        }
    }
    return false;
}

/* RECT_GHOST_* mask of the neighbors of cell i that are also among the cells. */
static unsigned board_view_ghost_mask(const int xs[4], const int ys[4], int i)
{
    static const struct {
        int dx;
        int dy;
        unsigned bit;
    } neighbors[] = {
        { -1, 0, RECT_GHOST_LEFT },       { 1, 0, RECT_GHOST_RIGHT },
        { 0, -1, RECT_GHOST_DOWN },       { 0, 1, RECT_GHOST_UP },
        { -1, -1, RECT_GHOST_DOWN_LEFT }, { 1, -1, RECT_GHOST_DOWN_RIGHT },
        { -1, 1, RECT_GHOST_UP_LEFT },    { 1, 1, RECT_GHOST_UP_RIGHT },
    };

    unsigned mask = 0;
    for (size_t n = 0; n < sizeof neighbors / sizeof neighbors[0]; ++n) {
        if (board_view_cells_contain(xs, ys, xs[i] + neighbors[n].dx, ys[i] + neighbors[n].dy)) {
            mask |= neighbors[n].bit;
        }
    }
    return mask;
}

/*
 * The ghost: a hologram of the piece's silhouette at its landing row. Each
 * cell is drawn over the whole grid slot with a mask of its ghost neighbors,
 * so the shader can join the four cells into one outline. Cells shared with
 * the piece are skipped; the piece covers them anyway, and drawing them would
 * only brighten its edges.
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
    const float inset = cell * BOARD_VIEW_CELL_INSET;
    float border = cell * BOARD_VIEW_GHOST_BORDER_CELLS;
    if (border < BOARD_VIEW_GHOST_BORDER_MIN_PX) {
        border = BOARD_VIEW_GHOST_BORDER_MIN_PX;
    }

    for (int i = 0; i < 4; ++i) {
        if (board_view_cells_contain(active_x, active_y, ghost_x[i], ghost_y[i])) {
            continue;
        }
        renderer_push_styled(renderer, board_view_cell_x(layout, ghost_x[i]),
                             board_view_cell_y(layout, ghost_y[i]), cell, cell, color,
                             RECT_STYLE_GHOST, border, inset,
                             (float)board_view_ghost_mask(ghost_x, ghost_y, i));
    }
}

/*
 * A beam of light down each column of the piece, from its lowest cell in that
 * column to the top of the ghost below it. A tetromino's columns are always
 * next to each other, so each beam tells the shader whether the piece also
 * has a column on its left and right. Nothing is drawn once the piece has
 * landed.
 */
static void board_view_draw_beam(Renderer *renderer, const BoardLayout *layout,
                                 const GameState *game)
{
    const int drop = game->piece.y - game_ghost_y(game);
    if (drop <= 0) {
        return;
    }

    int xs[4];
    int ys[4];
    game_piece_cells(&game->piece, xs, ys);

    const Color base = BOARD_VIEW_PALETTE[piece_cell_type(game->piece.type)];
    const Color color = { base.r, base.g, base.b, BOARD_VIEW_BEAM_ALPHA };
    const float cell = (float)layout->cell_size;

    for (int i = 0; i < 4; ++i) {
        /* One beam per column, drawn for the first cell found in it. */
        bool first = true;
        int lowest_y = ys[i];
        int highest_y = ys[i];
        bool beam_left = false;
        bool beam_right = false;
        for (int j = 0; j < 4; ++j) {
            if (xs[j] == xs[i]) {
                first = first && j >= i;
                lowest_y = ys[j] < lowest_y ? ys[j] : lowest_y;
                highest_y = ys[j] > highest_y ? ys[j] : highest_y;
            }
            beam_left = beam_left || xs[j] == xs[i] - 1;
            beam_right = beam_right || xs[j] == xs[i] + 1;
        }
        if (!first) {
            continue;
        }

        /* The ghost is the piece moved `drop` rows down, so its top cell in
           this column is the piece's top cell minus `drop`. The beam fills the
           empty rows from there up to the piece's lowest cell. */
        const float bottom = board_view_cell_y(layout, highest_y - drop + 1);
        const float top = board_view_cell_y(layout, lowest_y);
        if (top <= bottom) {
            continue;
        }
        renderer_push_styled(renderer, board_view_cell_x(layout, xs[i]), bottom, cell,
                             top - bottom, color, RECT_STYLE_BEAM, beam_left ? 1.0f : 0.0f,
                             beam_right ? 1.0f : 0.0f, 0.0f);
    }
}

/* A soft glow of the piece color behind each cell of the active piece. */
static void board_view_draw_halo(Renderer *renderer, const BoardLayout *layout,
                                 const GameState *game)
{
    int xs[4];
    int ys[4];
    game_piece_cells(&game->piece, xs, ys);

    const Color base = BOARD_VIEW_PALETTE[piece_cell_type(game->piece.type)];
    const Color color = { base.r, base.g, base.b, BOARD_VIEW_HALO_ALPHA };
    const float cell = (float)layout->cell_size;
    const float size = cell * BOARD_VIEW_HALO_CELLS;

    for (int i = 0; i < 4; ++i) {
        const float center_x = board_view_cell_x(layout, xs[i]) + cell * 0.5f;
        const float center_y = board_view_cell_y(layout, ys[i]) + cell * 0.5f;
        renderer_push_styled(renderer, center_x - size * 0.5f, center_y - size * 0.5f, size, size,
                             color, RECT_STYLE_GLOW, BOARD_VIEW_HALO_HARDNESS, 0.0f, 0.0f);
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
                              board_view_cell_y(layout, ys[i]), cell, color,
                              BOARD_VIEW_SPARKLE_PIECE_SEED + (float)i);
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
    /* Seeded from the box position, so each preview box twinkles on its own. */
    const float seed = BOARD_VIEW_SPARKLE_PREVIEW_SEED + box.x * 0.73f + box.y * 1.37f;

    for (int i = 0; i < 4; ++i) {
        board_view_push_block(renderer, left + (float)(cells[i].x - min_x) * cell,
                              bottom + (float)(cells[i].y - min_y) * cell, cell, color,
                              seed + (float)i);
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
    /* Beam, ghost and halo are light: they add up instead of covering. */
    renderer_set_blend(renderer, RENDER_BLEND_ADDITIVE);
    board_view_draw_beam(renderer, layout, game);
    board_view_draw_ghost(renderer, layout, game);
    board_view_draw_halo(renderer, layout, game);
    renderer_set_blend(renderer, RENDER_BLEND_ALPHA);
    board_view_draw_piece(renderer, layout, game);
    renderer_clear_clip(renderer);
}
