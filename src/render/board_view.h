#pragma once

#include "core/game.h"
#include "render/board_layout.h"
#include "render/renderer.h"

/*
 * Draws one frame of the playfield: frame, grid, locked cells, the ghost of the
 * active piece, the active piece itself and the next-piece panel. When the game
 * is topped out a translucent overlay dims the playfield.
 *
 * The ghost and the active piece are clipped to the visible rows, so cells in
 * the hidden spawn buffer never draw over the frame.
 *
 * alpha is the fixed-timestep interpolation factor; it is unused until
 * something actually moves between ticks.
 */
void board_view_draw(Renderer *renderer, const BoardLayout *layout, const GameState *game,
                     float alpha);
