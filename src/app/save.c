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
    settings->effects = settings_clamp_int(settings->effects, 0, SETTINGS_EFFECTS_MAX);
    settings->bloom = settings_clamp_int(settings->bloom, 0, 1);
    settings->das_ticks =
        settings_clamp_int(settings->das_ticks, SETTINGS_DAS_MIN, SETTINGS_DAS_MAX);
    settings->arr_ticks = settings_clamp_int(settings->arr_ticks, 0, SETTINGS_ARR_MAX);
    settings->start_level =
        settings_clamp_int(settings->start_level, 1, SETTINGS_START_LEVEL_MAX);

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

bool save_score_qualifies(const SaveData *save, uint32_t score)
{
    if (score == 0) {
        return false;
    }
    if (save->score_count < SAVE_MAX_SCORES) {
        return true;
    }
    return score > save->scores[SAVE_MAX_SCORES - 1].score;
}

int save_insert_score(SaveData *save, const HighScore *entry)
{
    if (!save_score_qualifies(save, entry->score)) {
        return -1;
    }

    /* Below every entry with the same or a better score. */
    int place = 0;
    while (place < save->score_count && save->scores[place].score >= entry->score) {
        ++place;
    }

    const int count = save->score_count < SAVE_MAX_SCORES ? save->score_count + 1
                                                          : SAVE_MAX_SCORES;
    for (int i = count - 1; i > place; --i) {
        save->scores[i] = save->scores[i - 1];
    }
    save->scores[place] = *entry;
    save->score_count = count;
    return place;
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

/* "score level lines seed". A run that scored nothing or has no level is not
   a run the game could have recorded, so the line is skipped. */
static void save_parse_score(const char *value, SaveData *save)
{
    uint64_t numbers[4];
    const char *cursor = value;
    for (int i = 0; i < 4; ++i) {
        if (!save_read_u64(&cursor, &numbers[i])) {
            return;
        }
    }
    if (!save_at_end(cursor) || numbers[0] == 0 || numbers[0] > UINT32_MAX ||
        numbers[1] == 0 || numbers[1] > UINT32_MAX || numbers[2] > UINT32_MAX) {
        return;
    }

    const HighScore entry = { (uint32_t)numbers[0], (uint32_t)numbers[1],
                              (uint32_t)numbers[2], numbers[3] };
    (void)save_insert_score(save, &entry);
}

static void save_apply_line(const char *key, const char *value, SaveData *save)
{
    Settings *settings = &save->settings;

    if (strcmp(key, "volume") == 0) {
        (void)save_parse_int(value, &settings->volume);
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
    } else if (strcmp(key, "score") == 0) {
        save_parse_score(value, save);
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
                          "%s\nversion=%d\nvolume=%d\neffects=%d\nbloom=%d\ndas=%d\narr=%d\n"
                          "start_level=%d\n",
                          SAVE_MAGIC, SAVE_FORMAT_VERSION, settings.volume, settings.effects,
                          settings.bloom, settings.das_ticks, settings.arr_ticks,
                          settings.start_level);
    bool ok = save_append(buffer, capacity, &used, line, length);

    for (int action = 0; ok && action < BIND_ACTION_COUNT; ++action) {
        length = snprintf(line, sizeof line, "key.%s=%u %u\n", SAVE_KEY_NAMES[action],
                          (unsigned)settings.keys[action][0], (unsigned)settings.keys[action][1]);
        ok = save_append(buffer, capacity, &used, line, length);
    }
    for (int i = 0; ok && i < save->score_count && i < SAVE_MAX_SCORES; ++i) {
        const HighScore *entry = &save->scores[i];
        length = snprintf(line, sizeof line, "score=%lu %lu %lu %llu\n",
                          (unsigned long)entry->score, (unsigned long)entry->level,
                          (unsigned long)entry->lines, (unsigned long long)entry->seed);
        ok = save_append(buffer, capacity, &used, line, length);
    }

    if (!ok) {
        buffer[0] = '\0';
        return 0;
    }
    return used;
}
