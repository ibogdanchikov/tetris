#pragma once

#include "core/game.h"
#include "render/board_layout.h"
#include "render/renderer.h"

/*
 * Draws everything around the playfield: the HOLD box and the score / level /
 * lines stats on the left and the NEXT queue on the right. Call it after
 * board_view_draw. The pause and game-over menus that cover the board are drawn
 * by menu_view_draw.
 */
void hud_draw(Renderer *renderer, const BoardLayout *layout, const GameState *game);
