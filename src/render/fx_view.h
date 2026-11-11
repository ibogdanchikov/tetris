#pragma once

#include "fx/fx.h"
#include "render/board_layout.h"
#include "render/renderer.h"

/*
 * Draws the presentation effects held in Fx: the lock flash, the hard-drop
 * trails and the glow particles. It is the only place that turns fx cell
 * coordinates into pixels, so the effect simulation stays window-size
 * independent.
 *
 * Call it after board_view_draw (the effects belong on top of the board) and
 * before hud_draw, so the panels and the paused / game-over overlay stay
 * readable. The level-up frame pulse is not here: board_view owns the frame and
 * takes the pulse as a parameter.
 *
 * The particles and trails are additive and clipped to a small margin around the
 * playfield, so a burst never paints over the HUD. The blend mode is restored to
 * alpha before returning.
 */
void fx_view_draw(Renderer *renderer, const BoardLayout *layout, const Fx *fx);
