#include "app/input_sdl.h"

uint32_t app_input_action(SDL_Scancode scancode)
{
    switch (scancode) {
    case SDL_SCANCODE_LEFT:
        return ACTION_LEFT;
    case SDL_SCANCODE_RIGHT:
        return ACTION_RIGHT;
    case SDL_SCANCODE_DOWN:
        return ACTION_SOFT_DROP;
    case SDL_SCANCODE_SPACE:
        return ACTION_HARD_DROP;
    case SDL_SCANCODE_UP:
    case SDL_SCANCODE_X:
        return ACTION_ROTATE_CW;
    case SDL_SCANCODE_Z:
    case SDL_SCANCODE_LCTRL:
        return ACTION_ROTATE_CCW;
    default:
        return 0;
    }
}

void app_input_handle_event(InputBuffer *buffer, const SDL_Event *event,
                            AppInputCommands *commands)
{
    switch (event->type) {
    case SDL_EVENT_QUIT:
        commands->quit = true;
        break;

    case SDL_EVENT_WINDOW_FOCUS_LOST:
        /* Key-up events go to the window that got the focus, not to us. */
        input_buffer_reset(buffer);
        break;

    case SDL_EVENT_KEY_DOWN: {
        if (event->key.repeat) {
            break;
        }
        const uint32_t action = app_input_action(event->key.scancode);
        if (action != 0) {
            input_buffer_key_down(buffer, action);
        } else if (event->key.scancode == SDL_SCANCODE_ESCAPE) {
            commands->quit = true;
        } else if (event->key.scancode == SDL_SCANCODE_R) {
            commands->restart = true;
        }
        break;
    }

    case SDL_EVENT_KEY_UP: {
        const uint32_t action = app_input_action(event->key.scancode);
        if (action != 0) {
            input_buffer_key_up(buffer, action);
        }
        break;
    }

    default:
        break;
    }
}
