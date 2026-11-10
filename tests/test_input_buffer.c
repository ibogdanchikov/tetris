/*
 * Input buffer tests plus a small integration check that drives game_step from
 * buffered key events. Nothing here touches SDL: the buffer is plain data and
 * the tick loop is exactly what main.c runs.
 */

#include "app/input_buffer.h"
#include "core/game.h"
#include "test_harness.h"

static void test_tap_within_one_tick(void)
{
    InputBuffer buffer = { 0, 0 };
    input_buffer_key_down(&buffer, ACTION_LEFT);
    input_buffer_key_up(&buffer, ACTION_LEFT);

    /* Down and up both landed between ticks: the tick still sees the press. */
    const GameInput tick = input_buffer_take(&buffer);
    CHECK((tick.held & ACTION_LEFT) != 0);
    CHECK((tick.pressed & ACTION_LEFT) != 0);

    /* And only that one tick sees it. */
    const GameInput next = input_buffer_take(&buffer);
    CHECK_EQ_INT(next.held, 0);
    CHECK_EQ_INT(next.pressed, 0);
}

static void test_held_across_ticks(void)
{
    InputBuffer buffer = { 0, 0 };
    input_buffer_key_down(&buffer, ACTION_SOFT_DROP);

    const GameInput first = input_buffer_take(&buffer);
    CHECK((first.held & ACTION_SOFT_DROP) != 0);
    CHECK((first.pressed & ACTION_SOFT_DROP) != 0);

    for (int tick = 0; tick < 3; ++tick) {
        const GameInput repeated = input_buffer_take(&buffer);
        CHECK((repeated.held & ACTION_SOFT_DROP) != 0);
        CHECK_EQ_INT(repeated.pressed, 0);
    }

    input_buffer_key_up(&buffer, ACTION_SOFT_DROP);
    const GameInput released = input_buffer_take(&buffer);
    CHECK_EQ_INT(released.held, 0);
    CHECK_EQ_INT(released.pressed, 0);
}

static void test_two_keys_tracked_separately(void)
{
    InputBuffer buffer = { 0, 0 };
    input_buffer_key_down(&buffer, ACTION_LEFT);
    input_buffer_key_down(&buffer, ACTION_ROTATE_CW);

    const uint32_t both = ACTION_LEFT | ACTION_ROTATE_CW;
    const GameInput pressed = input_buffer_take(&buffer);
    CHECK_EQ_INT(pressed.held, both);
    CHECK_EQ_INT(pressed.pressed, both);

    /* Releasing one key leaves the other down, with no new edge. */
    input_buffer_key_up(&buffer, ACTION_LEFT);
    const GameInput rotate_only = input_buffer_take(&buffer);
    CHECK_EQ_INT(rotate_only.held, ACTION_ROTATE_CW);
    CHECK_EQ_INT(rotate_only.pressed, 0);
}

static void test_key_up_without_key_down(void)
{
    InputBuffer buffer = { 0, 0 };
    input_buffer_key_up(&buffer, ACTION_LEFT);
    const GameInput empty = input_buffer_take(&buffer);
    CHECK_EQ_INT(empty.held, 0);
    CHECK_EQ_INT(empty.pressed, 0);

    /* A stray release of another key must not cancel a real press. */
    input_buffer_key_down(&buffer, ACTION_LEFT);
    input_buffer_key_up(&buffer, ACTION_RIGHT);
    const GameInput left = input_buffer_take(&buffer);
    CHECK_EQ_INT(left.held, ACTION_LEFT);
    CHECK_EQ_INT(left.pressed, ACTION_LEFT);
}

static void test_reset_drops_state(void)
{
    InputBuffer buffer = { 0, 0 };
    input_buffer_key_down(&buffer, ACTION_LEFT);
    input_buffer_key_down(&buffer, ACTION_HARD_DROP);
    const GameInput pending = input_buffer_take(&buffer);
    CHECK(pending.held != 0);

    /* Lost focus: nothing stays held and nothing stays queued. */
    input_buffer_key_down(&buffer, ACTION_RIGHT);
    input_buffer_reset(&buffer);
    const GameInput cleared = input_buffer_take(&buffer);
    CHECK_EQ_INT(cleared.held, 0);
    CHECK_EQ_INT(cleared.pressed, 0);
}

static void test_press_survives_frames_without_ticks(void)
{
    InputBuffer buffer = { 0, 0 };
    input_buffer_key_down(&buffer, ACTION_HARD_DROP);
    input_buffer_key_up(&buffer, ACTION_HARD_DROP);

    /* Two frames ran zero ticks, so nothing was taken and the press waits. */
    CHECK_EQ_INT(buffer.pending_pressed, ACTION_HARD_DROP);

    const GameInput tick = input_buffer_take(&buffer);
    CHECK_EQ_INT(tick.held, ACTION_HARD_DROP);
    CHECK_EQ_INT(tick.pressed, ACTION_HARD_DROP);
}

/* A whole tap between ticks must move the piece exactly one column. */
static void test_tap_left_moves_one_column(void)
{
    GameState game;
    game_init(&game, 42);
    const int start_x = game.piece.x;

    InputBuffer buffer = { 0, 0 };
    input_buffer_key_down(&buffer, ACTION_LEFT);
    input_buffer_key_up(&buffer, ACTION_LEFT);

    for (int tick = 0; tick < 3; ++tick) {
        const GameInput input = input_buffer_take(&buffer);
        game_step(&game, &input);
    }

    CHECK_EQ_INT(game.piece.x, start_x - 1);
    CHECK_EQ_INT(game.piece.rotation, 0);
    CHECK(!game.topped_out);
}

/* Holding the key after the tap must not turn it into repeated presses. */
static void test_second_tick_does_not_press_again(void)
{
    GameState game;
    game_init(&game, 42);
    InputBuffer buffer = { 0, 0 };

    input_buffer_key_down(&buffer, ACTION_RIGHT);
    const GameInput press = input_buffer_take(&buffer);
    game_step(&game, &press);
    const int after_press = game.piece.x;

    /* Same key still down, no release: held, but not pressed. */
    const GameInput hold = input_buffer_take(&buffer);
    CHECK_EQ_INT(hold.held, ACTION_RIGHT);
    CHECK_EQ_INT(hold.pressed, 0);

    /* One tick of DAS later nothing has repeated yet. */
    game_step(&game, &hold);
    CHECK_EQ_INT(game.piece.x, after_press);
}

void test_input_suite(void)
{
    test_tap_within_one_tick();
    test_held_across_ticks();
    test_two_keys_tracked_separately();
    test_key_up_without_key_down();
    test_reset_drops_state();
    test_press_survives_frames_without_ticks();
    test_tap_left_moves_one_column();
    test_second_tick_does_not_press_again();
}
