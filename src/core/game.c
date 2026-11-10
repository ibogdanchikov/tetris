#include "core/game.h"

#include <string.h>

#include "core/rng.h"

void game_piece_cells(const ActivePiece *piece, int out_x[4], int out_y[4])
{
    PieceCell cells[4];
    piece_cells(piece->type, piece->rotation, cells);
    for (int i = 0; i < 4; ++i) {
        out_x[i] = piece->x + cells[i].x;
        out_y[i] = piece->y + cells[i].y;
    }
}

bool game_piece_fits(const Board *board, const ActivePiece *piece)
{
    int xs[4];
    int ys[4];
    game_piece_cells(piece, xs, ys);

    for (int i = 0; i < 4; ++i) {
        if (!board_in_bounds(xs[i], ys[i])) {
            return false;
        }
        if (board_get(board, xs[i], ys[i]) != CELL_EMPTY) {
            return false;
        }
    }
    return true;
}

/* True when the piece could move down one row. */
static bool game_can_fall(const GameState *game)
{
    ActivePiece below = game->piece;
    below.y = (int8_t)(below.y - 1);
    return game_piece_fits(&game->board, &below);
}

/* Moves the piece by (dx, dy) when the target cells are free. */
static bool game_try_move(GameState *game, int dx, int dy)
{
    ActivePiece moved = game->piece;
    moved.x = (int8_t)(moved.x + dx);
    moved.y = (int8_t)(moved.y + dy);

    if (!game_piece_fits(&game->board, &moved)) {
        return false;
    }
    game->piece = moved;
    return true;
}

/*
 * A successful move or rotation while resting on the stack gives the player
 * more time to slide the piece. The number of resets per piece is capped, so a
 * piece cannot be kept alive forever.
 */
static void game_reset_lock_delay(GameState *game)
{
    if (!game->grounded || game->lock_resets >= GAME_MAX_LOCK_RESETS) {
        return;
    }
    game->lock_counter = 0;
    ++game->lock_resets;
}

/* Rotates in place; a blocked rotation leaves the piece untouched (no kicks). */
static bool game_try_rotate(GameState *game, int direction)
{
    ActivePiece rotated = game->piece;
    rotated.rotation = (int8_t)((rotated.rotation + direction + 4) % 4);

    if (!game_piece_fits(&game->board, &rotated)) {
        return false;
    }
    game->piece = rotated;
    game_reset_lock_delay(game);
    return true;
}

/*
 * Horizontal movement with DAS/ARR. shift_counter counts down to the next
 * move: a fresh press moves at once and loads DAS, and every repeat loads ARR.
 */
static void game_step_shift(GameState *game, const GameInput *input)
{
    const bool left = (input->held & ACTION_LEFT) != 0;
    const bool right = (input->held & ACTION_RIGHT) != 0;
    int8_t direction = 0;

    if (left && right) {
        const bool press_left = (input->pressed & ACTION_LEFT) != 0;
        const bool press_right = (input->pressed & ACTION_RIGHT) != 0;
        if (press_left != press_right) {
            direction = press_left ? -1 : 1;
        } else if (!press_left) {
            /* Both already held: the one pressed last keeps priority. */
            direction = game->shift_dir;
        }
    } else if (left) {
        direction = -1;
    } else if (right) {
        direction = 1;
    }

    if (direction == 0) {
        game->shift_dir = 0;
        game->shift_counter = 0;
        return;
    }

    if (direction != game->shift_dir) {
        game->shift_dir = direction;
        game->shift_counter = GAME_DAS_TICKS;
        if (game_try_move(game, direction, 0)) {
            game_reset_lock_delay(game);
        }
        return;
    }

    if (game->shift_counter > 0) {
        --game->shift_counter;
    }
    if (game->shift_counter == 0) {
        if (game_try_move(game, direction, 0)) {
            game_reset_lock_delay(game);
        }
        game->shift_counter = GAME_ARR_TICKS;
    }
}

/* Writes the piece into the board, clears lines and spawns the next piece. */
static void game_lock_piece(GameState *game)
{
    int xs[4];
    int ys[4];
    game_piece_cells(&game->piece, xs, ys);

    const CellType type = piece_cell_type(game->piece.type);
    for (int i = 0; i < 4; ++i) {
        board_set(&game->board, xs[i], ys[i], type);
    }

    const int cleared = board_clear_full_rows(&game->board);
    game->lines_cleared += (uint32_t)cleared;
    game->last_clear_count = (uint8_t)cleared;

    game_spawn_next(game);
}

void game_spawn_next(GameState *game)
{
    game->piece.type = game->next_piece;
    game->piece.rotation = 0;
    game->next_piece = bag_next(&game->bag, &game->rng_state);

    /* Centre the piece's box on the field. The spawn cells start at dx 0, so
       this puts 3-wide pieces in columns 3-5, I in 3-6 and O in 4-5. */
    PieceCell cells[4];
    piece_cells(game->piece.type, 0, cells);
    int min_x = cells[0].x;
    int max_x = cells[0].x;
    int min_y = cells[0].y;
    for (int i = 1; i < 4; ++i) {
        if (cells[i].x < min_x) {
            min_x = cells[i].x;
        }
        if (cells[i].x > max_x) {
            max_x = cells[i].x;
        }
        if (cells[i].y < min_y) {
            min_y = cells[i].y;
        }
    }
    const int width = max_x - min_x + 1;
    game->piece.x = (int8_t)((BOARD_WIDTH - width) / 2 - min_x);
    /* Lowest cells in the first hidden row. */
    game->piece.y = (int8_t)(BOARD_VISIBLE_HEIGHT - min_y);

    game->gravity_counter = 0;
    game->lock_counter = 0;
    game->lock_resets = 0;
    game->grounded = false;
    game->shift_dir = 0;
    game->shift_counter = 0;
    ++game->piece_serial;

    if (!game_piece_fits(&game->board, &game->piece)) {
        game->topped_out = true;
        return;
    }

    /* The guideline drops the piece one row right after spawn, so its lowest
       cells show up in the top visible row instead of staying hidden. */
    game_try_move(game, 0, -1);

    game->grounded = !game_can_fall(game);
}

void game_init(GameState *game, uint64_t seed)
{
    /* Zeroing everything first makes padding deterministic too, so two states
       can be compared with memcmp. */
    memset(game, 0, sizeof *game);
    rng_seed(&game->rng_state, seed);
    bag_init(&game->bag, &game->rng_state);
    game->next_piece = bag_next(&game->bag, &game->rng_state);
    game_spawn_next(game);
}

int game_ghost_y(const GameState *game)
{
    ActivePiece probe = game->piece;

    /*
     * Drop one row at a time while the piece still fits. The box origin may go
     * negative: the lowest cells of I and of the 3x3 pieces sit above dy 0, so
     * a piece resting on the floor has a box bottom below row 0. The loop ends
     * on its own once the lowest cell would leave the board.
     */
    for (;;) {
        ActivePiece below = probe;
        below.y = (int8_t)(below.y - 1);
        if (!game_piece_fits(&game->board, &below)) {
            break;
        }
        probe = below;
    }
    return probe.y;
}

void game_step(GameState *game, const GameInput *input)
{
    if (game->topped_out) {
        ++game->tick;
        return;
    }

    /* Soft drop has its own cadence: restart it on the press edge instead of
       inheriting a partly elapsed gravity counter. */
    if ((input->pressed & ACTION_SOFT_DROP) != 0) {
        game->gravity_counter = 0;
    }

    /* (1) rotation; CW and CCW in the same tick cancel each other. */
    const bool rotate_cw = (input->pressed & ACTION_ROTATE_CW) != 0;
    const bool rotate_ccw = (input->pressed & ACTION_ROTATE_CCW) != 0;
    if (rotate_cw != rotate_ccw) {
        game_try_rotate(game, rotate_cw ? 1 : -1);
    }

    /* (2) horizontal movement. */
    game_step_shift(game, input);

    /* (3) hard drop: fall to the ghost row and lock in this same tick. */
    if ((input->pressed & ACTION_HARD_DROP) != 0) {
        game->piece.y = (int8_t)game_ghost_y(game);
        game_lock_piece(game);
        ++game->tick;
        return;
    }

    /* (4) gravity, or the faster soft drop while it is held. */
    const uint16_t interval = (input->held & ACTION_SOFT_DROP) != 0
                                  ? GAME_SOFT_DROP_TICKS
                                  : GAME_GRAVITY_TICKS;
    ++game->gravity_counter;
    if (game->gravity_counter >= interval) {
        game->gravity_counter = 0;
        game_try_move(game, 0, -1);
    }

    /* (5) lock delay: counts up while resting, and a piece that leaves the
       ground (for example by sliding off a ledge) resumes falling. */
    game->grounded = !game_can_fall(game);
    if (!game->grounded) {
        game->lock_counter = 0;
    } else {
        ++game->lock_counter;
        if (game->lock_counter >= GAME_LOCK_DELAY_TICKS) {
            game_lock_piece(game);
        }
    }

    /* (6) one tick has elapsed. */
    ++game->tick;
}
