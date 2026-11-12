#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "app/save.h"

/*
 * The screen state machine and the menus on the screens. SDL-free: the app
 * turns key presses into MenuKey values and acts on the MenuCommand it gets
 * back, and src/render/menu_view.c draws the current screen from this state.
 *
 * Game rules are not in here. SCREEN_GAME_OVER mirrors GameState.game_over, the
 * app switches to it when a run ends, and nothing here looks at the board.
 *
 *   TITLE --select--> MAIN_MENU --START--> PLAYING <--resume-- PAUSED
 *     ^  back           |  ^                   |  pause/focus -->  |
 *     +-----------------+  |                   v game over         |
 *                          +-------------- GAME_OVER               |
 *   MAIN_MENU and PAUSED both open SETTINGS, which opens CONTROLS;
 *   MAIN_MENU opens HIGH_SCORES. Back always returns to the screen that opened
 *   the current one.
 */

typedef enum Screen {
    SCREEN_TITLE = 0,
    SCREEN_MAIN_MENU,
    SCREEN_PLAYING,
    SCREEN_PAUSED,
    SCREEN_GAME_OVER,
    SCREEN_SETTINGS,
    SCREEN_CONTROLS,
    SCREEN_HIGH_SCORES,
} Screen;

/* Menu keys, already translated from the keyboard by the app. */
typedef enum MenuKey {
    MENU_KEY_UP = 0,
    MENU_KEY_DOWN,
    MENU_KEY_LEFT,
    MENU_KEY_RIGHT,
    MENU_KEY_SELECT,
    MENU_KEY_BACK,
    MENU_KEY_CLEAR, /* empties the selected key slot on the controls screen */
} MenuKey;

/* What the app has to do after a key. */
typedef enum MenuCommand {
    MENU_COMMAND_NONE = 0,
    MENU_COMMAND_MOVED,            /* the selection moved; only worth a sound */
    MENU_COMMAND_SCREEN_CHANGED,   /* another menu screen is showing */
    MENU_COMMAND_SETTINGS_CHANGED, /* a setting changed; apply it and save later */
    MENU_COMMAND_START_GAME,       /* start a new run and switch to PLAYING */
    MENU_COMMAND_RESUME,           /* close the pause menu */
    MENU_COMMAND_RESTART,          /* drop the run and start a new one */
    MENU_COMMAND_ABANDON,          /* drop the run and go to the main menu */
    MENU_COMMAND_QUIT,
} MenuCommand;

/* One row of a menu screen. */
typedef enum MenuItem {
    MENU_ITEM_START = 0,
    MENU_ITEM_MODE,
    MENU_ITEM_START_LEVEL,
    MENU_ITEM_HIGH_SCORES,
    MENU_ITEM_SETTINGS,
    MENU_ITEM_QUIT,
    MENU_ITEM_RESUME,
    MENU_ITEM_RESTART,
    MENU_ITEM_MAIN_MENU,
    MENU_ITEM_PLAY_AGAIN,
    MENU_ITEM_VOLUME,
    MENU_ITEM_MUSIC,
    MENU_ITEM_EFFECTS,
    MENU_ITEM_BLOOM,
    MENU_ITEM_DAS,
    MENU_ITEM_ARR,
    MENU_ITEM_CONTROLS,
    MENU_ITEM_BACK,
    MENU_ITEM_BIND_FIRST, /* one row per BindAction, BIND_LEFT first */
    MENU_ITEM_RESET_KEYS = MENU_ITEM_BIND_FIRST + BIND_ACTION_COUNT,
} MenuItem;

/* Longest menu; the controls screen has one row per action plus two. */
#define MENU_MAX_ITEMS (BIND_ACTION_COUNT + 2)

typedef struct Menu {
    Screen screen;
    Screen settings_parent; /* MAIN_MENU or PAUSED: where SETTINGS goes back to */
    int cursor;             /* selected row of the current screen */
    int column;             /* key slot on the controls screen, 0 or 1 */
    bool waiting_for_key;   /* the controls screen is waiting for a key to bind */
    int last_rank;          /* place of the run that just ended, -1 if none */
    int last_mode;          /* PlayMode of that run, whose table last_rank is in */
    int score_mode;         /* PlayMode whose table the high-score screen shows */
} Menu;

/* Starts on the title screen. */
void menu_init(Menu *menu);

/* Switches to a screen with the first row selected. */
void menu_set_screen(Menu *menu, Screen screen);

/*
 * Applies one menu key on the current screen. Value rows (mode, start level,
 * volume, music, effects, bloom, DAS, ARR) change with LEFT and RIGHT and
 * report MENU_COMMAND_SETTINGS_CHANGED; the start level only in Marathon,
 * since the other modes always start at level 1. On the high-score screen LEFT
 * and RIGHT switch between the modes' tables; it opens on the selected mode.
 * PLAYING ignores every key; the app handles the game keys itself. Call
 * menu_bind_key instead while waiting_for_key.
 */
MenuCommand menu_handle_key(Menu *menu, Settings *settings, MenuKey key);

/*
 * The key the controls screen was waiting for. Escape cancels, a reserved key
 * is ignored and the screen keeps waiting, and any other key is bound to the
 * selected slot (MENU_COMMAND_SETTINGS_CHANGED).
 */
MenuCommand menu_bind_key(Menu *menu, Settings *settings, int scancode);

/* The rows of a screen, in order. Screens without rows return 0. */
int menu_items(Screen screen, const MenuItem **out_items);

/* The selected row's item; only meaningful when the screen has rows. */
MenuItem menu_selected_item(const Menu *menu);

/* Upper-case label of a row, for example "START LEVEL". */
const char *menu_item_label(MenuItem item);

/* True for rows whose value LEFT and RIGHT change. */
bool menu_item_has_value(MenuItem item);

/*
 * The value shown next to a value row, for example "10", "OFF" or "167 MS".
 * Writes an empty string for any other row.
 */
void menu_item_value(MenuItem item, const Settings *settings, char *out, size_t size);

/* The action a controls row binds, or -1 for the other rows. */
int menu_item_bind_action(MenuItem item);

/* A run time from ticks (60 per second) as "M:SS.CC", for example "1:23.45". */
void menu_format_ticks(uint64_t ticks, char *out, size_t size);
