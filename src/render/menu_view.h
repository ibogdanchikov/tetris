#pragma once

#include <stdbool.h>

#include "app/menu.h"
#include "app/save.h"
#include "core/game.h"
#include "render/board_layout.h"
#include "render/renderer.h"

/*
 * Draws the screens of the menu state machine with the quad renderer and the
 * built-in font. Nothing here changes state; src/app/menu.c owns the screens
 * and the rows.
 *
 * The title, the main menu, the high scores, the settings and the controls
 * cover the whole group (both panels and the playfield) with a neon panel, and
 * the board is not drawn behind them, even when the settings were opened from
 * the pause menu. The pause and game-over menus dim the playfield and leave the
 * HUD readable next to it.
 */

/* Name of a key for the controls screen. The app supplies it: SDL knows the
   names, the view does not. */
typedef const char *(*MenuViewKeyName)(int scancode);

typedef struct MenuViewState {
    const Menu *menu;
    const Settings *settings;
    const SaveData *save;   /* score table, and the best score on the title */
    const GameState *game;  /* the run behind the pause and game-over menus */
    MenuViewKeyName key_name;
    float time_seconds;     /* presentation time, for the blinking hint and the bobbing pieces */
} MenuViewState;

/* True when the screen covers the whole group, so the board and HUD need not be drawn. */
bool menu_view_hides_game(const Menu *menu);

/* Draws the current screen. Draws nothing while playing. Leaves the blend mode at alpha. */
void menu_view_draw(Renderer *renderer, const BoardLayout *layout, const MenuViewState *state);
