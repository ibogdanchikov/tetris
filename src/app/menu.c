#include "app/menu.h"

#include <stdio.h>

static const MenuItem MENU_MAIN_ITEMS[] = {
    MENU_ITEM_START, MENU_ITEM_START_LEVEL, MENU_ITEM_HIGH_SCORES, MENU_ITEM_SETTINGS,
    MENU_ITEM_QUIT,
};

static const MenuItem MENU_PAUSE_ITEMS[] = {
    MENU_ITEM_RESUME, MENU_ITEM_RESTART, MENU_ITEM_SETTINGS, MENU_ITEM_MAIN_MENU,
};

static const MenuItem MENU_GAME_OVER_ITEMS[] = {
    MENU_ITEM_PLAY_AGAIN,
    MENU_ITEM_MAIN_MENU,
};

static const MenuItem MENU_SETTINGS_ITEMS[] = {
    MENU_ITEM_VOLUME, MENU_ITEM_EFFECTS, MENU_ITEM_DAS,
    MENU_ITEM_ARR,    MENU_ITEM_CONTROLS, MENU_ITEM_BACK,
};

static const MenuItem MENU_CONTROLS_ITEMS[] = {
    (MenuItem)(MENU_ITEM_BIND_FIRST + BIND_LEFT),
    (MenuItem)(MENU_ITEM_BIND_FIRST + BIND_RIGHT),
    (MenuItem)(MENU_ITEM_BIND_FIRST + BIND_SOFT_DROP),
    (MenuItem)(MENU_ITEM_BIND_FIRST + BIND_HARD_DROP),
    (MenuItem)(MENU_ITEM_BIND_FIRST + BIND_ROTATE_CW),
    (MenuItem)(MENU_ITEM_BIND_FIRST + BIND_ROTATE_CCW),
    (MenuItem)(MENU_ITEM_BIND_FIRST + BIND_HOLD),
    MENU_ITEM_RESET_KEYS,
    MENU_ITEM_BACK,
};

#define MENU_COUNT(items) ((int)(sizeof(items) / sizeof((items)[0])))

int menu_items(Screen screen, const MenuItem **out_items)
{
    switch (screen) {
    case SCREEN_MAIN_MENU:
        *out_items = MENU_MAIN_ITEMS;
        return MENU_COUNT(MENU_MAIN_ITEMS);
    case SCREEN_PAUSED:
        *out_items = MENU_PAUSE_ITEMS;
        return MENU_COUNT(MENU_PAUSE_ITEMS);
    case SCREEN_GAME_OVER:
        *out_items = MENU_GAME_OVER_ITEMS;
        return MENU_COUNT(MENU_GAME_OVER_ITEMS);
    case SCREEN_SETTINGS:
        *out_items = MENU_SETTINGS_ITEMS;
        return MENU_COUNT(MENU_SETTINGS_ITEMS);
    case SCREEN_CONTROLS:
        *out_items = MENU_CONTROLS_ITEMS;
        return MENU_COUNT(MENU_CONTROLS_ITEMS);
    case SCREEN_TITLE:
    case SCREEN_PLAYING:
    case SCREEN_HIGH_SCORES:
    default:
        *out_items = NULL;
        return 0;
    }
}

void menu_init(Menu *menu)
{
    menu->screen = SCREEN_TITLE;
    menu->settings_parent = SCREEN_MAIN_MENU;
    menu->cursor = 0;
    menu->column = 0;
    menu->waiting_for_key = false;
    menu->last_rank = -1;
}

void menu_set_screen(Menu *menu, Screen screen)
{
    menu->screen = screen;
    menu->cursor = 0;
    menu->column = 0;
    menu->waiting_for_key = false;
}

/* Switches screens with the cursor on a given row, so going back lands on the
   row that opened the screen. */
static void menu_set_screen_at(Menu *menu, Screen screen, MenuItem item)
{
    menu_set_screen(menu, screen);
    const MenuItem *items = NULL;
    const int count = menu_items(screen, &items);
    for (int i = 0; i < count; ++i) {
        if (items[i] == item) {
            menu->cursor = i;
            break;
        }
    }
}

MenuItem menu_selected_item(const Menu *menu)
{
    const MenuItem *items = NULL;
    const int count = menu_items(menu->screen, &items);
    if (count == 0) {
        return MENU_ITEM_BACK;
    }
    const int cursor = menu->cursor < 0 ? 0 : (menu->cursor >= count ? count - 1 : menu->cursor);
    return items[cursor];
}

int menu_item_bind_action(MenuItem item)
{
    const int action = (int)item - (int)MENU_ITEM_BIND_FIRST;
    return action >= 0 && action < BIND_ACTION_COUNT ? action : -1;
}

const char *menu_item_label(MenuItem item)
{
    const int action = menu_item_bind_action(item);
    if (action >= 0) {
        return settings_action_name((BindAction)action);
    }

    switch (item) {
    case MENU_ITEM_START:
        return "START";
    case MENU_ITEM_START_LEVEL:
        return "LEVEL";
    case MENU_ITEM_HIGH_SCORES:
        return "HIGH SCORES";
    case MENU_ITEM_SETTINGS:
        return "SETTINGS";
    case MENU_ITEM_QUIT:
        return "QUIT";
    case MENU_ITEM_RESUME:
        return "RESUME";
    case MENU_ITEM_RESTART:
        return "RESTART";
    case MENU_ITEM_MAIN_MENU:
        return "MAIN MENU";
    case MENU_ITEM_PLAY_AGAIN:
        return "PLAY AGAIN";
    case MENU_ITEM_VOLUME:
        return "VOLUME";
    case MENU_ITEM_EFFECTS:
        return "EFFECTS";
    case MENU_ITEM_DAS:
        return "DAS";
    case MENU_ITEM_ARR:
        return "ARR";
    case MENU_ITEM_CONTROLS:
        return "CONTROLS";
    case MENU_ITEM_BACK:
        return "BACK";
    case MENU_ITEM_RESET_KEYS:
        return "RESET KEYS";
    case MENU_ITEM_BIND_FIRST:
    default:
        return "";
    }
}

/* The setting a value row edits, with its range; NULL for any other row. */
static int *menu_item_setting(MenuItem item, Settings *settings, int *out_min, int *out_max)
{
    switch (item) {
    case MENU_ITEM_START_LEVEL:
        *out_min = 1;
        *out_max = SETTINGS_START_LEVEL_MAX;
        return &settings->start_level;
    case MENU_ITEM_VOLUME:
        *out_min = 0;
        *out_max = SETTINGS_VOLUME_MAX;
        return &settings->volume;
    case MENU_ITEM_EFFECTS:
        *out_min = 0;
        *out_max = SETTINGS_EFFECTS_MAX;
        return &settings->effects;
    case MENU_ITEM_DAS:
        *out_min = SETTINGS_DAS_MIN;
        *out_max = SETTINGS_DAS_MAX;
        return &settings->das_ticks;
    case MENU_ITEM_ARR:
        *out_min = 0;
        *out_max = SETTINGS_ARR_MAX;
        return &settings->arr_ticks;
    default:
        return NULL;
    }
}

bool menu_item_has_value(MenuItem item)
{
    Settings scratch;
    int min = 0;
    int max = 0;
    return menu_item_setting(item, &scratch, &min, &max) != NULL;
}

/* Ticks at 60 per second, shown as rounded milliseconds. */
static int menu_ticks_to_ms(int ticks)
{
    return (ticks * 1000 + 30) / 60;
}

void menu_item_value(MenuItem item, const Settings *settings, char *out, size_t size)
{
    if (size == 0) {
        return;
    }
    out[0] = '\0';

    switch (item) {
    case MENU_ITEM_START_LEVEL:
        snprintf(out, size, "%d", settings->start_level);
        break;
    case MENU_ITEM_VOLUME:
        if (settings->volume == 0) {
            snprintf(out, size, "OFF");
        } else {
            snprintf(out, size, "%d", settings->volume);
        }
        break;
    case MENU_ITEM_EFFECTS:
        if (settings->effects == 0) {
            snprintf(out, size, "OFF");
        } else {
            snprintf(out, size, "%d", settings->effects);
        }
        break;
    case MENU_ITEM_DAS:
        snprintf(out, size, "%d MS", menu_ticks_to_ms(settings->das_ticks));
        break;
    case MENU_ITEM_ARR:
        snprintf(out, size, "%d MS", menu_ticks_to_ms(settings->arr_ticks));
        break;
    default:
        break;
    }
}

/* LEFT/RIGHT on a value row. Stops at the ends of the range. */
static MenuCommand menu_change_value(MenuItem item, Settings *settings, int step)
{
    int min = 0;
    int max = 0;
    int *value = menu_item_setting(item, settings, &min, &max);
    if (value == NULL) {
        return MENU_COMMAND_NONE;
    }
    const int changed = *value + step;
    if (changed < min || changed > max) {
        return MENU_COMMAND_NONE;
    }
    *value = changed;
    return MENU_COMMAND_SETTINGS_CHANGED;
}

/* SELECT on a row. */
static MenuCommand menu_select(Menu *menu, Settings *settings, MenuItem item)
{
    switch (item) {
    case MENU_ITEM_START:
    case MENU_ITEM_PLAY_AGAIN:
        return MENU_COMMAND_START_GAME;
    case MENU_ITEM_HIGH_SCORES:
        menu_set_screen(menu, SCREEN_HIGH_SCORES);
        return MENU_COMMAND_SCREEN_CHANGED;
    case MENU_ITEM_SETTINGS:
        menu->settings_parent = menu->screen;
        menu_set_screen(menu, SCREEN_SETTINGS);
        return MENU_COMMAND_SCREEN_CHANGED;
    case MENU_ITEM_QUIT:
        return MENU_COMMAND_QUIT;
    case MENU_ITEM_RESUME:
        return MENU_COMMAND_RESUME;
    case MENU_ITEM_RESTART:
        return MENU_COMMAND_RESTART;
    case MENU_ITEM_MAIN_MENU:
        return MENU_COMMAND_ABANDON;
    case MENU_ITEM_CONTROLS:
        menu_set_screen(menu, SCREEN_CONTROLS);
        return MENU_COMMAND_SCREEN_CHANGED;
    case MENU_ITEM_RESET_KEYS:
        settings_default_keys(settings);
        return MENU_COMMAND_SETTINGS_CHANGED;
    case MENU_ITEM_BACK:
        return menu_handle_key(menu, settings, MENU_KEY_BACK);
    default:
        if (menu_item_bind_action(item) >= 0) {
            menu->waiting_for_key = true;
            return MENU_COMMAND_MOVED;
        }
        return MENU_COMMAND_NONE;
    }
}

/* BACK on each screen. */
static MenuCommand menu_back(Menu *menu)
{
    switch (menu->screen) {
    case SCREEN_TITLE:
        return MENU_COMMAND_QUIT;
    case SCREEN_MAIN_MENU:
        menu_set_screen(menu, SCREEN_TITLE);
        return MENU_COMMAND_SCREEN_CHANGED;
    case SCREEN_PAUSED:
        return MENU_COMMAND_RESUME;
    case SCREEN_GAME_OVER:
        return MENU_COMMAND_ABANDON;
    case SCREEN_SETTINGS:
        menu_set_screen_at(menu, menu->settings_parent, MENU_ITEM_SETTINGS);
        return MENU_COMMAND_SCREEN_CHANGED;
    case SCREEN_CONTROLS:
        menu_set_screen_at(menu, SCREEN_SETTINGS, MENU_ITEM_CONTROLS);
        return MENU_COMMAND_SCREEN_CHANGED;
    case SCREEN_HIGH_SCORES:
        menu_set_screen_at(menu, SCREEN_MAIN_MENU, MENU_ITEM_HIGH_SCORES);
        return MENU_COMMAND_SCREEN_CHANGED;
    case SCREEN_PLAYING:
    default:
        return MENU_COMMAND_NONE;
    }
}

MenuCommand menu_handle_key(Menu *menu, Settings *settings, MenuKey key)
{
    if (menu->screen == SCREEN_PLAYING || menu->waiting_for_key) {
        return MENU_COMMAND_NONE;
    }

    if (key == MENU_KEY_BACK) {
        return menu_back(menu);
    }

    const MenuItem *items = NULL;
    const int count = menu_items(menu->screen, &items);
    if (count == 0) {
        /* The title and the score table only wait for select or back. */
        if (key == MENU_KEY_SELECT) {
            if (menu->screen == SCREEN_TITLE) {
                menu_set_screen(menu, SCREEN_MAIN_MENU);
                return MENU_COMMAND_SCREEN_CHANGED;
            }
            return menu_back(menu);
        }
        return MENU_COMMAND_NONE;
    }

    const MenuItem item = menu_selected_item(menu);
    const int action = menu_item_bind_action(item);

    switch (key) {
    case MENU_KEY_UP:
        menu->cursor = (menu->cursor + count - 1) % count;
        return MENU_COMMAND_MOVED;
    case MENU_KEY_DOWN:
        menu->cursor = (menu->cursor + 1) % count;
        return MENU_COMMAND_MOVED;
    case MENU_KEY_LEFT:
    case MENU_KEY_RIGHT:
        if (action >= 0) {
            const int column = key == MENU_KEY_LEFT ? 0 : SETTINGS_KEYS_PER_ACTION - 1;
            if (column == menu->column) {
                return MENU_COMMAND_NONE;
            }
            menu->column = column;
            return MENU_COMMAND_MOVED;
        }
        return menu_change_value(item, settings, key == MENU_KEY_LEFT ? -1 : 1);
    case MENU_KEY_SELECT:
        return menu_select(menu, settings, item);
    case MENU_KEY_CLEAR:
        if (action >= 0 && settings->keys[action][menu->column] != SETTINGS_KEY_NONE) {
            settings_clear_key(settings, (BindAction)action, menu->column);
            return MENU_COMMAND_SETTINGS_CHANGED;
        }
        return MENU_COMMAND_NONE;
    case MENU_KEY_BACK:
    default:
        return MENU_COMMAND_NONE;
    }
}

MenuCommand menu_bind_key(Menu *menu, Settings *settings, int scancode)
{
    if (!menu->waiting_for_key) {
        return MENU_COMMAND_NONE;
    }
    if (scancode == SETTINGS_KEY_ESCAPE) {
        menu->waiting_for_key = false;
        return MENU_COMMAND_MOVED;
    }

    const int action = menu_item_bind_action(menu_selected_item(menu));
    if (action < 0) {
        menu->waiting_for_key = false;
        return MENU_COMMAND_NONE;
    }
    if (!settings_bind_key(settings, (BindAction)action, menu->column, scancode)) {
        return MENU_COMMAND_NONE;
    }
    menu->waiting_for_key = false;
    return MENU_COMMAND_SETTINGS_CHANGED;
}
