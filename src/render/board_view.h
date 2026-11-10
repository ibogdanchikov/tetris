#pragma once

#include "core/game.h"
#include "render/board_layout.h"
#include "render/renderer.h"

/*
 * Draws one frame of the playfield: frame, grid, locked cells, the ghost of the
 * active piece and the active piece itself. The ghost and the piece are clipped
 * to the visible rows, so cells in the hidden spawn buffer never draw over the
 * frame.
 *
 * The HUD panels and the paused/game-over overlays live in render/hud.h; call
 * board_view_draw first.
 *
 * alpha is the fixed-timestep interpolation factor; it is unused until
 * something actually moves between ticks.
 */
void board_view_draw(Renderer *renderer, const BoardLayout *layout, const GameState *game,
                     float alpha);

/* Classic tetromino colour for a cell type, shared by the board and the HUD. */
Color board_view_cell_color(CellType type);

/*
 * Draws a piece in its spawn orientation centred inside a box by its actual
 * drawn extent, not by its SRS bounding box: a flat I is one cell tall and would
 * sit off-centre if the whole box were centred. The hold and next previews use
 * this, so they stay centred the same way the old next panel did.
 */
void board_view_draw_preview(Renderer *renderer, LayoutRect box, int cell_size, PieceType type,
                             Color color);
