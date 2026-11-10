#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Visible playfield size. Row 0 is the bottom row, column 0 the left column. */
#define BOARD_WIDTH 10
#define BOARD_HEIGHT 20

typedef enum CellType {
    CELL_EMPTY = 0,
    CELL_I,
    CELL_O,
    CELL_T,
    CELL_S,
    CELL_Z,
    CELL_J,
    CELL_L,
    CELL_TYPE_COUNT
} CellType;

typedef struct Board {
    uint8_t cells[BOARD_HEIGHT][BOARD_WIDTH];
} Board;

/* Fills the whole playfield with CELL_EMPTY. */
void board_clear(Board *board);

/* Returns the cell at (x, y). Out-of-bounds reads return CELL_EMPTY. */
CellType board_get(const Board *board, int x, int y);

/* Stores type at (x, y). Out-of-bounds writes are ignored and return false. */
bool board_set(Board *board, int x, int y, CellType type);

/* True when (x, y) addresses a cell of the playfield. */
bool board_in_bounds(int x, int y);
