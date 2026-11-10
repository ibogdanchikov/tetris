#pragma once

#include <stdint.h>

/*
 * One tick worth of player intent, produced by the platform layer.
 *
 * held    - actions that are down right now.
 * pressed - actions that went down since the previous tick. The caller
 *           delivers each edge exactly once; the game never sees repeats.
 *
 * Auto-repeat (DAS/ARR) lives in the game, not in the platform.
 */
typedef enum GameAction {
    ACTION_LEFT = 1u << 0,
    ACTION_RIGHT = 1u << 1,
    ACTION_SOFT_DROP = 1u << 2,
    ACTION_HARD_DROP = 1u << 3,
    ACTION_ROTATE_CW = 1u << 4,
    ACTION_ROTATE_CCW = 1u << 5,
} GameAction;

typedef struct GameInput {
    uint32_t held;
    uint32_t pressed;
} GameInput;
