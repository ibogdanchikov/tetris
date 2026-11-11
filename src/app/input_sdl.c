#include "app/input_sdl.h"

/* save.h names keys by number so it can stay free of SDL; they must match. */
_Static_assert(SETTINGS_KEY_C == SDL_SCANCODE_C, "scancode C");
_Static_assert(SETTINGS_KEY_M == SDL_SCANCODE_M, "scancode M");
_Static_assert(SETTINGS_KEY_P == SDL_SCANCODE_P, "scancode P");
_Static_assert(SETTINGS_KEY_R == SDL_SCANCODE_R, "scancode R");
_Static_assert(SETTINGS_KEY_X == SDL_SCANCODE_X, "scancode X");
_Static_assert(SETTINGS_KEY_Z == SDL_SCANCODE_Z, "scancode Z");
_Static_assert(SETTINGS_KEY_ESCAPE == SDL_SCANCODE_ESCAPE, "scancode Escape");
_Static_assert(SETTINGS_KEY_SPACE == SDL_SCANCODE_SPACE, "scancode Space");
_Static_assert(SETTINGS_KEY_F5 == SDL_SCANCODE_F5, "scancode F5");
_Static_assert(SETTINGS_KEY_RIGHT == SDL_SCANCODE_RIGHT, "scancode Right");
_Static_assert(SETTINGS_KEY_LEFT == SDL_SCANCODE_LEFT, "scancode Left");
_Static_assert(SETTINGS_KEY_DOWN == SDL_SCANCODE_DOWN, "scancode Down");
_Static_assert(SETTINGS_KEY_UP == SDL_SCANCODE_UP, "scancode Up");
_Static_assert(SETTINGS_KEY_LCTRL == SDL_SCANCODE_LCTRL, "scancode Left Ctrl");
_Static_assert(SETTINGS_KEY_LSHIFT == SDL_SCANCODE_LSHIFT, "scancode Left Shift");
_Static_assert(SETTINGS_SCANCODE_LIMIT == SDL_SCANCODE_COUNT, "scancode count");

uint32_t app_input_action(const Settings *settings, SDL_Scancode scancode)
{
    return settings_actions_for_key(settings, (int)scancode);
}

/* Menu key for a scancode, or -1 when the key does nothing in a menu. */
static int app_input_menu_key(SDL_Scancode scancode)
{
    switch (scancode) {
    case SDL_SCANCODE_UP:
        return MENU_KEY_UP;
    case SDL_SCANCODE_DOWN:
        return MENU_KEY_DOWN;
    case SDL_SCANCODE_LEFT:
        return MENU_KEY_LEFT;
    case SDL_SCANCODE_RIGHT:
        return MENU_KEY_RIGHT;
    /* Not Space: it is the default hard drop, and a player still hammering it
       when the stack tops out would skip the game-over screen. */
    case SDL_SCANCODE_RETURN:
    case SDL_SCANCODE_KP_ENTER:
        return MENU_KEY_SELECT;
    case SDL_SCANCODE_ESCAPE:
    case SDL_SCANCODE_BACKSPACE:
        return MENU_KEY_BACK;
    case SDL_SCANCODE_DELETE:
        return MENU_KEY_CLEAR;
    default:
        return -1;
    }
}

static void app_input_push_menu_key(AppInputCommands *commands, MenuKey key)
{
    if (commands->menu_key_count < APP_INPUT_MAX_MENU_KEYS) {
        commands->menu_keys[commands->menu_key_count++] = key;
    }
}

/* Keys that work on every screen. Returns true when the key was one of them. */
static bool app_input_global_key(SDL_Scancode scancode, AppInputCommands *commands)
{
    if (scancode == SDL_SCANCODE_M) {
        commands->mute_toggle = true;
        return true;
    }
    if (scancode == SDL_SCANCODE_F5) {
        commands->reload_shaders = true;
        return true;
    }
    return false;
}

static void app_input_key_down_playing(InputBuffer *buffer, const Settings *settings,
                                       const SDL_KeyboardEvent *key,
                                       AppInputCommands *commands)
{
    if (key->repeat || app_input_global_key(key->scancode, commands)) {
        return;
    }
    switch (key->scancode) {
    case SDL_SCANCODE_ESCAPE:
    case SDL_SCANCODE_P:
        commands->pause_toggle = true;
        return;
    case SDL_SCANCODE_R:
        commands->restart = true;
        return;
    default:
        break;
    }

    const uint32_t action = app_input_action(settings, key->scancode);
    if (action != 0) {
        input_buffer_key_down(buffer, action);
    }
}

static void app_input_key_down_menu(Screen screen, const SDL_KeyboardEvent *key,
                                    AppInputCommands *commands)
{
    if (!key->repeat && commands->bind_scancode == 0) {
        commands->bind_scancode = (int)key->scancode;
    }

    const int menu_key = app_input_menu_key(key->scancode);
    if (menu_key >= 0) {
        /* A held arrow scrolls; a held Return must not select twice. */
        const bool arrow = menu_key == MENU_KEY_UP || menu_key == MENU_KEY_DOWN ||
                           menu_key == MENU_KEY_LEFT || menu_key == MENU_KEY_RIGHT;
        if (!key->repeat || arrow) {
            app_input_push_menu_key(commands, (MenuKey)menu_key);
        }
        return;
    }
    if (key->repeat || app_input_global_key(key->scancode, commands)) {
        return;
    }
    if (key->scancode == SDL_SCANCODE_P && screen == SCREEN_PAUSED) {
        commands->pause_toggle = true;
    } else if (key->scancode == SDL_SCANCODE_R &&
               (screen == SCREEN_PAUSED || screen == SCREEN_GAME_OVER)) {
        commands->restart = true;
    }
}

void app_input_handle_event(InputBuffer *buffer, const Settings *settings, Screen screen,
                            const SDL_Event *event, AppInputCommands *commands)
{
    switch (event->type) {
    case SDL_EVENT_QUIT:
        commands->quit = true;
        break;

    case SDL_EVENT_WINDOW_FOCUS_LOST:
        /* Key-up events go to the window that got the focus, not to us. */
        input_buffer_reset(buffer);
        commands->focus_lost = true;
        break;

    case SDL_EVENT_KEY_DOWN:
        if (screen == SCREEN_PLAYING) {
            app_input_key_down_playing(buffer, settings, &event->key, commands);
        } else {
            app_input_key_down_menu(screen, &event->key, commands);
        }
        break;

    case SDL_EVENT_KEY_UP: {
        const uint32_t action = app_input_action(settings, event->key.scancode);
        if (action != 0) {
            input_buffer_key_up(buffer, action);
        }
        break;
    }

    default:
        break;
    }
}

const char *app_input_key_name(int scancode)
{
    static char name[32];
    if (scancode <= SDL_SCANCODE_UNKNOWN || scancode >= SDL_SCANCODE_COUNT) {
        return "-";
    }

    const char *sdl_name = SDL_GetScancodeName((SDL_Scancode)scancode);
    if (sdl_name == NULL || sdl_name[0] == '\0') {
        SDL_snprintf(name, sizeof name, "KEY %d", scancode);
        return name;
    }

    SDL_strlcpy(name, sdl_name, sizeof name);
    for (char *c = name; *c != '\0'; ++c) {
        if (*c >= 'a' && *c <= 'z') {
            *c = (char)(*c - 'a' + 'A');
        }
    }
    return name;
}
