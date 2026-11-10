#include "core/board.h"

#include <string.h>

void board_clear(Board *board)
{
    memset(board->cells, CELL_EMPTY, sizeof board->cells);
}

bool board_in_bounds(int x, int y)
{
    return x >= 0 && x < BOARD_WIDTH && y >= 0 && y < BOARD_HEIGHT;
}

CellType board_get(const Board *board, int x, int y)
{
    if (!board_in_bounds(x, y)) {
        return CELL_EMPTY;
    }
    return (CellType)board->cells[y][x];
}

bool board_set(Board *board, int x, int y, CellType type)
{
    if (!board_in_bounds(x, y)) {
        return false;
    }
    board->cells[y][x] = (uint8_t)type;
    return true;
}
