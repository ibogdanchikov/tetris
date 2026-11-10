#pragma once

#include "core/board.h"
#include "render/board_layout.h"
#include "render/renderer.h"

/*
 * Draws the playfield: border frame, grid, and every non-empty cell.
 * alpha is the fixed-timestep interpolation factor; it is unused until
 * something actually moves between ticks.
 */
void board_view_draw(Renderer *renderer, const BoardLayout *layout, const Board *board, float alpha);
