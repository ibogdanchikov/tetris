#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "core/game.h"

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
 *     music=7
 *     effects=10
 *     bloom=1
 *     das=10
 *     arr=2
 *     start_level=1
 *     mode=sprint
 *     key.left=80 0
 *     ...
 *     score=12345 3 25 9600400441103183515
 *     sprint=5832 40 9600400441103183516
 *     ultra=48210 61 9600400441103183517
 *
 * The first line is the magic line and must match exactly. Then come key=value
 * lines in any order. Empty lines and lines starting with '#' are skipped,
 * unknown keys and malformed values are ignored, numbers out of range are
 * clamped. A Marathon run is "score=score level lines seed", a Sprint run
 * "sprint=ticks lines seed" and an Ultra run "ultra=score lines seed". A missing
 * magic line or a missing or unknown version makes the whole file count as
 * corrupt, and the defaults are used instead.
 *
 * New keys do not change the version: a build that does not know a key skips
 * it. The Sprint and Ultra tables and the mode were added that way, so a save
 * from before them loads with its scores as Marathon.
 */

#define SAVE_MAGIC "TETRIS SAVE"
#define SAVE_FORMAT_VERSION 1

/* How many runs each high-score table keeps. */
#define SAVE_MAX_SCORES 10

/* Enough for every setting, every binding and three full tables with 64-bit seeds. */
#define SAVE_TEXT_CAPACITY 4096

/*
 * The ways to play. Marathon is the endless game; Sprint is
 * PLAY_SPRINT_LINES lines against the clock; Ultra is PLAY_ULTRA_TICKS of
 * play for score. Sprint and Ultra stay at level 1.
 */
typedef enum PlayMode {
    PLAY_MARATHON = 0,
    PLAY_SPRINT,
    PLAY_ULTRA,
    PLAY_MODE_COUNT
} PlayMode;

#define PLAY_SPRINT_LINES 40
#define PLAY_ULTRA_TICKS 7200 /* two minutes at 60 ticks per second */

/* Ranges the settings are clamped to. */
#define SETTINGS_VOLUME_MAX 10 /* 0 = silent, 10 = the full mix */
#define SETTINGS_MUSIC_MAX 10  /* 0 = no music, on top of the volume */
#define SETTINGS_MUSIC_DEFAULT 7
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
    int music;       /* 0..SETTINGS_MUSIC_MAX */
    int effects;     /* 0..SETTINGS_EFFECTS_MAX */
    int bloom;       /* 0 = off, 1 = on */
    int das_ticks;   /* SETTINGS_DAS_MIN..SETTINGS_DAS_MAX */
    int arr_ticks;   /* 0..SETTINGS_ARR_MAX */
    int start_level; /* 1..SETTINGS_START_LEVEL_MAX, Marathon only */
    int mode;        /* PlayMode */
    uint16_t keys[BIND_ACTION_COUNT][SETTINGS_KEYS_PER_ACTION];
} Settings;

/*
 * One finished run. The seed is kept so a run can be told apart and replayed.
 * Sprint ranks by ticks, the others by score; a Sprint entry has no score and
 * level 1, an Ultra entry level 1.
 */
typedef struct HighScore {
    uint32_t score;
    uint32_t level;
    uint32_t lines;
    uint64_t seed;
    uint64_t ticks; /* how long the run lasted; what a Sprint is ranked by */
} HighScore;

/* The best runs of one mode, best first. */
typedef struct ScoreTable {
    HighScore entries[SAVE_MAX_SCORES];
    int count;
} ScoreTable;

typedef struct SaveData {
    Settings settings;
    ScoreTable tables[PLAY_MODE_COUNT]; /* indexed by PlayMode */
} SaveData;

/* Upper-case name of a mode for the menus, for example "SPRINT". */
const char *play_mode_name(PlayMode mode);

/*
 * The core's config for a run with these settings: the shift timing, and the
 * mode's goals. Marathon starts at the start level and levels up; Sprint ends
 * at PLAY_SPRINT_LINES lines and Ultra after PLAY_ULTRA_TICKS, both at level 1.
 */
GameConfig settings_game_config(const Settings *settings);

/* Full volume and effects, music at SETTINGS_MUSIC_DEFAULT, bloom on, the
   core's DAS and ARR, Marathon at level 1 and the default keys. */
void settings_defaults(Settings *settings);

/* Default settings and empty score tables. */
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
 * True when the run would enter its mode's table. Sprint ranks by fewer ticks,
 * Marathon and Ultra by more score. A run with nothing to rank by (no ticks in
 * Sprint, no score otherwise) never enters.
 */
bool save_score_qualifies(const SaveData *save, PlayMode mode, const HighScore *entry);

/*
 * Adds a run to its mode's table, best first. A run that ties an entry already
 * in the table goes below it, so the earlier run keeps its place. Returns the
 * new entry's place (0 = best), or -1 when it did not make the table.
 */
int save_insert_score(SaveData *save, PlayMode mode, const HighScore *entry);

/*
 * Records a run that has ended in its mode's table and returns its place, or
 * -1. A Sprint that did not reach its line goal is lost and records nothing; a
 * Marathon or an Ultra counts however it ended. The entry keeps what the save
 * file keeps for the mode: score, level and lines for Marathon; the run's
 * ticks and lines for Sprint; score and lines for Ultra.
 */
int save_record_run(SaveData *save, PlayMode mode, const GameState *game, uint64_t seed);
