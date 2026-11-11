/*
 * Milestone 6 scoring: the T-spin rule, mini T-spins, back-to-back, combos,
 * perfect clears, the clear label text and replay determinism with spins.
 *
 * Boards are built by hand, y-up like the game: row 0 is the bottom row.
 */

#include <string.h>

#include "core/game.h"
#include "fx/fx.h"
#include "test_harness.h"

static GameInput input_none(void)
{
    const GameInput input = { 0, 0 };
    return input;
}

static GameInput input_press(uint32_t actions)
{
    const GameInput input = { actions, actions };
    return input;
}

/* Puts the active piece somewhere by hand, with clean per-piece counters. */
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
    game->last_kick_index = 0;
    game->last_action_was_rotation = false;
    game->lowest_y = 0;
    game->grounded = true;
}

/* Hard-drops the active piece and returns what the lock scored. */
static uint32_t hard_drop(GameState *game)
{
    const GameInput drop = input_press(ACTION_HARD_DROP);
    const uint32_t before = game->score;
    game_step(game, &drop);
    CHECK((game->events & GAME_EVENT_LOCK) != 0);
    return game->score - before;
}

/* Lets the line-clear delay run out, so the next lock can happen. */
static void finish_clear(GameState *game)
{
    const GameInput none = input_none();
    for (int i = 0; i < 100 && game->phase == GAME_PHASE_CLEARING; ++i) {
        game_step(game, &none);
    }
    CHECK(game->phase == GAME_PHASE_FALLING);
}

/*
 * A T-spin double slot at columns 3-5:
 *
 *     row 2   ...X......    overhang at (3, 2)
 *     row 1   XXX...XXXX
 *     row 0   XXXX.XXXXX
 *
 * A T pointing right at (3, 0) fits; turning it clockwise points it down into
 * the slot with no kick. Corners around its center (4, 1): (3, 2), (3, 0) and
 * (5, 0) are filled, and the two in front, (3, 0) and (5, 0), are both filled,
 * so it is a full T-spin double.
 */
static void build_tsd(GameState *game)
{
    board_clear(&game->board);
    for (int x = 0; x < BOARD_WIDTH; ++x) {
        if (x != 4) {
            board_set(&game->board, x, 0, CELL_J);
        }
        if (x < 3 || x > 5) {
            board_set(&game->board, x, 1, CELL_J);
        }
    }
    board_set(&game->board, 3, 2, CELL_J);
    place(game, PIECE_T, 1, 3, 0);
}

/* Spins the T into the slot with a real rotation, then drops it in place. */
static uint32_t do_tsd(GameState *game)
{
    build_tsd(game);
    const GameInput spin = input_press(ACTION_ROTATE_CW);
    game_step(game, &spin);
    CHECK_EQ_INT(game->piece.rotation, 2);
    CHECK(game->last_action_was_rotation);
    return hard_drop(game);
}

/* A lock that clears nothing: an O on an empty floor. */
static uint32_t do_quiet_lock(GameState *game)
{
    board_clear(&game->board);
    board_set(&game->board, 9, 0, CELL_J); /* not a perfect clear either way */
    place(game, PIECE_O, 0, 0, 0);
    return hard_drop(game);
}

/* A plain line clear: a flat I into row 0, with a block left above it. */
static uint32_t do_single(GameState *game)
{
    board_clear(&game->board);
    for (int x = 4; x < BOARD_WIDTH; ++x) {
        board_set(&game->board, x, 0, CELL_J);
    }
    board_set(&game->board, 9, 1, CELL_J);
    place(game, PIECE_I, 0, 0, -2); /* the flat I's cells are on dy 2 */
    return hard_drop(game);
}

/* Four lines with a vertical I in column 9; keep_block leaves one cell above. */
static uint32_t do_quad(GameState *game, bool keep_block)
{
    board_clear(&game->board);
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < BOARD_WIDTH - 1; ++x) {
            board_set(&game->board, x, y, CELL_J);
        }
    }
    if (keep_block) {
        board_set(&game->board, 0, 4, CELL_J);
    }
    place(game, PIECE_I, 1, BOARD_WIDTH - 3, 0);
    return hard_drop(game);
}

/* --- the T-spin rule ----------------------------------------------------- */

static void test_tspin_rule_corners(void)
{
    Board board;
    board_clear(&board);
    ActivePiece t = { PIECE_T, 0, 3, 3 }; /* center (4, 4), corners at x 3/5, y 3/5 */

    /* Not a rotation, or not a T: never a T-spin. */
    board_set(&board, 3, 5, CELL_J);
    board_set(&board, 5, 5, CELL_J);
    board_set(&board, 3, 3, CELL_J);
    CHECK_EQ_INT(game_tspin_kind(&board, &t, false, 0), GAME_TSPIN_NONE);
    ActivePiece s = t;
    s.type = PIECE_S;
    CHECK_EQ_INT(game_tspin_kind(&board, &s, true, 0), GAME_TSPIN_NONE);

    /* Three corners with both front ones (pointing up: top-left, top-right). */
    CHECK_EQ_INT(game_tspin_kind(&board, &t, true, 0), GAME_TSPIN_FULL);

    /* Two corners are not enough. */
    board_set(&board, 3, 3, CELL_EMPTY);
    CHECK_EQ_INT(game_tspin_kind(&board, &t, true, 0), GAME_TSPIN_NONE);

    /* Three corners, but only one of the front ones: a mini, unless the
       rotation took the fifth kick. */
    board_set(&board, 5, 5, CELL_EMPTY);
    board_set(&board, 3, 3, CELL_J);
    board_set(&board, 5, 3, CELL_J);
    CHECK_EQ_INT(game_tspin_kind(&board, &t, true, 0), GAME_TSPIN_MINI);
    CHECK_EQ_INT(game_tspin_kind(&board, &t, true, 3), GAME_TSPIN_MINI);
    CHECK_EQ_INT(game_tspin_kind(&board, &t, true, 4), GAME_TSPIN_FULL);

    /* The front follows the rotation: pointing down, the bottom corners are
       the front, so the same board is a full T-spin. */
    t.rotation = 2;
    CHECK_EQ_INT(game_tspin_kind(&board, &t, true, 0), GAME_TSPIN_FULL);
    /* Pointing right the front is top-right and bottom-right: one is empty. */
    t.rotation = 1;
    CHECK_EQ_INT(game_tspin_kind(&board, &t, true, 0), GAME_TSPIN_MINI);
    /* Pointing left: top-left and bottom-left, both filled. */
    t.rotation = 3;
    CHECK_EQ_INT(game_tspin_kind(&board, &t, true, 0), GAME_TSPIN_FULL);
}

static void test_tspin_rule_walls_and_floor_count(void)
{
    Board board;
    board_clear(&board);

    /* Pointing up on the floor: both bottom corners are below the board. One
       top corner filled makes three, but the front pair is incomplete. */
    ActivePiece t = { PIECE_T, 0, 0, -1 }; /* center (1, 0) */
    board_set(&board, 0, 1, CELL_J);
    CHECK_EQ_INT(game_tspin_kind(&board, &t, true, 0), GAME_TSPIN_MINI);

    /* Pointing left against the left wall: both left corners are outside, and
       one right corner filled makes three with the front pair complete. */
    board_clear(&board);
    ActivePiece left = { PIECE_T, 3, -1, 5 }; /* center (0, 6) */
    board_set(&board, 1, 7, CELL_J);
    CHECK_EQ_INT(game_tspin_kind(&board, &left, true, 0), GAME_TSPIN_FULL);
}

/* --- scoring ------------------------------------------------------------- */

static void test_tspin_double_scores_1200(void)
{
    GameState game;
    game_init(&game, 1);
    const uint32_t points = do_tsd(&game);

    CHECK_EQ_INT(points, 1200);
    CHECK_EQ_INT(game.last_clear.points, 1200);
    CHECK_EQ_INT(game.last_clear.lines, 2);
    CHECK_EQ_INT(game.last_clear.tspin, GAME_TSPIN_FULL);
    CHECK(!game.last_clear.back_to_back);
    CHECK(!game.last_clear.perfect_clear);
    CHECK_EQ_INT(game.last_clear.combo, 0);
    CHECK(game.back_to_back);

    /* At level 3 the whole lock is three times as much. */
    GameState fast;
    game_init_at_level(&fast, 1, 3);
    CHECK_EQ_INT(do_tsd(&fast), 3600);
}

static void test_tspin_without_lines_and_minis(void)
{
    /* The T-spin double slot with row 0 left open: a T-spin that clears
       nothing scores 400 and does not touch the back-to-back state. */
    GameState game;
    game_init(&game, 1);
    build_tsd(&game);
    board_set(&game.board, 0, 0, CELL_EMPTY);
    board_set(&game.board, 0, 1, CELL_EMPTY);
    const GameInput spin = input_press(ACTION_ROTATE_CW);
    game_step(&game, &spin);
    CHECK_EQ_INT(hard_drop(&game), 400);
    CHECK_EQ_INT(game.last_clear.tspin, GAME_TSPIN_FULL);
    CHECK_EQ_INT(game.last_clear.lines, 0);
    CHECK(!game.back_to_back);

    /* A T pointing up on the floor next to one block: a mini. With row 0 full
       apart from the T it is a mini single (200), with the fifth kick a full
       T-spin single (800). */
    for (int kick = 0; kick <= 4; kick += 4) {
        GameState mini;
        game_init(&mini, 1);
        board_clear(&mini.board);
        for (int x = 3; x < BOARD_WIDTH; ++x) {
            board_set(&mini.board, x, 0, CELL_J);
        }
        board_set(&mini.board, 0, 1, CELL_J);
        place(&mini, PIECE_T, 0, 0, -1);
        mini.last_action_was_rotation = true;
        mini.last_kick_index = (int8_t)kick;

        CHECK_EQ_INT(hard_drop(&mini), kick == 4 ? 800 : 200);
        CHECK_EQ_INT(mini.last_clear.tspin, kick == 4 ? GAME_TSPIN_FULL : GAME_TSPIN_MINI);
        CHECK_EQ_INT(mini.last_clear.lines, 1);
    }
}

/*
 * A T pointing right into a two-row slot:
 *
 *     row 2   ...X......    corner (3, 2); (5, 2) stays open
 *     row 1   XXXX..XXXX
 *     row 0   XXXX.XXXXX
 *
 * At (3, 0) its corners (3, 2), (3, 0) and (5, 0) are filled, but the front
 * pair (top-right and bottom-right) is not, so a spin there is a mini T-spin
 * double. Unlike a T pointing down, it also fits one row higher.
 */
static void build_mini_slot(GameState *game, int y)
{
    board_clear(&game->board);
    for (int x = 0; x < BOARD_WIDTH; ++x) {
        if (x != 4) {
            board_set(&game->board, x, 0, CELL_J);
        }
        if (x != 4 && x != 5) {
            board_set(&game->board, x, 1, CELL_J);
        }
    }
    board_set(&game->board, 3, 2, CELL_J);
    place(game, PIECE_T, 1, 3, y);
    CHECK(game_piece_fits(&game->board, &game->piece));
    game->last_action_was_rotation = true;
}

static void test_spin_is_lost_by_moving_down(void)
{
    /* Spun in place: a mini T-spin double. */
    GameState game;
    game_init(&game, 1);
    build_mini_slot(&game, 0);
    CHECK_EQ_INT(hard_drop(&game), 400);
    CHECK_EQ_INT(game.last_clear.tspin, GAME_TSPIN_MINI);
    CHECK_EQ_INT(game.last_clear.lines, 2);

    /* Spun one row higher, so the hard drop moves it into the slot: the drop
       is the last movement, not the rotation. */
    GameState moved;
    game_init(&moved, 1);
    build_mini_slot(&moved, 1);
    CHECK_EQ_INT(hard_drop(&moved), 300 + 2); /* a plain double and one dropped row */
    CHECK_EQ_INT(moved.last_clear.tspin, GAME_TSPIN_NONE);
}

static void test_back_to_back(void)
{
    GameState game;
    game_init(&game, 1);

    /* T-spin double, a lock that clears nothing, then another T-spin double:
       the quiet lock breaks the combo but not the chain, so the second one
       gets half again. */
    CHECK_EQ_INT(do_tsd(&game), 1200);
    finish_clear(&game);
    CHECK_EQ_INT(do_quiet_lock(&game), 0);
    CHECK_EQ_INT(do_tsd(&game), 1800);
    CHECK(game.last_clear.back_to_back);
    CHECK_EQ_INT(game.last_clear.combo, 0);
    finish_clear(&game);

    /* A plain single breaks the chain. */
    CHECK_EQ_INT(do_quiet_lock(&game), 0);
    CHECK_EQ_INT(do_single(&game), 100);
    CHECK(!game.back_to_back);
    finish_clear(&game);
    CHECK_EQ_INT(do_quiet_lock(&game), 0);
    CHECK_EQ_INT(do_tsd(&game), 1200);
    CHECK(!game.last_clear.back_to_back);
    finish_clear(&game);

    /* Four lines count as difficult too: after the T-spin, 800 becomes 1200. */
    CHECK_EQ_INT(do_quiet_lock(&game), 0);
    CHECK_EQ_INT(do_quad(&game, true), 1200);
    CHECK(game.last_clear.back_to_back);
}

static void test_combo(void)
{
    GameState game;
    game_init(&game, 1);

    /* Singles in a row: 100, then 100 + 50, then 100 + 100. */
    CHECK_EQ_INT(do_single(&game), 100);
    CHECK_EQ_INT(game.last_clear.combo, 0);
    finish_clear(&game);
    CHECK_EQ_INT(do_single(&game), 150);
    CHECK_EQ_INT(game.last_clear.combo, 1);
    finish_clear(&game);
    CHECK_EQ_INT(do_single(&game), 200);
    CHECK_EQ_INT(game.last_clear.combo, 2);
    finish_clear(&game);

    /* A lock that clears nothing ends it. */
    CHECK_EQ_INT(do_quiet_lock(&game), 0);
    CHECK_EQ_INT(game.clear_streak, 0);
    CHECK_EQ_INT(do_single(&game), 100);
    CHECK_EQ_INT(game.last_clear.combo, 0);
}

static void test_perfect_clear(void)
{
    /* A single that empties the board: 100 + 800. */
    GameState game;
    game_init(&game, 1);
    board_clear(&game.board);
    for (int x = 4; x < BOARD_WIDTH; ++x) {
        board_set(&game.board, x, 0, CELL_J);
    }
    place(&game, PIECE_I, 0, 0, -2);
    CHECK_EQ_INT(hard_drop(&game), 900);
    CHECK(game.last_clear.perfect_clear);
    finish_clear(&game);
    CHECK_EQ_INT(board_get(&game.board, 5, 0), CELL_EMPTY);

    /* Four lines that empty the board: 800 + 2000. */
    GameState quad;
    game_init(&quad, 1);
    CHECK_EQ_INT(do_quad(&quad, false), 2800);
    CHECK(quad.last_clear.perfect_clear);
    finish_clear(&quad);

    /* The same after another four-line clear is back-to-back: 1200 + 3200. */
    CHECK_EQ_INT(do_quiet_lock(&quad), 0);
    CHECK_EQ_INT(do_quad(&quad, false), 4400);
    CHECK(quad.last_clear.back_to_back);
    CHECK(quad.last_clear.perfect_clear);

    /* A block left above the rows means no perfect clear. */
    GameState kept;
    game_init(&kept, 1);
    CHECK_EQ_INT(do_quad(&kept, true), 800);
    CHECK(!kept.last_clear.perfect_clear);

    /* The perfect clear points are multiplied by the level too. */
    GameState level;
    game_init_at_level(&level, 1, 2);
    CHECK_EQ_INT(do_quad(&level, false), 2800 * 2);
}

/* --- clear labels -------------------------------------------------------- */

static int label_for(const GameClearResult *clear, char lines[][FX_LABEL_LINE_SIZE])
{
    static Fx fx;
    fx_init(&fx, 1);
    GameState game;
    game_init(&game, 1);
    game.last_clear = *clear;
    fx_on_events(&fx, GAME_EVENT_LOCK, &game);
    return fx_label_lines(&fx, lines, FX_LABEL_MAX_LINES);
}

static void test_clear_labels(void)
{
    char lines[FX_LABEL_MAX_LINES][FX_LABEL_LINE_SIZE];

    /* A plain double is not worth a label. */
    const GameClearResult plain = { 2, GAME_TSPIN_NONE, false, false, 0, 300 };
    CHECK_EQ_INT(label_for(&plain, lines), 0);

    const GameClearResult tsd = { 2, GAME_TSPIN_FULL, true, false, 0, 1800 };
    CHECK_EQ_INT(label_for(&tsd, lines), 3);
    CHECK(strcmp(lines[0], "B2B") == 0);
    CHECK(strcmp(lines[1], "T-SPIN") == 0);
    CHECK(strcmp(lines[2], "DOUBLE") == 0);

    const GameClearResult mini = { 0, GAME_TSPIN_MINI, false, false, 0, 100 };
    CHECK_EQ_INT(label_for(&mini, lines), 2);
    CHECK(strcmp(lines[0], "MINI") == 0);
    CHECK(strcmp(lines[1], "T-SPIN") == 0);

    const GameClearResult combo = { 1, GAME_TSPIN_NONE, false, false, 12, 700 };
    CHECK_EQ_INT(label_for(&combo, lines), 2);
    CHECK(strcmp(lines[0], "SINGLE") == 0);
    CHECK(strcmp(lines[1], "COMBO 12") == 0);

    /* Everything at once fits the seven lines. */
    const GameClearResult all = { 2, GAME_TSPIN_MINI, true, true, 65535, 0 };
    CHECK_EQ_INT(label_for(&all, lines), FX_LABEL_MAX_LINES);
    CHECK(strcmp(lines[3], "DOUBLE") == 0);
    CHECK(strcmp(lines[4], "COMBO 65535") == 0);
    CHECK(strcmp(lines[5], "PERFECT") == 0);
    CHECK(strcmp(lines[6], "CLEAR") == 0);

    const GameClearResult quad = { 4, GAME_TSPIN_NONE, false, false, 0, 800 };
    CHECK_EQ_INT(label_for(&quad, lines), 1);
    CHECK(strcmp(lines[0], "QUAD") == 0);
}

static void test_clear_label_fades(void)
{
    static Fx fx;
    fx_init(&fx, 1);
    fx_set_intensity(&fx, 0.0f); /* the label is not an effect: it stays */
    GameState game;
    game_init(&game, 1);
    game.last_clear.tspin = GAME_TSPIN_FULL;
    fx_on_events(&fx, GAME_EVENT_LOCK, &game);

    CHECK(fx_label_alpha(&fx) == 1.0f);
    for (int i = 0; i < 60; ++i) {
        fx_update(&fx, 1.0f / 60.0f);
    }
    CHECK(fx_label_alpha(&fx) > 0.0f && fx_label_alpha(&fx) <= 1.0f);
    for (int i = 0; i < 60; ++i) {
        fx_update(&fx, 1.0f / 60.0f);
    }
    char lines[FX_LABEL_MAX_LINES][FX_LABEL_LINE_SIZE];
    CHECK(fx_label_alpha(&fx) == 0.0f);
    CHECK_EQ_INT(fx_label_lines(&fx, lines, FX_LABEL_MAX_LINES), 0);
}

/* --- replays ------------------------------------------------------------- */

static void run_spin_script(GameState *game)
{
    game_init(game, 7);
    CHECK_EQ_INT(do_tsd(game), 1200);
    finish_clear(game);
    CHECK_EQ_INT(do_quiet_lock(game), 0);
    CHECK_EQ_INT(do_tsd(game), 1800);
    finish_clear(game);
    CHECK_EQ_INT(do_quad(game, false), 1200 + 3200 + 50);
}

static void test_spins_replay_exactly(void)
{
    GameState first;
    GameState second;
    run_spin_script(&first);
    run_spin_script(&second);
    CHECK(memcmp(&first, &second, sizeof first) == 0);
    CHECK_EQ_INT(first.score, 1200 + 1800 + 4450);
}

void test_scoring_suite(void)
{
    test_tspin_rule_corners();
    test_tspin_rule_walls_and_floor_count();
    test_tspin_double_scores_1200();
    test_tspin_without_lines_and_minis();
    test_spin_is_lost_by_moving_down();
    test_back_to_back();
    test_combo();
    test_perfect_clear();
    test_clear_labels();
    test_clear_label_fades();
    test_spins_replay_exactly();
}
