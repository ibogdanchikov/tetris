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

bool board_row_full(const Board *board, int y)
{
    if (y < 0 || y >= BOARD_HEIGHT) {
        return false;
    }
    for (int x = 0; x < BOARD_WIDTH; ++x) {
        if (board->cells[y][x] == CELL_EMPTY) {
            return false;
        }
    }
    return true;
}

int board_clear_full_rows(Board *board)
{
    int write_y = 0;
    int cleared = 0;

    /*
     * Compaction: write_y never overtakes read_y, so rows that still have to be
     * examined are never overwritten. Rows above the last kept row are wiped.
     */
    for (int read_y = 0; read_y < BOARD_HEIGHT; ++read_y) {
        if (board_row_full(board, read_y)) {
            ++cleared;
            continue;
        }
        if (write_y != read_y) {
            memcpy(board->cells[write_y], board->cells[read_y], BOARD_WIDTH);
        }
        ++write_y;
    }

    for (int y = write_y; y < BOARD_HEIGHT; ++y) {
        memset(board->cells[y], CELL_EMPTY, BOARD_WIDTH);
    }
    return cleared;
}
