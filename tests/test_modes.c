/*
 * Milestone 8, game modes: the line goal and the tick limit in the core, the
 * fixed level, the new end reasons, replaying a Sprint, and the per-mode score
 * tables, save keys and mode settings in the app's save data.
 */

#include <string.h>

#include "app/menu.h"
#include "app/save.h"
#include "core/game.h"
#include "test_harness.h"

static GameInput input_none(void)
{
    const GameInput input = { 0, 0 };
    return input;
}

static GameConfig config_with(uint32_t line_goal, uint32_t tick_limit, bool level_up)
{
    GameConfig config = game_default_config();
    config.line_goal = line_goal;
    config.tick_limit = tick_limit;
    config.level_up = level_up;
    return config;
}

/* Puts a piece somewhere by hand with clean per-piece counters. */
static void place(GameState *game, PieceType type, int rotation, int x, int y)
{
    game->piece.type = type;
    game->piece.rotation = (int8_t)rotation;
    game->piece.x = (int8_t)x;
    game->piece.y = (int8_t)y;
    game->gravity_accum = 0;
    game->lock_counter = 0;
    game->lock_resets = 0;
    game->shift_dir = 0;
    game->shift_counter = 0;
    game->last_action_was_rotation = false;
    game->lowest_y = 0;
    game->grounded = true;
}

/*
 * Clears `rows` rows at once with a vertical I in column 9, with one block left
 * above them so it is never a perfect clear. Returns after the lock tick.
 */
static void clear_rows(GameState *game, int rows)
{
    board_clear(&game->board);
    for (int y = 0; y < rows; ++y) {
        for (int x = 0; x < BOARD_WIDTH - 1; ++x) {
            board_set(&game->board, x, y, CELL_J);
        }
    }
    board_set(&game->board, 0, rows, CELL_J);
    place(game, PIECE_I, 1, BOARD_WIDTH - 3, 0);
    const GameInput drop = { ACTION_HARD_DROP, ACTION_HARD_DROP };
    game_step(game, &drop);
}

static void finish_clear(GameState *game)
{
    const GameInput none = input_none();
    for (int i = 0; i < 100 && game->phase == GAME_PHASE_CLEARING; ++i) {
        game_step(game, &none);
    }
}

/* --- the line goal ------------------------------------------------------- */

static void test_line_goal_ends_on_the_lock(void)
{
    GameState game;
    const GameConfig config = config_with(PLAY_SPRINT_LINES, 0, false);
    game_init_with_config(&game, 3, &config);

    /* 36 lines and a four-line clear: exactly 40 finishes on the lock tick,
       with the rows gone at once and no clear delay. */
    game.lines_cleared = 36;
    const uint64_t before = game.tick;
    clear_rows(&game, 4);
    CHECK(game.game_over);
    CHECK_EQ_INT(game.game_over_reason, GAME_OVER_LINE_GOAL);
    CHECK(game_run_completed(&game));
    CHECK((game.events & GAME_EVENT_GAME_OVER) != 0);
    CHECK((game.events & GAME_EVENT_LINE_CLEAR) != 0);
    CHECK_EQ_INT(game.lines_cleared, 40);
    CHECK(game.phase == GAME_PHASE_FALLING);
    CHECK_EQ_INT(board_get(&game.board, 0, 0), CELL_J); /* the block above fell */
    CHECK_EQ_INT(board_get(&game.board, 5, 0), CELL_EMPTY);
    CHECK(game.end_tick == before + 1u);
    CHECK(game_run_ticks(&game) == before + 1u);

    /* Later steps only move the tick; the run's length stays put. */
    const GameInput none = input_none();
    game_step(&game, &none);
    CHECK(game.tick == before + 2u);
    CHECK(game_run_ticks(&game) == before + 1u);

    /* A clear that goes past the goal finishes too. */
    game_init_with_config(&game, 3, &config);
    game.lines_cleared = 38;
    clear_rows(&game, 4);
    CHECK_EQ_INT(game.game_over_reason, GAME_OVER_LINE_GOAL);
    CHECK_EQ_INT(game.lines_cleared, 42);

    /* One short of it is an ordinary clear. */
    game_init_with_config(&game, 3, &config);
    game.lines_cleared = 35;
    clear_rows(&game, 4);
    CHECK(!game.game_over);
    CHECK(game.phase == GAME_PHASE_CLEARING);
    CHECK_EQ_INT(game.lines_cleared, 39);

    /* Without a goal, Marathon never finishes on lines. */
    GameState marathon;
    game_init(&marathon, 3);
    marathon.lines_cleared = 1000;
    clear_rows(&marathon, 4);
    CHECK(!marathon.game_over);
}

/* --- the tick limit ------------------------------------------------------ */

static void test_tick_limit_ends_on_its_tick(void)
{
    GameState game;
    const GameConfig config = config_with(0, 100, false);
    game_init_with_config(&game, 4, &config);

    const GameInput none = input_none();
    for (int i = 0; i < 99; ++i) {
        game_step(&game, &none);
    }
    CHECK(!game.game_over);
    game_step(&game, &none);
    CHECK(game.game_over);
    CHECK_EQ_INT(game.game_over_reason, GAME_OVER_TIME_UP);
    CHECK(game_run_completed(&game));
    CHECK((game.events & GAME_EVENT_GAME_OVER) != 0);
    CHECK(game.tick == 100u);
    CHECK(game.end_tick == 100u);
    game_step(&game, &none);
    CHECK(game_run_ticks(&game) == 100u);

    /* The limit ends the run mid-clear too, and the clear keeps its points. */
    game_init_with_config(&game, 4, &config);
    game.tick = 90;
    clear_rows(&game, 2);
    CHECK(game.phase == GAME_PHASE_CLEARING);
    const uint32_t points = game.score;
    CHECK(points > 0);
    finish_clear(&game);
    CHECK(game.game_over);
    CHECK_EQ_INT(game.game_over_reason, GAME_OVER_TIME_UP);
    CHECK(game.phase == GAME_PHASE_CLEARING);
    CHECK_EQ_INT(game.score, points);
    CHECK(game.end_tick == 100u);

    /* A top-out before the limit is a lost run, not a finished one. */
    game_init_with_config(&game, 4, &config);
    for (int y = 0; y < BOARD_HEIGHT; ++y) {
        board_set(&game.board, 4, y, CELL_J);
    }
    game_spawn_next(&game);
    CHECK(game.game_over);
    CHECK_EQ_INT(game.game_over_reason, GAME_OVER_BLOCK_OUT);
    CHECK(!game_run_completed(&game));
}

static void test_fixed_level(void)
{
    /* Ten lines level a Marathon up, but not a run without level_up. */
    GameState levelling;
    GameState fixed;
    const GameConfig on = config_with(0, 0, true);
    const GameConfig off = config_with(0, 0, false);
    game_init_with_config(&levelling, 5, &on);
    game_init_with_config(&fixed, 5, &off);
    levelling.lines_cleared = 9;
    fixed.lines_cleared = 9;
    clear_rows(&levelling, 1);
    clear_rows(&fixed, 1);
    CHECK_EQ_INT(levelling.level, 2);
    CHECK((levelling.events & GAME_EVENT_LEVEL_UP) != 0);
    CHECK_EQ_INT(fixed.level, 1);
    CHECK((fixed.events & GAME_EVENT_LEVEL_UP) == 0);
}

/* --- replays --------------------------------------------------------------- */

/* A two-line Sprint: a double after some idle ticks, then another double. */
static void run_short_sprint(GameState *game)
{
    const GameConfig config = config_with(4, 0, false);
    game_init_with_config(game, 11, &config);
    const GameInput none = input_none();
    for (int i = 0; i < 37; ++i) {
        game_step(game, &none);
    }
    clear_rows(game, 2);
    finish_clear(game);
    for (int i = 0; i < 12; ++i) {
        game_step(game, &none);
    }
    clear_rows(game, 2);
}

static void test_sprint_replays_to_the_tick(void)
{
    GameState first;
    GameState second;
    run_short_sprint(&first);
    run_short_sprint(&second);
    CHECK_EQ_INT(first.game_over_reason, GAME_OVER_LINE_GOAL);
    CHECK(game_run_ticks(&first) == game_run_ticks(&second));
    CHECK(memcmp(&first, &second, sizeof first) == 0);
    /* 37 idle, the first lock, the clear delay, 12 idle and the last lock. */
    CHECK(game_run_ticks(&first) == 37u + 1u + GAME_LINE_CLEAR_DELAY_TICKS + 12u + 1u);
}

/* --- mode settings and the score tables -------------------------------------- */

static void test_mode_configs(void)
{
    Settings settings;
    settings_defaults(&settings);
    CHECK_EQ_INT(settings.mode, PLAY_MARATHON);
    settings.start_level = 7;
    settings.das_ticks = 8;
    settings.arr_ticks = 0;

    GameConfig config = settings_game_config(&settings);
    CHECK_EQ_INT(config.start_level, 7);
    CHECK_EQ_INT(config.das_ticks, 8);
    CHECK_EQ_INT(config.arr_ticks, 0);
    CHECK_EQ_INT(config.line_goal, 0);
    CHECK_EQ_INT(config.tick_limit, 0);
    CHECK(config.level_up);

    /* Sprint and Ultra start at level 1 whatever the picker says. */
    settings.mode = PLAY_SPRINT;
    config = settings_game_config(&settings);
    CHECK_EQ_INT(config.start_level, 1);
    CHECK_EQ_INT(config.line_goal, PLAY_SPRINT_LINES);
    CHECK_EQ_INT(config.tick_limit, 0);
    CHECK(!config.level_up);
    CHECK_EQ_INT(config.das_ticks, 8);

    settings.mode = PLAY_ULTRA;
    config = settings_game_config(&settings);
    CHECK_EQ_INT(config.start_level, 1);
    CHECK_EQ_INT(config.line_goal, 0);
    CHECK_EQ_INT(config.tick_limit, PLAY_ULTRA_TICKS);
    CHECK(!config.level_up);
}

static void test_sprint_ranks_by_time(void)
{
    SaveData save;
    save_defaults(&save);
    const HighScore runs[] = {
        { 0, 1, 40, 1, 500 }, { 0, 1, 40, 2, 300 }, { 0, 1, 41, 3, 700 }, { 0, 1, 40, 4, 300 },
    };
    CHECK_EQ_INT(save_insert_score(&save, PLAY_SPRINT, &runs[0]), 0);
    CHECK_EQ_INT(save_insert_score(&save, PLAY_SPRINT, &runs[1]), 0);
    CHECK_EQ_INT(save_insert_score(&save, PLAY_SPRINT, &runs[2]), 2);
    /* A tie goes below the earlier run. */
    CHECK_EQ_INT(save_insert_score(&save, PLAY_SPRINT, &runs[3]), 1);
    const ScoreTable *sprint = &save.tables[PLAY_SPRINT];
    CHECK_EQ_INT(sprint->count, 4);
    CHECK(sprint->entries[0].seed == 2u && sprint->entries[1].seed == 4u);
    CHECK(sprint->entries[3].ticks == 700u);

    /* No time is not a run; the other tables are untouched. */
    const HighScore untimed = { 0, 1, 40, 5, 0 };
    CHECK_EQ_INT(save_insert_score(&save, PLAY_SPRINT, &untimed), -1);
    CHECK_EQ_INT(save.tables[PLAY_MARATHON].count, 0);
    CHECK_EQ_INT(save.tables[PLAY_ULTRA].count, 0);

    /* A full table only takes a faster run. */
    for (uint64_t i = 0; i < SAVE_MAX_SCORES; ++i) {
        const HighScore run = { 0, 1, 40, 10 + i, 100 + i };
        (void)save_insert_score(&save, PLAY_SPRINT, &run);
    }
    const HighScore slow = { 0, 1, 40, 99, 5000 };
    CHECK(!save_score_qualifies(&save, PLAY_SPRINT, &slow));
    const HighScore fast = { 0, 1, 40, 98, 50 };
    CHECK_EQ_INT(save_insert_score(&save, PLAY_SPRINT, &fast), 0);
}

static void test_record_run(void)
{
    SaveData save;
    save_defaults(&save);
    GameState game;

    /* A finished Sprint records its time and lines; a lost one nothing. */
    const GameConfig sprint = config_with(4, 0, false);
    game_init_with_config(&game, 6, &sprint);
    game.lines_cleared = 2;
    game.score = 900;
    clear_rows(&game, 2);
    CHECK_EQ_INT(save_record_run(&save, PLAY_SPRINT, &game, 77), 0);
    const HighScore *best = &save.tables[PLAY_SPRINT].entries[0];
    CHECK(best->ticks == game_run_ticks(&game));
    CHECK_EQ_INT(best->lines, 4);
    CHECK_EQ_INT(best->score, 0);
    CHECK(best->seed == 77u);

    game_init_with_config(&game, 6, &sprint);
    for (int y = 0; y < BOARD_HEIGHT; ++y) {
        board_set(&game.board, 4, y, CELL_J);
    }
    game_spawn_next(&game);
    CHECK_EQ_INT(game.game_over_reason, GAME_OVER_BLOCK_OUT);
    CHECK_EQ_INT(save_record_run(&save, PLAY_SPRINT, &game, 78), -1);
    CHECK_EQ_INT(save.tables[PLAY_SPRINT].count, 1);

    /* An Ultra that topped out keeps its score. */
    game.score = 4321;
    game.lines_cleared = 12;
    CHECK_EQ_INT(save_record_run(&save, PLAY_ULTRA, &game, 79), 0);
    CHECK_EQ_INT(save.tables[PLAY_ULTRA].entries[0].score, 4321);
    CHECK_EQ_INT(save.tables[PLAY_ULTRA].entries[0].level, 1);

    /* A Marathon keeps score, level and lines. */
    game.level = 6;
    CHECK_EQ_INT(save_record_run(&save, PLAY_MARATHON, &game, 80), 0);
    CHECK_EQ_INT(save.tables[PLAY_MARATHON].entries[0].level, 6);
}

static void test_save_keys_for_modes(void)
{
    SaveData save;
    save_defaults(&save);
    save.settings.mode = PLAY_ULTRA;
    const HighScore marathon = { 1200, 3, 25, 1, 0 };
    const HighScore sprint = { 0, 1, 40, 2, 5832 };
    const HighScore ultra = { 48210, 1, 61, 3, 0 };
    (void)save_insert_score(&save, PLAY_MARATHON, &marathon);
    (void)save_insert_score(&save, PLAY_SPRINT, &sprint);
    (void)save_insert_score(&save, PLAY_ULTRA, &ultra);

    char text[SAVE_TEXT_CAPACITY];
    const size_t length = save_serialize(&save, text, sizeof text);
    CHECK(length > 0);
    CHECK(strstr(text, "mode=ultra\n") != NULL);
    CHECK(strstr(text, "score=1200 3 25 1\n") != NULL);
    CHECK(strstr(text, "sprint=5832 40 2\n") != NULL);
    CHECK(strstr(text, "ultra=48210 61 3\n") != NULL);

    SaveData loaded;
    CHECK(save_parse(text, length, &loaded));
    CHECK_EQ_INT(loaded.settings.mode, PLAY_ULTRA);
    /* Field by field: HighScore has padding, which memcmp would compare too. */
    for (int mode = 0; mode < PLAY_MODE_COUNT; ++mode) {
        CHECK_EQ_INT(loaded.tables[mode].count, 1);
        const HighScore *a = &loaded.tables[mode].entries[0];
        const HighScore *b = &save.tables[mode].entries[0];
        CHECK(a->score == b->score && a->level == b->level && a->lines == b->lines &&
              a->seed == b->seed && a->ticks == b->ticks);
    }

    /* An unknown mode name keeps the default; malformed runs are skipped. */
    const char *odd = SAVE_MAGIC "\nversion=1\nmode=zen\nsprint=0 40 1\nsprint=10 40\n"
                                 "ultra=0 5 1\nultra=7 x 1\n";
    CHECK(save_parse(odd, strlen(odd), &loaded));
    CHECK_EQ_INT(loaded.settings.mode, PLAY_MARATHON);
    CHECK_EQ_INT(loaded.tables[PLAY_SPRINT].count, 0);
    CHECK_EQ_INT(loaded.tables[PLAY_ULTRA].count, 0);
}

static void test_old_save_file(void)
{
    /* A save from 0.5.0: no mode, and its scores are Marathon runs. */
    const char *old = SAVE_MAGIC "\nversion=1\nvolume=7\nmusic=7\neffects=10\nbloom=1\ndas=10\n"
                                 "arr=2\nstart_level=3\nkey.left=80 0\n"
                                 "score=12400 4 31 9600400441103183515\n"
                                 "score=800 1 8 9600400441103183516\n";
    SaveData loaded;
    CHECK(save_parse(old, strlen(old), &loaded));
    CHECK_EQ_INT(loaded.settings.mode, PLAY_MARATHON);
    CHECK_EQ_INT(loaded.settings.start_level, 3);
    CHECK_EQ_INT(loaded.tables[PLAY_MARATHON].count, 2);
    CHECK_EQ_INT(loaded.tables[PLAY_MARATHON].entries[0].score, 12400);
    CHECK_EQ_INT(loaded.tables[PLAY_SPRINT].count, 0);
    CHECK_EQ_INT(loaded.tables[PLAY_ULTRA].count, 0);
}

static void test_time_format(void)
{
    char text[24];
    menu_format_ticks(0, text, sizeof text);
    CHECK(strcmp(text, "0:00.00") == 0);
    menu_format_ticks(59, text, sizeof text); /* 98.3 hundredths, rounded down */
    CHECK(strcmp(text, "0:00.98") == 0);
    menu_format_ticks(5832, text, sizeof text); /* 97.2 seconds */
    CHECK(strcmp(text, "1:37.20") == 0);
    menu_format_ticks(PLAY_ULTRA_TICKS, text, sizeof text);
    CHECK(strcmp(text, "2:00.00") == 0);
}

void test_modes_suite(void)
{
    test_line_goal_ends_on_the_lock();
    test_tick_limit_ends_on_its_tick();
    test_fixed_level();
    test_sprint_replays_to_the_tick();
    test_mode_configs();
    test_sprint_ranks_by_time();
    test_record_run();
    test_save_keys_for_modes();
    test_old_save_file();
    test_time_format();
}
