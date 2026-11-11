#pragma once

#include <stdbool.h>

#include <SDL3/SDL.h>

#include "app/input_buffer.h"

/*
 * Keyboard mapping and event handling for the app layer. The platform layer
 * hands over raw SDL events and knows nothing about game actions.
 *
 * Scancodes are used instead of keycodes so the mapping follows key positions
 * and works the same on any keyboard layout.
 */

/* Game action bound to a scancode, or 0 when the key is not a game key. */
uint32_t app_input_action(SDL_Scancode scancode);

/* App-level commands gathered from the events of one frame. */
typedef struct AppInputCommands {
    bool quit;
    bool restart;
    bool pause_toggle;
    bool focus_lost;
    bool reload_shaders;
    bool mute_toggle;
} AppInputCommands;

/*
 * Applies one SDL event: game keys go to the buffer as actions, Escape and the
 * quit event set commands->quit, R sets commands->restart, P sets
 * commands->pause_toggle, M sets commands->mute_toggle, F5 sets
 * commands->reload_shaders, and losing focus clears the buffer so no key stays
 * stuck down and sets commands->focus_lost.
 *
 * Whether a lost focus pauses the game is the app's decision, not this layer's:
 * a finished run has nothing to pause. OS key repeat is ignored: DAS/ARR inside
 * the game produce the repeats, and a platform repeat would look like a fresh
 * press every time.
 */
void app_input_handle_event(InputBuffer *buffer, const SDL_Event *event,
                            AppInputCommands *commands);
