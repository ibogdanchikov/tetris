#pragma once

#include <stdint.h>

#include "core/board.h"

/* The seven tetrominoes, in the order the bag shuffles them. */
typedef enum PieceType {
    PIECE_I = 0,
    PIECE_O,
    PIECE_T,
    PIECE_S,
    PIECE_Z,
    PIECE_J,
    PIECE_L,
    PIECE_COUNT
} PieceType;

/*
 * One cell of a tetromino as an offset inside its bounding box.
 *
 * The box origin is its bottom-left corner and dy points up, which matches
 * board rows (row 0 is the bottom). The box is 4x4 for I, 3x3 for J L S T Z and
 * 2x2 for O. SRS diagrams are usually drawn y-down, so these tables are the
 * vertical mirror of the familiar pictures: the first line of a drawing is
 * dy = box_size - 1.
 *
 * Keeping the cells in the SRS box (rather than normalising each rotation to
 * the box bottom-left) is what makes the SRS wall-kick tables usable later:
 * the kicks are defined relative to this same box. O is a 2x2 box because its
 * cells do not move when it rotates, which is exactly what SRS asks of it.
 */
typedef struct PieceCell {
    int8_t x;
    int8_t y;
} PieceCell;

/* Board cell type used when this piece is written into the playfield. */
CellType piece_cell_type(PieceType type);

/*
 * Writes the four cells of the piece in the given rotation into out:
 * 0 = spawn, 1 = clockwise, 2, 3 = counter-clockwise. Rotation is taken
 * modulo 4, so negative values are fine too.
 */
void piece_cells(PieceType type, int rotation, PieceCell out[4]);
