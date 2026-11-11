#pragma once

#include "core/game.h"
#include "fx/fx.h"
#include "render/board_layout.h"
#include "render/renderer.h"

/*
 * Draws everything around the playfield: the HOLD box, the score / level /
 * lines stats and the label of a notable clear on the left, and the NEXT queue
 * on the right. Call it after board_view_draw. The pause and game-over menus
 * that cover the board are drawn by menu_view_draw.
 *
 * The clear label sits in the left panel under the stats and shrinks its text
 * until it fits there, so it never covers the board.
 */
void hud_draw(Renderer *renderer, const BoardLayout *layout, const GameState *game,
              const Fx *fx);
