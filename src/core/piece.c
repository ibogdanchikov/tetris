#include "core/piece.h"

/*
 * Shapes in the SRS bounding boxes, four cells per rotation.
 *
 * Each comment shows the state as it is usually drawn (top line first). With
 * dy pointing up, that top line is dy = box_size - 1, so the spawn shapes sit
 * in the upper part of their box exactly like the guideline expects.
 */
static const PieceCell PIECE_CELLS[PIECE_COUNT][4][4] = {
    /* I (4x4 box). Spawn is flat; then vertical in column 2; flat one row
       lower; vertical in column 1. */
    [PIECE_I] = {
        { { 0, 2 }, { 1, 2 }, { 2, 2 }, { 3, 2 } }, /* .... / IIII / .... / .... */
        { { 2, 0 }, { 2, 1 }, { 2, 2 }, { 2, 3 } }, /* ..I. / ..I. / ..I. / ..I. */
        { { 0, 1 }, { 1, 1 }, { 2, 1 }, { 3, 1 } }, /* .... / .... / IIII / .... */
        { { 1, 0 }, { 1, 1 }, { 1, 2 }, { 1, 3 } }, /* .I.. / .I.. / .I.. / .I.. */
    },

    /* O (2x2 box). Rotating never moves it, so all four states are identical. */
    [PIECE_O] = {
        { { 0, 0 }, { 1, 0 }, { 0, 1 }, { 1, 1 } }, /* OO / OO */
        { { 0, 0 }, { 1, 0 }, { 0, 1 }, { 1, 1 } },
        { { 0, 0 }, { 1, 0 }, { 0, 1 }, { 1, 1 } },
        { { 0, 0 }, { 1, 0 }, { 0, 1 }, { 1, 1 } },
    },

    /* T (3x3 box). Spawn points up; rotation 1 points right. */
    [PIECE_T] = {
        { { 1, 2 }, { 0, 1 }, { 1, 1 }, { 2, 1 } }, /* .T. / TTT / ... */
        { { 1, 2 }, { 1, 1 }, { 2, 1 }, { 1, 0 } }, /* .T. / .TT / .T. */
        { { 0, 1 }, { 1, 1 }, { 2, 1 }, { 1, 0 } }, /* ... / TTT / .T. */
        { { 1, 2 }, { 0, 1 }, { 1, 1 }, { 1, 0 } }, /* .T. / TT. / .T. */
    },

    /* S (3x3 box). Spawn has its top row shifted right. */
    [PIECE_S] = {
        { { 1, 2 }, { 2, 2 }, { 0, 1 }, { 1, 1 } }, /* .SS / SS. / ... */
        { { 1, 2 }, { 1, 1 }, { 2, 1 }, { 2, 0 } }, /* .S. / .SS / ..S */
        { { 1, 1 }, { 2, 1 }, { 0, 0 }, { 1, 0 } }, /* ... / .SS / SS. */
        { { 0, 2 }, { 0, 1 }, { 1, 1 }, { 1, 0 } }, /* S.. / SS. / .S. */
    },

    /* Z (3x3 box). Spawn has its top row shifted left. */
    [PIECE_Z] = {
        { { 0, 2 }, { 1, 2 }, { 1, 1 }, { 2, 1 } }, /* ZZ. / .ZZ / ... */
        { { 2, 2 }, { 1, 1 }, { 2, 1 }, { 1, 0 } }, /* ..Z / .ZZ / .Z. */
        { { 0, 1 }, { 1, 1 }, { 1, 0 }, { 2, 0 } }, /* ... / ZZ. / .ZZ */
        { { 1, 2 }, { 0, 1 }, { 1, 1 }, { 0, 0 } }, /* .Z. / ZZ. / Z.. */
    },

    /* J (3x3 box). Spawn has its corner at the top left. */
    [PIECE_J] = {
        { { 0, 2 }, { 0, 1 }, { 1, 1 }, { 2, 1 } }, /* J.. / JJJ / ... */
        { { 1, 2 }, { 2, 2 }, { 1, 1 }, { 1, 0 } }, /* .JJ / .J. / .J. */
        { { 0, 1 }, { 1, 1 }, { 2, 1 }, { 2, 0 } }, /* ... / JJJ / ..J */
        { { 1, 2 }, { 1, 1 }, { 0, 0 }, { 1, 0 } }, /* .J. / .J. / JJ. */
    },

    /* L (3x3 box). Spawn has its corner at the top right. */
    [PIECE_L] = {
        { { 2, 2 }, { 0, 1 }, { 1, 1 }, { 2, 1 } }, /* ..L / LLL / ... */
        { { 1, 2 }, { 1, 1 }, { 1, 0 }, { 2, 0 } }, /* .L. / .L. / .LL */
        { { 0, 1 }, { 1, 1 }, { 2, 1 }, { 0, 0 } }, /* ... / LLL / L.. */
        { { 0, 2 }, { 1, 2 }, { 1, 1 }, { 1, 0 } }, /* LL. / .L. / .L. */
    },
};

CellType piece_cell_type(PieceType type)
{
    switch (type) {
    case PIECE_I:
        return CELL_I;
    case PIECE_O:
        return CELL_O;
    case PIECE_T:
        return CELL_T;
    case PIECE_S:
        return CELL_S;
    case PIECE_Z:
        return CELL_Z;
    case PIECE_J:
        return CELL_J;
    case PIECE_L:
        return CELL_L;
    case PIECE_COUNT:
        break;
    }
    return CELL_EMPTY;
}

void piece_cells(PieceType type, int rotation, PieceCell out[4])
{
    const int index = (int)type;
    if (index < 0 || index >= PIECE_COUNT) {
        for (int i = 0; i < 4; ++i) {
            out[i] = (PieceCell){ 0, 0 };
        }
        return;
    }

    int state = rotation % 4;
    if (state < 0) {
        state += 4;
    }
    for (int i = 0; i < 4; ++i) {
        out[i] = PIECE_CELLS[index][state][i];
    }
}
