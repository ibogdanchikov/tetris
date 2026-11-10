#pragma once

#include <stdbool.h>

#include "core/game.h"
#include "render/board_layout.h"
#include "render/renderer.h"

/*
 * Draws everything around the playfield: the HOLD box and the score / level /
 * lines stats on the left, the NEXT queue on the right, and the dimmed overlay
 * with its message when the game is over or paused. Call it after
 * board_view_draw, so the overlay covers the board.
 *
 * paused is app-level state, not part of GameState. When the game is over and
 * paused at the same time the game-over overlay wins.
 */
void hud_draw(Renderer *renderer, const BoardLayout *layout, const GameState *game, bool paused);
