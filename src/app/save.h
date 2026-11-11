#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Settings, key bindings and the top ten scores, plus the text format they are
 * saved in. Deliberately free of SDL and of file I/O, so the tests can run the
 * parser and the serializer on plain strings; src/app/save_file.c finds the
 * file and reads and writes it.
 *
 * The format is plain text, one item per line:
 *
 *     TETRIS SAVE
 *     version=1
 *     volume=10
 *     effects=10
 *     das=10
 *     arr=2
 *     start_level=1
 *     key.left=80 0
 *     ...
 *     score=12345 3 25 9600400441103183515
 *
 * The first line is the magic line and must match exactly. Then come key=value
 * lines in any order. Empty lines and lines starting with '#' are skipped,
 * unknown keys and malformed values are ignored, numbers out of range are
 * clamped, and a score line is "score level lines seed". A missing magic line or
 * a missing or unknown version makes the whole file count as corrupt, and the
 * defaults are used instead.
 */

#define SAVE_MAGIC "TETRIS SAVE"
#define SAVE_FORMAT_VERSION 1

/* How many runs the high-score table keeps. */
#define SAVE_MAX_SCORES 10

/* Enough for every setting, every binding and ten scores with 64-bit seeds. */
#define SAVE_TEXT_CAPACITY 4096

/* Ranges the settings are clamped to. */
#define SETTINGS_VOLUME_MAX 10 /* 0 = silent, 10 = the full mix */
#define SETTINGS_EFFECTS_MAX 10 /* 0 = no particles, shake or flashes, 10 = all of them */
#define SETTINGS_DAS_MIN 1      /* ticks */
#define SETTINGS_DAS_MAX 30
#define SETTINGS_ARR_MAX 10 /* ticks; 0 = the piece goes straight to the wall */
#define SETTINGS_START_LEVEL_MAX 15

/*
 * Actions a key can be bound to, in GameAction bit order: BIND_LEFT is
 * ACTION_LEFT, and so on (settings_action_bit).
 */
typedef enum BindAction {
    BIND_LEFT = 0,
    BIND_RIGHT,
    BIND_SOFT_DROP,
    BIND_HARD_DROP,
    BIND_ROTATE_CW,
    BIND_ROTATE_CCW,
    BIND_HOLD,
    BIND_ACTION_COUNT
} BindAction;

/* Each action can have two keys. A key of 0 means the slot is empty. */
#define SETTINGS_KEYS_PER_ACTION 2

/*
 * Keys are stored as SDL scancodes, which are USB HID usage IDs, so this module
 * can name the ones it needs without including SDL. src/app/input_sdl.c checks
 * every value against SDL_Scancode at compile time. Scancodes go up to 511.
 */
#define SETTINGS_SCANCODE_LIMIT 512
#define SETTINGS_KEY_NONE 0
#define SETTINGS_KEY_C 6
#define SETTINGS_KEY_M 16
#define SETTINGS_KEY_P 19
#define SETTINGS_KEY_R 21
#define SETTINGS_KEY_X 27
#define SETTINGS_KEY_Z 29
#define SETTINGS_KEY_ESCAPE 41
#define SETTINGS_KEY_SPACE 44
#define SETTINGS_KEY_F5 62
#define SETTINGS_KEY_RIGHT 79
#define SETTINGS_KEY_LEFT 80
#define SETTINGS_KEY_DOWN 81
#define SETTINGS_KEY_UP 82
#define SETTINGS_KEY_LCTRL 224
#define SETTINGS_KEY_LSHIFT 225

typedef struct Settings {
    int volume;      /* 0..SETTINGS_VOLUME_MAX */
    int effects;     /* 0..SETTINGS_EFFECTS_MAX */
    int das_ticks;   /* SETTINGS_DAS_MIN..SETTINGS_DAS_MAX */
    int arr_ticks;   /* 0..SETTINGS_ARR_MAX */
    int start_level; /* 1..SETTINGS_START_LEVEL_MAX */
    uint16_t keys[BIND_ACTION_COUNT][SETTINGS_KEYS_PER_ACTION];
} Settings;

/* One finished run. The seed is kept so a run can be told apart and replayed. */
typedef struct HighScore {
    uint32_t score;
    uint32_t level;
    uint32_t lines;
    uint64_t seed;
} HighScore;

typedef struct SaveData {
    Settings settings;
    HighScore scores[SAVE_MAX_SCORES]; /* best first */
    int score_count;
} SaveData;

/* Full volume and effects, the core's DAS and ARR, level 1 and the default keys. */
void settings_defaults(Settings *settings);

/* Default settings and an empty score table. */
void save_defaults(SaveData *save);

/* Clamps every value into its range and empties key slots that hold a
   reserved or out-of-range key. */
void settings_clamp(Settings *settings);

/* The GameAction bit of an action. */
uint32_t settings_action_bit(BindAction action);

/* Upper-case name of an action for the menus, for example "ROTATE CW". */
const char *settings_action_name(BindAction action);

/*
 * True for the keys the app keeps for itself, which cannot be bound: Escape
 * (menu, back), P (pause), M (mute), R (restart) and F5 (shader reload).
 */
bool settings_key_reserved(int scancode);

/* Every action the key is bound to, as GameAction bits; 0 when it is not bound. */
uint32_t settings_actions_for_key(const Settings *settings, int scancode);

/*
 * Puts a key into one slot of an action. The key is first taken out of every
 * other slot, so one key never drives two actions. A reserved or out-of-range
 * key is refused and false is returned.
 */
bool settings_bind_key(Settings *settings, BindAction action, int slot, int scancode);

/* Empties one slot. */
void settings_clear_key(Settings *settings, BindAction action, int slot);

/* Puts the default keys back without touching the other settings. */
void settings_default_keys(Settings *settings);

/*
 * Parses the text of a save file. out always ends up valid: it starts from the
 * defaults and takes every line it can use. Returns false when the text is not
 * a save file of a known version at all (no magic line, no version or an
 * unknown one); out then holds the defaults.
 */
bool save_parse(const char *text, size_t length, SaveData *out);

/*
 * Writes the save file text into buffer, NUL-terminated. Returns its length, or
 * 0 when it does not fit; SAVE_TEXT_CAPACITY is always enough.
 */
size_t save_serialize(const SaveData *save, char *buffer, size_t capacity);

/*
 * True when a run with this score would enter the table. A run that scored
 * nothing never does.
 */
bool save_score_qualifies(const SaveData *save, uint32_t score);

/*
 * Adds a run to the table, best first. A run that ties an entry already in the
 * table goes below it, so the earlier run keeps its place. Returns the new
 * entry's place (0 = best), or -1 when it did not make the table.
 */
int save_insert_score(SaveData *save, const HighScore *entry);
