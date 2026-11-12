/*
 * Save file suite: the text parser and serializer, the score table and the key
 * bindings. Everything here works on strings in memory; the file itself is
 * src/app/save_file.c, which needs SDL and is not linked.
 */

#include <string.h>

#include "app/save.h"
#include "test_harness.h"

static bool save_parse_text(const char *text, SaveData *out)
{
    return save_parse(text, strlen(text), out);
}

static bool settings_equal(const Settings *a, const Settings *b)
{
    return a->volume == b->volume && a->music == b->music && a->effects == b->effects &&
           a->bloom == b->bloom &&
           a->das_ticks == b->das_ticks &&
           a->arr_ticks == b->arr_ticks && a->start_level == b->start_level &&
           memcmp(a->keys, b->keys, sizeof a->keys) == 0;
}

static bool scores_equal(const SaveData *a, const SaveData *b)
{
    for (int mode = 0; mode < PLAY_MODE_COUNT; ++mode) {
        const ScoreTable *ta = &a->tables[mode];
        const ScoreTable *tb = &b->tables[mode];
        if (ta->count != tb->count) {
            return false;
        }
        for (int i = 0; i < ta->count; ++i) {
            const HighScore *x = &ta->entries[i];
            const HighScore *y = &tb->entries[i];
            if (x->score != y->score || x->level != y->level || x->lines != y->lines ||
                x->seed != y->seed || x->ticks != y->ticks) {
                return false;
            }
        }
    }
    return true;
}

static void test_save_round_trip(void)
{
    SaveData save;
    save_defaults(&save);
    save.settings.volume = 3;
    save.settings.music = 2;
    save.settings.effects = 0;
    save.settings.bloom = 0;
    save.settings.das_ticks = 7;
    save.settings.arr_ticks = 0;
    save.settings.start_level = 12;
    CHECK(settings_bind_key(&save.settings, BIND_HOLD, 1, 4 /* A */));
    settings_clear_key(&save.settings, BIND_ROTATE_CCW, 1);

    const HighScore runs[] = {
        { 1200u, 3u, 25u, 1u, 0 },
        { 4294967295u, 15u, 4000u, UINT64_MAX, 0 },
        { 800u, 1u, 8u, 0x853c49e6748fea9bULL, 0 },
    };
    for (int i = 0; i < 3; ++i) {
        CHECK(save_insert_score(&save, PLAY_MARATHON, &runs[i]) >= 0);
    }

    char text[SAVE_TEXT_CAPACITY];
    const size_t length = save_serialize(&save, text, sizeof text);
    CHECK(length > 0);
    CHECK_EQ_INT(strlen(text), length);
    CHECK(strncmp(text, SAVE_MAGIC "\n", strlen(SAVE_MAGIC) + 1) == 0);

    SaveData loaded;
    CHECK(save_parse(text, length, &loaded));
    CHECK(settings_equal(&loaded.settings, &save.settings));
    CHECK(scores_equal(&loaded, &save));
    CHECK_EQ_INT(loaded.tables[PLAY_MARATHON].entries[0].score, 4294967295u);
    CHECK(loaded.tables[PLAY_MARATHON].entries[0].seed == UINT64_MAX);
}

static void test_save_defaults_when_missing_or_corrupt(void)
{
    SaveData defaults;
    save_defaults(&defaults);

    SaveData loaded;
    CHECK(!save_parse(NULL, 0, &loaded));
    CHECK(settings_equal(&loaded.settings, &defaults.settings));
    CHECK_EQ_INT(loaded.tables[PLAY_MARATHON].count, 0);

    CHECK(!save_parse_text("", &loaded));
    CHECK(settings_equal(&loaded.settings, &defaults.settings));

    /* Wrong magic line: nothing else in the file is trusted. */
    CHECK(!save_parse_text("NOT A SAVE\nversion=1\nvolume=2\nscore=500 1 4 9\n", &loaded));
    CHECK(settings_equal(&loaded.settings, &defaults.settings));
    CHECK_EQ_INT(loaded.tables[PLAY_MARATHON].count, 0);

    /* No version, or one this build does not know. */
    CHECK(!save_parse_text(SAVE_MAGIC "\nvolume=2\n", &loaded));
    CHECK_EQ_INT(loaded.settings.volume, defaults.settings.volume);
    CHECK(!save_parse_text(SAVE_MAGIC "\nversion=2\nvolume=2\n", &loaded));
    CHECK_EQ_INT(loaded.settings.volume, defaults.settings.volume);
    CHECK(!save_parse_text(SAVE_MAGIC "\nversion=one\n", &loaded));

    /* Binary junk after a valid header is ignored line by line. */
    const char junk[] = SAVE_MAGIC "\nversion=1\n\x01\x02\xff=\xfe\nvolume=4\n";
    CHECK(save_parse(junk, sizeof junk - 1, &loaded));
    CHECK_EQ_INT(loaded.settings.volume, 4);
}

static void test_save_parse_is_forgiving(void)
{
    SaveData defaults;
    save_defaults(&defaults);

    /* A byte order mark, CRLF line ends, comments, blank lines, spaces around
       the equals sign and unknown keys are all fine. */
    const char *text = "\xEF\xBB\xBF" SAVE_MAGIC "\r\n"
                       "# written by hand\r\n"
                       "\r\n"
                       "version = 1\r\n"
                       "  volume = 6  \r\n"
                       "colour=pink\r\n"
                       "das=12\r\n";
    SaveData loaded;
    CHECK(save_parse_text(text, &loaded));
    CHECK_EQ_INT(loaded.settings.volume, 6);
    CHECK_EQ_INT(loaded.settings.das_ticks, 12);
    CHECK_EQ_INT(loaded.settings.effects, defaults.settings.effects);

    /* Out of range values are clamped; malformed ones keep the default. */
    CHECK(save_parse_text(SAVE_MAGIC "\nversion=1\nvolume=99\ndas=0\narr=999999999999\n"
                                     "start_level=40\neffects=-3\n",
                          &loaded));
    CHECK_EQ_INT(loaded.settings.volume, SETTINGS_VOLUME_MAX);
    CHECK_EQ_INT(loaded.settings.das_ticks, SETTINGS_DAS_MIN);
    CHECK_EQ_INT(loaded.settings.arr_ticks, SETTINGS_ARR_MAX);
    CHECK_EQ_INT(loaded.settings.start_level, SETTINGS_START_LEVEL_MAX);
    CHECK_EQ_INT(loaded.settings.effects, defaults.settings.effects);

    CHECK(save_parse_text(SAVE_MAGIC "\nversion=1\nvolume=5x\narr=\n", &loaded));
    CHECK_EQ_INT(loaded.settings.volume, defaults.settings.volume);
    CHECK_EQ_INT(loaded.settings.arr_ticks, defaults.settings.arr_ticks);

    /* A line too long to be anything the game wrote is skipped. */
    char long_line[600];
    memset(long_line, '9', sizeof long_line);
    memcpy(long_line, SAVE_MAGIC "\nversion=1\nvolume=", 30);
    long_line[sizeof long_line - 1] = '\0';
    CHECK(save_parse_text(long_line, &loaded));
    CHECK_EQ_INT(loaded.settings.volume, defaults.settings.volume);
}

static void test_save_parse_keys(void)
{
    SaveData defaults;
    save_defaults(&defaults);
    SaveData loaded;

    /* One key or two; a missing second slot is empty. */
    CHECK(save_parse_text(SAVE_MAGIC "\nversion=1\nkey.hold=4\nkey.left=7 9\n", &loaded));
    CHECK_EQ_INT(loaded.settings.keys[BIND_HOLD][0], 4);
    CHECK_EQ_INT(loaded.settings.keys[BIND_HOLD][1], SETTINGS_KEY_NONE);
    CHECK_EQ_INT(loaded.settings.keys[BIND_LEFT][0], 7);
    CHECK_EQ_INT(loaded.settings.keys[BIND_LEFT][1], 9);

    /* Malformed lines keep the defaults; reserved and out-of-range keys are
       dropped. */
    CHECK(save_parse_text(SAVE_MAGIC "\nversion=1\nkey.right=left\nkey.hold=1 2 3\n"
                                     "key.soft_drop=41 600\n",
                          &loaded));
    CHECK_EQ_INT(loaded.settings.keys[BIND_RIGHT][0], defaults.settings.keys[BIND_RIGHT][0]);
    CHECK_EQ_INT(loaded.settings.keys[BIND_HOLD][0], defaults.settings.keys[BIND_HOLD][0]);
    CHECK_EQ_INT(loaded.settings.keys[BIND_SOFT_DROP][0], SETTINGS_KEY_NONE);
    CHECK_EQ_INT(loaded.settings.keys[BIND_SOFT_DROP][1], SETTINGS_KEY_NONE);

    /* A key bound twice keeps only its first slot. */
    CHECK(save_parse_text(SAVE_MAGIC "\nversion=1\nkey.left=44 0\n", &loaded));
    CHECK_EQ_INT(loaded.settings.keys[BIND_LEFT][0], SETTINGS_KEY_SPACE);
    CHECK_EQ_INT(loaded.settings.keys[BIND_HARD_DROP][0], SETTINGS_KEY_NONE);
    CHECK_EQ_INT(settings_actions_for_key(&loaded.settings, SETTINGS_KEY_SPACE), 1u << BIND_LEFT);
}

static void test_save_scores(void)
{
    SaveData loaded;

    /* Twelve valid runs, out of order, keep the best ten, best first. A run
       with no score or no level cannot have been recorded and is skipped. */
    char text[SAVE_TEXT_CAPACITY] = SAVE_MAGIC "\nversion=1\n";
    const unsigned scores[] = { 50, 900, 300, 1200, 10, 700, 1100, 600, 20, 1000, 800, 400 };
    for (int i = 0; i < 12; ++i) {
        char line[64];
        snprintf(line, sizeof line, "score=%u 2 %d 7\n", scores[i], i);
        strcat(text, line);
    }
    strcat(text, "score=0 1 0 1\nscore=99999 0 3 1\nscore=5 1 1\nscore=5 1 1 1 1\n");
    CHECK(save_parse_text(text, &loaded));
    CHECK_EQ_INT(loaded.tables[PLAY_MARATHON].count, SAVE_MAX_SCORES);
    CHECK_EQ_INT(loaded.tables[PLAY_MARATHON].entries[0].score, 1200);
    CHECK_EQ_INT(loaded.tables[PLAY_MARATHON].entries[SAVE_MAX_SCORES - 1].score, 50);
    for (int i = 1; i < loaded.tables[PLAY_MARATHON].count; ++i) {
        CHECK(loaded.tables[PLAY_MARATHON].entries[i - 1].score >= loaded.tables[PLAY_MARATHON].entries[i].score);
    }

    /* A tie goes below the run already in the table. */
    SaveData save;
    save_defaults(&save);
    const HighScore first = { 500u, 1u, 5u, 1u, 0 };
    const HighScore second = { 500u, 2u, 6u, 2u, 0 };
    CHECK_EQ_INT(save_insert_score(&save, PLAY_MARATHON, &first), 0);
    CHECK_EQ_INT(save_insert_score(&save, PLAY_MARATHON, &second), 1);
    CHECK(save.tables[PLAY_MARATHON].entries[0].seed == 1u);

    /* A full table only takes a run that beats its last entry. */
    for (uint32_t i = 0; i < SAVE_MAX_SCORES; ++i) {
        const HighScore entry = { 1000u + i, 1u, 0u, i, 0 };
        CHECK(save_insert_score(&save, PLAY_MARATHON, &entry) >= 0);
    }
    CHECK_EQ_INT(save.tables[PLAY_MARATHON].count, SAVE_MAX_SCORES);
    const HighScore low = { 1000u, 1u, 0u, 99u, 0 };
    CHECK(!save_score_qualifies(&save, PLAY_MARATHON, &low));
    CHECK_EQ_INT(save_insert_score(&save, PLAY_MARATHON, &low), -1);
    const HighScore best = { 5000u, 9u, 90u, 42u, 0 };
    CHECK_EQ_INT(save_insert_score(&save, PLAY_MARATHON, &best), 0);
    CHECK_EQ_INT(save.tables[PLAY_MARATHON].entries[SAVE_MAX_SCORES - 1].score, 1001);

    const HighScore nothing = { 0u, 1u, 0u, 0u, 0 };
    save_defaults(&save);
    CHECK_EQ_INT(save_insert_score(&save, PLAY_MARATHON, &nothing), -1);
    CHECK_EQ_INT(save.tables[PLAY_MARATHON].count, 0);
}

static void test_save_serialize_limits(void)
{
    SaveData save;
    save_defaults(&save);
    for (uint32_t i = 0; i < SAVE_MAX_SCORES; ++i) {
        const HighScore entry = { 4000000000u - i, 4000000000u, 4000000000u, UINT64_MAX, 0 };
        (void)save_insert_score(&save, PLAY_MARATHON, &entry);
    }

    /* The worst case fits the documented capacity, and a buffer that is too
       small gets an empty string rather than half a file. */
    char text[SAVE_TEXT_CAPACITY];
    CHECK(save_serialize(&save, text, sizeof text) > 0);
    char small[64];
    CHECK_EQ_INT(save_serialize(&save, small, sizeof small), 0);
    CHECK_EQ_INT(small[0], '\0');
}

static void test_settings_bind_key(void)
{
    Settings settings;
    settings_defaults(&settings);

    /* Binding a key that another action uses moves it. */
    CHECK(settings_bind_key(&settings, BIND_HOLD, 0, SETTINGS_KEY_SPACE));
    CHECK_EQ_INT(settings.keys[BIND_HOLD][0], SETTINGS_KEY_SPACE);
    CHECK_EQ_INT(settings.keys[BIND_HARD_DROP][0], SETTINGS_KEY_NONE);
    CHECK_EQ_INT(settings_actions_for_key(&settings, SETTINGS_KEY_SPACE), 1u << BIND_HOLD);

    /* The app's own keys and out-of-range keys are refused. */
    CHECK(!settings_bind_key(&settings, BIND_LEFT, 0, SETTINGS_KEY_ESCAPE));
    CHECK(!settings_bind_key(&settings, BIND_LEFT, 0, SETTINGS_KEY_P));
    CHECK(!settings_bind_key(&settings, BIND_LEFT, 0, SETTINGS_SCANCODE_LIMIT));
    CHECK(!settings_bind_key(&settings, BIND_LEFT, 2, 4));
    CHECK_EQ_INT(settings.keys[BIND_LEFT][0], SETTINGS_KEY_LEFT);

    settings_default_keys(&settings);
    CHECK_EQ_INT(settings.keys[BIND_HARD_DROP][0], SETTINGS_KEY_SPACE);
    CHECK_EQ_INT(settings_actions_for_key(&settings, SETTINGS_KEY_UP), 1u << BIND_ROTATE_CW);
    CHECK_EQ_INT(settings_actions_for_key(&settings, SETTINGS_KEY_NONE), 0);
}

void test_save_suite(void)
{
    test_save_round_trip();
    test_save_defaults_when_missing_or_corrupt();
    test_save_parse_is_forgiving();
    test_save_parse_keys();
    test_save_scores();
    test_save_serialize_limits();
    test_settings_bind_key();
}
