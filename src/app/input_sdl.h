#pragma once

#include <stdbool.h>

#include <SDL3/SDL.h>

#include "app/input_buffer.h"
#include "app/menu.h"
#include "app/save.h"

/*
 * Keyboard mapping and event handling for the app layer. The platform layer
 * hands over raw SDL events and knows nothing about game actions.
 *
 * Scancodes are used instead of keycodes so the mapping follows key positions
 * and works the same on any keyboard layout.
 */

/* Game actions bound to a scancode in the settings, or 0 when it is not a game key. */
uint32_t app_input_action(const Settings *settings, SDL_Scancode scancode);

/* Most menu keys one frame can carry; more are dropped. */
#define APP_INPUT_MAX_MENU_KEYS 16

/* App-level commands gathered from the events of one frame. */
typedef struct AppInputCommands {
    bool quit;           /* the window was closed */
    bool restart;        /* R */
    bool pause_toggle;   /* P */
    bool focus_lost;
    bool reload_shaders; /* F5 */
    bool mute_toggle;    /* M */

    /* Menu keys in the order they were pressed. Escape is MENU_KEY_BACK. */
    MenuKey menu_keys[APP_INPUT_MAX_MENU_KEYS];
    int menu_key_count;

    /* The key of a fresh press, for binding it on the controls screen; 0 = none. */
    int bind_scancode;
} AppInputCommands;

/*
 * Applies one SDL event.
 *
 * While playing, game keys go to the buffer as actions, Escape and P set
 * commands->pause_toggle, and R, M and F5 set restart, mute_toggle and
 * reload_shaders. On every other screen the arrows, Return, Escape, Backspace
 * and Delete become menu keys instead, and a fresh key press is also kept in
 * bind_scancode for the controls screen. M and F5 work everywhere, R also works
 * on the pause and game-over screens.
 *
 * Closing the window sets commands->quit. Losing focus clears the buffer so no
 * key stays stuck down and sets commands->focus_lost; whether that pauses is
 * the app's decision. OS key repeat never reaches the game: DAS/ARR inside the
 * game produce the repeats. In the menus it repeats the arrow keys, so a held
 * arrow scrolls.
 */
void app_input_handle_event(InputBuffer *buffer, const Settings *settings, Screen screen,
                            const SDL_Event *event, AppInputCommands *commands);

/*
 * Name of a key for the controls screen, for example "LEFT CTRL", or "-" for an
 * empty slot. The name is SDL's, upper-cased into a static buffer that the next
 * call overwrites.
 */
const char *app_input_key_name(int scancode);
