#pragma once

#include <stdint.h>

#include "core/input.h"

/*
 * Collects keyboard state between simulation ticks. Deliberately free of SDL so
 * the tests can link it: the app layer feeds it actions, the game reads ticks.
 *
 * Events arrive at frame rate while the game advances in fixed ticks, so a
 * press and its release can both land between two ticks. The buffer therefore
 * remembers presses until the next take and reports them as held as well: a tap
 * shorter than one tick must still move the piece once.
 */
typedef struct InputBuffer {
    uint32_t held;            /* actions that are down right now */
    uint32_t pending_pressed; /* presses not yet delivered to a tick */
} InputBuffer;

/* Marks an action as down and queues the press for the next take. */
void input_buffer_key_down(InputBuffer *buffer, uint32_t action);

/*
 * Marks an action as up. A press that has not been taken yet stays queued, so
 * the tap still reaches a tick even though the key is already released.
 */
void input_buffer_key_up(InputBuffer *buffer, uint32_t action);

/*
 * Returns one tick worth of input and clears the queued presses. Frames that
 * run zero ticks do not call this, so their presses stay pending until the
 * next tick instead of being dropped.
 */
GameInput input_buffer_take(InputBuffer *buffer);

/* Drops all state, for example when the window loses focus. */
void input_buffer_reset(InputBuffer *buffer);
