#pragma once

#include <stdbool.h>
#include <stdint.h>

/*
 * Playfield storage. Row 0 is the bottom row, column 0 the left column.
 *
 * The stored field is taller than the visible one: BOARD_BUFFER_ROWS hidden
 * rows sit above the 20 visible rows so pieces can spawn and rotate out of
 * sight. Only rows [0, BOARD_VISIBLE_HEIGHT) are drawn.
 */
#define BOARD_WIDTH 10
#define BOARD_VISIBLE_HEIGHT 20
#define BOARD_BUFFER_ROWS 4
#define BOARD_HEIGHT (BOARD_VISIBLE_HEIGHT + BOARD_BUFFER_ROWS)

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

/* True when (x, y) addresses a cell of the stored playfield. */
bool board_in_bounds(int x, int y);

/* True when every cell of row y is filled. Rows outside the field are not full. */
bool board_row_full(const Board *board, int y);

/*
 * Removes every full row, drops the rows above it down and fills the top of the
 * field with empty rows. Returns the number of rows removed.
 */
int board_clear_full_rows(Board *board);
