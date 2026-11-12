#include "app/save.h"

#include <stdio.h>
#include <string.h>

#include "core/game.h"
#include "core/input.h"

/* BindAction is in GameAction bit order, so settings_action_bit is a shift. */
_Static_assert(ACTION_LEFT == 1u << BIND_LEFT, "BIND_LEFT");
_Static_assert(ACTION_RIGHT == 1u << BIND_RIGHT, "BIND_RIGHT");
_Static_assert(ACTION_SOFT_DROP == 1u << BIND_SOFT_DROP, "BIND_SOFT_DROP");
_Static_assert(ACTION_HARD_DROP == 1u << BIND_HARD_DROP, "BIND_HARD_DROP");
_Static_assert(ACTION_ROTATE_CW == 1u << BIND_ROTATE_CW, "BIND_ROTATE_CW");
_Static_assert(ACTION_ROTATE_CCW == 1u << BIND_ROTATE_CCW, "BIND_ROTATE_CCW");
_Static_assert(ACTION_HOLD == 1u << BIND_HOLD, "BIND_HOLD");

/* Longest line the parser reads; anything longer is ignored as malformed. */
#define SAVE_MAX_LINE 256

/* Name of each action in the file, after "key.". */
static const char *const SAVE_KEY_NAMES[BIND_ACTION_COUNT] = {
    "left", "right", "soft_drop", "hard_drop", "rotate_cw", "rotate_ccw", "hold",
};

/* Mode names: lower case in the file, upper case on screen. */
static const char *const SAVE_MODE_KEYS[PLAY_MODE_COUNT] = { "marathon", "sprint", "ultra" };
static const char *const PLAY_MODE_NAMES[PLAY_MODE_COUNT] = { "MARATHON", "SPRINT", "ULTRA" };

static const char *const SETTINGS_ACTION_NAMES[BIND_ACTION_COUNT] = {
    "LEFT", "RIGHT", "SOFT DROP", "HARD DROP", "ROTATE CW", "ROTATE CCW", "HOLD",
};

static const uint16_t SETTINGS_DEFAULT_KEYS[BIND_ACTION_COUNT][SETTINGS_KEYS_PER_ACTION] = {
    { SETTINGS_KEY_LEFT, SETTINGS_KEY_NONE },
    { SETTINGS_KEY_RIGHT, SETTINGS_KEY_NONE },
    { SETTINGS_KEY_DOWN, SETTINGS_KEY_NONE },
    { SETTINGS_KEY_SPACE, SETTINGS_KEY_NONE },
    { SETTINGS_KEY_UP, SETTINGS_KEY_X },
    { SETTINGS_KEY_Z, SETTINGS_KEY_LCTRL },
    { SETTINGS_KEY_C, SETTINGS_KEY_LSHIFT },
};

static int settings_clamp_int(int value, int min, int max)
{
    if (value < min) {
        return min;
    }
    if (value > max) {
        return max;
    }
    return value;
}

void settings_default_keys(Settings *settings)
{
    memcpy(settings->keys, SETTINGS_DEFAULT_KEYS, sizeof settings->keys);
}

void settings_defaults(Settings *settings)
{
    memset(settings, 0, sizeof *settings);
    settings->volume = SETTINGS_VOLUME_MAX;
    settings->music = SETTINGS_MUSIC_DEFAULT;
    settings->effects = SETTINGS_EFFECTS_MAX;
    settings->bloom = 1;
    settings->das_ticks = GAME_DAS_TICKS;
    settings->arr_ticks = GAME_ARR_TICKS;
    settings->start_level = 1;
    settings_default_keys(settings);
}

void save_defaults(SaveData *save)
{
    memset(save, 0, sizeof *save);
    settings_defaults(&save->settings);
}

bool settings_key_reserved(int scancode)
{
    return scancode == SETTINGS_KEY_ESCAPE || scancode == SETTINGS_KEY_P ||
           scancode == SETTINGS_KEY_M || scancode == SETTINGS_KEY_R ||
           scancode == SETTINGS_KEY_F5;
}

static bool settings_key_bindable(int scancode)
{
    return scancode > SETTINGS_KEY_NONE && scancode < SETTINGS_SCANCODE_LIMIT &&
           !settings_key_reserved(scancode);
}

void settings_clamp(Settings *settings)
{
    settings->volume = settings_clamp_int(settings->volume, 0, SETTINGS_VOLUME_MAX);
    settings->music = settings_clamp_int(settings->music, 0, SETTINGS_MUSIC_MAX);
    settings->effects = settings_clamp_int(settings->effects, 0, SETTINGS_EFFECTS_MAX);
    settings->bloom = settings_clamp_int(settings->bloom, 0, 1);
    settings->das_ticks =
        settings_clamp_int(settings->das_ticks, SETTINGS_DAS_MIN, SETTINGS_DAS_MAX);
    settings->arr_ticks = settings_clamp_int(settings->arr_ticks, 0, SETTINGS_ARR_MAX);
    settings->start_level =
        settings_clamp_int(settings->start_level, 1, SETTINGS_START_LEVEL_MAX);
    settings->mode = settings_clamp_int(settings->mode, 0, PLAY_MODE_COUNT - 1);

    /* Drop keys that cannot be bound, then any key already used by an earlier
       slot, so one key never drives two actions. */
    for (int action = 0; action < BIND_ACTION_COUNT; ++action) {
        for (int slot = 0; slot < SETTINGS_KEYS_PER_ACTION; ++slot) {
            const int key = settings->keys[action][slot];
            if (!settings_key_bindable(key)) {
                settings->keys[action][slot] = SETTINGS_KEY_NONE;
                continue;
            }
            for (int earlier = 0; earlier < action * SETTINGS_KEYS_PER_ACTION + slot;
                 ++earlier) {
                if (settings->keys[earlier / SETTINGS_KEYS_PER_ACTION]
                                  [earlier % SETTINGS_KEYS_PER_ACTION] == key) {
                    settings->keys[action][slot] = SETTINGS_KEY_NONE;
                    break;
                }
            }
        }
    }
}

const char *play_mode_name(PlayMode mode)
{
    return (unsigned)mode < (unsigned)PLAY_MODE_COUNT ? PLAY_MODE_NAMES[mode] : "";
}

GameConfig settings_game_config(const Settings *settings)
{
    GameConfig config = game_default_config();
    config.das_ticks = (uint16_t)settings->das_ticks;
    config.arr_ticks = (uint16_t)settings->arr_ticks;
    switch (settings->mode) {
    case PLAY_SPRINT:
        config.line_goal = PLAY_SPRINT_LINES;
        config.level_up = false;
        break;
    case PLAY_ULTRA:
        config.tick_limit = PLAY_ULTRA_TICKS;
        config.level_up = false;
        break;
    case PLAY_MARATHON:
    default:
        config.start_level = (uint32_t)settings->start_level;
        break;
    }
    return config;
}

uint32_t settings_action_bit(BindAction action)
{
    return (unsigned)action < (unsigned)BIND_ACTION_COUNT ? 1u << action : 0u;
}

const char *settings_action_name(BindAction action)
{
    return (unsigned)action < (unsigned)BIND_ACTION_COUNT ? SETTINGS_ACTION_NAMES[action] : "";
}

uint32_t settings_actions_for_key(const Settings *settings, int scancode)
{
    if (scancode == SETTINGS_KEY_NONE) {
        return 0;
    }
    uint32_t actions = 0;
    for (int action = 0; action < BIND_ACTION_COUNT; ++action) {
        for (int slot = 0; slot < SETTINGS_KEYS_PER_ACTION; ++slot) {
            if (settings->keys[action][slot] == scancode) {
                actions |= settings_action_bit((BindAction)action);
            }
        }
    }
    return actions;
}

bool settings_bind_key(Settings *settings, BindAction action, int slot, int scancode)
{
    if ((unsigned)action >= (unsigned)BIND_ACTION_COUNT || slot < 0 ||
        slot >= SETTINGS_KEYS_PER_ACTION || !settings_key_bindable(scancode)) {
        return false;
    }
    for (int a = 0; a < BIND_ACTION_COUNT; ++a) {
        for (int s = 0; s < SETTINGS_KEYS_PER_ACTION; ++s) {
            if (settings->keys[a][s] == scancode) {
                settings->keys[a][s] = SETTINGS_KEY_NONE;
            }
        }
    }
    settings->keys[action][slot] = (uint16_t)scancode;
    return true;
}

void settings_clear_key(Settings *settings, BindAction action, int slot)
{
    if ((unsigned)action < (unsigned)BIND_ACTION_COUNT && slot >= 0 &&
        slot < SETTINGS_KEYS_PER_ACTION) {
        settings->keys[action][slot] = SETTINGS_KEY_NONE;
    }
}

/* --- high scores --------------------------------------------------------- */

/* True when a ranks strictly above b in the mode's table. */
static bool save_ranks_above(PlayMode mode, const HighScore *a, const HighScore *b)
{
    if (mode == PLAY_SPRINT) {
        return a->ticks < b->ticks;
    }
    return a->score > b->score;
}

bool save_score_qualifies(const SaveData *save, PlayMode mode, const HighScore *entry)
{
    if ((unsigned)mode >= (unsigned)PLAY_MODE_COUNT) {
        return false;
    }
    if (mode == PLAY_SPRINT ? entry->ticks == 0 : entry->score == 0) {
        return false;
    }
    const ScoreTable *table = &save->tables[mode];
    if (table->count < SAVE_MAX_SCORES) {
        return true;
    }
    return save_ranks_above(mode, entry, &table->entries[SAVE_MAX_SCORES - 1]);
}

int save_insert_score(SaveData *save, PlayMode mode, const HighScore *entry)
{
    if (!save_score_qualifies(save, mode, entry)) {
        return -1;
    }
    ScoreTable *table = &save->tables[mode];

    /* Below every entry that ranks the same or better. */
    int place = 0;
    while (place < table->count && !save_ranks_above(mode, entry, &table->entries[place])) {
        ++place;
    }

    const int count = table->count < SAVE_MAX_SCORES ? table->count + 1 : SAVE_MAX_SCORES;
    for (int i = count - 1; i > place; --i) {
        table->entries[i] = table->entries[i - 1];
    }
    table->entries[place] = *entry;
    table->count = count;
    return place;
}

int save_record_run(SaveData *save, PlayMode mode, const GameState *game, uint64_t seed)
{
    HighScore entry = { 0, 1, game->lines_cleared, seed, 0 };
    switch (mode) {
    case PLAY_SPRINT:
        if (game->game_over_reason != GAME_OVER_LINE_GOAL) {
            return -1;
        }
        entry.ticks = game_run_ticks(game);
        break;
    case PLAY_ULTRA:
        entry.score = game->score;
        break;
    case PLAY_MARATHON:
    default:
        entry.score = game->score;
        entry.level = game->level;
        break;
    }
    return save_insert_score(save, mode, &entry);
}

/* --- parsing ------------------------------------------------------------- */

static bool save_is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\r';
}

/* Trims spaces at both ends in place and returns the new start. */
static char *save_trim(char *text)
{
    while (save_is_space(*text)) {
        ++text;
    }
    size_t length = strlen(text);
    while (length > 0 && save_is_space(text[length - 1])) {
        text[--length] = '\0';
    }
    return text;
}

/*
 * Reads one unsigned decimal number at *cursor, skipping the spaces before it,
 * and moves the cursor past it. Fails on a sign, on no digits and on overflow.
 */
static bool save_read_u64(const char **cursor, uint64_t *out)
{
    const char *c = *cursor;
    while (save_is_space(*c)) {
        ++c;
    }
    if (*c < '0' || *c > '9') {
        return false;
    }

    uint64_t value = 0;
    while (*c >= '0' && *c <= '9') {
        const uint64_t digit = (uint64_t)(*c - '0');
        if (value > (UINT64_MAX - digit) / 10u) {
            return false;
        }
        value = value * 10u + digit;
        ++c;
    }
    *cursor = c;
    *out = value;
    return true;
}

/* True when only spaces are left. */
static bool save_at_end(const char *cursor)
{
    while (save_is_space(*cursor)) {
        ++cursor;
    }
    return *cursor == '\0';
}

/* A value that is exactly one number. Large numbers are kept large, so the
   clamp later still sees them as too big. */
static bool save_parse_int(const char *value, int *out)
{
    uint64_t number = 0;
    if (!save_read_u64(&value, &number) || !save_at_end(value)) {
        return false;
    }
    *out = number > 1000000u ? 1000000 : (int)number;
    return true;
}

/* "a b" or just "a": the keys of one action. Out-of-range keys are emptied
   later by settings_clamp. */
static void save_parse_keys(const char *value, uint16_t keys[SETTINGS_KEYS_PER_ACTION])
{
    uint16_t parsed[SETTINGS_KEYS_PER_ACTION] = { SETTINGS_KEY_NONE, SETTINGS_KEY_NONE };
    const char *cursor = value;
    for (int slot = 0; slot < SETTINGS_KEYS_PER_ACTION; ++slot) {
        if (slot > 0 && save_at_end(cursor)) {
            break;
        }
        uint64_t key = 0;
        if (!save_read_u64(&cursor, &key)) {
            return;
        }
        parsed[slot] = key < SETTINGS_SCANCODE_LIMIT ? (uint16_t)key : SETTINGS_KEY_NONE;
    }
    if (!save_at_end(cursor)) {
        return;
    }
    memcpy(keys, parsed, sizeof parsed);
}

/* Reads exactly `count` numbers and nothing else. */
static bool save_read_numbers(const char *value, uint64_t *numbers, int count)
{
    const char *cursor = value;
    for (int i = 0; i < count; ++i) {
        if (!save_read_u64(&cursor, &numbers[i])) {
            return false;
        }
    }
    return save_at_end(cursor);
}

/* "score level lines seed", a Marathon run. A run that scored nothing or has
   no level is not a run the game could have recorded, so the line is skipped. */
static void save_parse_score(const char *value, SaveData *save)
{
    uint64_t numbers[4];
    if (!save_read_numbers(value, numbers, 4) || numbers[0] == 0 || numbers[0] > UINT32_MAX ||
        numbers[1] == 0 || numbers[1] > UINT32_MAX || numbers[2] > UINT32_MAX) {
        return;
    }
    const HighScore entry = { (uint32_t)numbers[0], (uint32_t)numbers[1],
                              (uint32_t)numbers[2], numbers[3], 0 };
    (void)save_insert_score(save, PLAY_MARATHON, &entry);
}

/* "ticks lines seed", a Sprint run; no ticks is not a time. */
static void save_parse_sprint(const char *value, SaveData *save)
{
    uint64_t numbers[3];
    if (!save_read_numbers(value, numbers, 3) || numbers[0] == 0 || numbers[1] > UINT32_MAX) {
        return;
    }
    const HighScore entry = { 0, 1, (uint32_t)numbers[1], numbers[2], numbers[0] };
    (void)save_insert_score(save, PLAY_SPRINT, &entry);
}

/* "score lines seed", an Ultra run; it lasted the whole limit or less, but the
   table ranks by score only, so the length is not kept. */
static void save_parse_ultra(const char *value, SaveData *save)
{
    uint64_t numbers[3];
    if (!save_read_numbers(value, numbers, 3) || numbers[0] == 0 || numbers[0] > UINT32_MAX ||
        numbers[1] > UINT32_MAX) {
        return;
    }
    const HighScore entry = { (uint32_t)numbers[0], 1, (uint32_t)numbers[1], numbers[2], 0 };
    (void)save_insert_score(save, PLAY_ULTRA, &entry);
}

static void save_apply_line(const char *key, const char *value, SaveData *save)
{
    Settings *settings = &save->settings;

    if (strcmp(key, "volume") == 0) {
        (void)save_parse_int(value, &settings->volume);
    } else if (strcmp(key, "music") == 0) {
        (void)save_parse_int(value, &settings->music);
    } else if (strcmp(key, "effects") == 0) {
        (void)save_parse_int(value, &settings->effects);
    } else if (strcmp(key, "bloom") == 0) {
        (void)save_parse_int(value, &settings->bloom);
    } else if (strcmp(key, "das") == 0) {
        (void)save_parse_int(value, &settings->das_ticks);
    } else if (strcmp(key, "arr") == 0) {
        (void)save_parse_int(value, &settings->arr_ticks);
    } else if (strcmp(key, "start_level") == 0) {
        (void)save_parse_int(value, &settings->start_level);
    } else if (strcmp(key, "mode") == 0) {
        for (int mode = 0; mode < PLAY_MODE_COUNT; ++mode) {
            if (strcmp(value, SAVE_MODE_KEYS[mode]) == 0) {
                settings->mode = mode;
            }
        }
    } else if (strcmp(key, "score") == 0) {
        save_parse_score(value, save);
    } else if (strcmp(key, "sprint") == 0) {
        save_parse_sprint(value, save);
    } else if (strcmp(key, "ultra") == 0) {
        save_parse_ultra(value, save);
    } else if (strncmp(key, "key.", 4) == 0) {
        for (int action = 0; action < BIND_ACTION_COUNT; ++action) {
            if (strcmp(key + 4, SAVE_KEY_NAMES[action]) == 0) {
                save_parse_keys(value, settings->keys[action]);
                break;
            }
        }
    }
}

bool save_parse(const char *text, size_t length, SaveData *out)
{
    save_defaults(out);
    if (text == NULL) {
        return false;
    }

    /* A byte order mark from a text editor is not part of the magic line. */
    if (length >= 3 && (unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB &&
        (unsigned char)text[2] == 0xBF) {
        text += 3;
        length -= 3;
    }

    SaveData parsed;
    save_defaults(&parsed);
    bool first_line = true;
    bool have_version = false;

    size_t pos = 0;
    while (pos < length) {
        size_t end = pos;
        while (end < length && text[end] != '\n') {
            ++end;
        }
        const size_t line_length = end - pos;
        char buffer[SAVE_MAX_LINE];
        const bool fits = line_length < sizeof buffer;
        if (fits) {
            memcpy(buffer, text + pos, line_length);
            buffer[line_length] = '\0';
        }
        pos = end + 1;

        if (first_line) {
            first_line = false;
            if (!fits || strcmp(save_trim(buffer), SAVE_MAGIC) != 0) {
                return false;
            }
            continue;
        }
        if (!fits) {
            continue;
        }

        char *line = save_trim(buffer);
        if (*line == '\0' || *line == '#') {
            continue;
        }
        char *equals = strchr(line, '=');
        if (equals == NULL) {
            continue;
        }
        *equals = '\0';
        const char *key = save_trim(line);
        const char *value = save_trim(equals + 1);

        if (strcmp(key, "version") == 0) {
            int version = 0;
            if (!save_parse_int(value, &version) || version != SAVE_FORMAT_VERSION) {
                return false;
            }
            have_version = true;
            continue;
        }
        save_apply_line(key, value, &parsed);
    }

    if (first_line || !have_version) {
        return false;
    }
    settings_clamp(&parsed.settings);
    *out = parsed;
    return true;
}

/* --- serializing --------------------------------------------------------- */

/* Appends one finished line to buffer at *used. Returns false once it is full. */
static bool save_append(char *buffer, size_t capacity, size_t *used, const char *line,
                        int line_length)
{
    if (line_length < 0 || (size_t)line_length >= capacity - *used) {
        return false;
    }
    memcpy(buffer + *used, line, (size_t)line_length + 1);
    *used += (size_t)line_length;
    return true;
}

size_t save_serialize(const SaveData *save, char *buffer, size_t capacity)
{
    if (buffer == NULL || capacity == 0) {
        return 0;
    }
    buffer[0] = '\0';

    Settings settings = save->settings;
    settings_clamp(&settings);

    size_t used = 0;
    char line[SAVE_MAX_LINE];
    int length = snprintf(line, sizeof line,
                          "%s\nversion=%d\nvolume=%d\nmusic=%d\neffects=%d\nbloom=%d\n"
                          "das=%d\narr=%d\nstart_level=%d\nmode=%s\n",
                          SAVE_MAGIC, SAVE_FORMAT_VERSION, settings.volume, settings.music,
                          settings.effects, settings.bloom, settings.das_ticks,
                          settings.arr_ticks, settings.start_level,
                          SAVE_MODE_KEYS[settings.mode]);
    bool ok = save_append(buffer, capacity, &used, line, length);

    for (int action = 0; ok && action < BIND_ACTION_COUNT; ++action) {
        length = snprintf(line, sizeof line, "key.%s=%u %u\n", SAVE_KEY_NAMES[action],
                          (unsigned)settings.keys[action][0], (unsigned)settings.keys[action][1]);
        ok = save_append(buffer, capacity, &used, line, length);
    }
    for (int mode = 0; ok && mode < PLAY_MODE_COUNT; ++mode) {
        const ScoreTable *table = &save->tables[mode];
        for (int i = 0; ok && i < table->count && i < SAVE_MAX_SCORES; ++i) {
            const HighScore *entry = &table->entries[i];
            if (mode == PLAY_SPRINT) {
                length = snprintf(line, sizeof line, "sprint=%llu %lu %llu\n",
                                  (unsigned long long)entry->ticks, (unsigned long)entry->lines,
                                  (unsigned long long)entry->seed);
            } else if (mode == PLAY_ULTRA) {
                length = snprintf(line, sizeof line, "ultra=%lu %lu %llu\n",
                                  (unsigned long)entry->score, (unsigned long)entry->lines,
                                  (unsigned long long)entry->seed);
            } else {
                length = snprintf(line, sizeof line, "score=%lu %lu %lu %llu\n",
                                  (unsigned long)entry->score, (unsigned long)entry->level,
                                  (unsigned long)entry->lines, (unsigned long long)entry->seed);
            }
            ok = save_append(buffer, capacity, &used, line, length);
        }
    }

    if (!ok) {
        buffer[0] = '\0';
        return 0;
    }
    return used;
}
