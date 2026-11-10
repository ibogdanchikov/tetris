#include "core/game.h"

#include <stddef.h>

#include "core/rng.h"

/*
 * Milestone 1 has no falling pieces yet, so the board is filled with a fixed
 * demo pattern instead: two full rows of mixed colours, a gap-toothed row above
 * them and a few loose cells higher up. Every colour appears at least once, so
 * the renderer and the palette can be checked visually.
 */
static void game_fill_demo_pattern(Board *board)
{
    static const CellType bottom_row[BOARD_WIDTH] = {
        CELL_I, CELL_O, CELL_T, CELL_S, CELL_Z, CELL_J, CELL_L, CELL_I, CELL_O, CELL_T,
    };
    static const CellType second_row[BOARD_WIDTH] = {
        CELL_L, CELL_J, CELL_Z, CELL_S, CELL_T, CELL_O, CELL_I, CELL_L, CELL_J, CELL_Z,
    };
    static const struct {
        int x;
        int y;
        CellType type;
    } loose_cells[] = {
        { 0, 2, CELL_S }, { 1, 2, CELL_S }, { 2, 2, CELL_Z }, { 3, 2, CELL_Z },
        { 6, 2, CELL_I }, { 7, 2, CELL_I }, { 8, 2, CELL_O }, { 9, 2, CELL_O },
        { 4, 5, CELL_T },
        { 7, 6, CELL_L },
    };

    board_clear(board);
    for (int x = 0; x < BOARD_WIDTH; ++x) {
        board_set(board, x, 0, bottom_row[x]);
        board_set(board, x, 1, second_row[x]);
    }
    for (size_t i = 0; i < sizeof loose_cells / sizeof loose_cells[0]; ++i) {
        board_set(board, loose_cells[i].x, loose_cells[i].y, loose_cells[i].type);
    }
}

void game_init(GameState *game, uint64_t seed)
{
    game_fill_demo_pattern(&game->board);
    game->tick = 0;
    rng_seed(&game->rng_state, seed);
}

void game_step(GameState *game)
{
    /* Nothing moves yet: the tick counter is the whole simulation for now. */
    ++game->tick;
}
