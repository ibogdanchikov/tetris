/* Piece shape and 7-bag tests. */

#include <stdbool.h>

#include "core/bag.h"
#include "core/piece.h"
#include "core/rng.h"
#include "test_harness.h"

/* Side length of the SRS bounding box used by the shape tables. */
static int box_size(PieceType type)
{
    switch (type) {
    case PIECE_I:
        return 4;
    case PIECE_O:
        return 2;
    case PIECE_T:
    case PIECE_S:
    case PIECE_Z:
    case PIECE_J:
    case PIECE_L:
        return 3;
    case PIECE_COUNT:
        break;
    }
    return 0;
}

static bool same_cell_set(const PieceCell a[4], const PieceCell b[4])
{
    for (int i = 0; i < 4; ++i) {
        bool found = false;
        for (int j = 0; j < 4; ++j) {
            if (a[i].x == b[j].x && a[i].y == b[j].y) {
                found = true;
                break;
            }
        }
        if (!found) {
            return false;
        }
    }
    return true;
}

static void test_piece_shapes_are_sane(void)
{
    for (int type = 0; type < PIECE_COUNT; ++type) {
        const int box = box_size((PieceType)type);
        CHECK(box >= 2);

        for (int rotation = 0; rotation < 4; ++rotation) {
            PieceCell cells[4];
            piece_cells((PieceType)type, rotation, cells);

            /* Exactly four distinct cells, all inside the bounding box. */
            int distinct = 0;
            for (int i = 0; i < 4; ++i) {
                CHECK(cells[i].x >= 0 && cells[i].x < box);
                CHECK(cells[i].y >= 0 && cells[i].y < box);

                bool duplicate = false;
                for (int j = 0; j < i; ++j) {
                    if (cells[i].x == cells[j].x && cells[i].y == cells[j].y) {
                        duplicate = true;
                    }
                }
                if (!duplicate) {
                    ++distinct;
                }
            }
            CHECK_EQ_INT(distinct, 4);
        }
    }
}

/* Rotating a cell clockwise in a y-up box of side N is (x, y) -> (y, N-1-x). */
static void test_piece_rotations_are_consistent(void)
{
    for (int type = 0; type < PIECE_COUNT; ++type) {
        const int box = box_size((PieceType)type);
        for (int rotation = 0; rotation < 4; ++rotation) {
            PieceCell current[4];
            PieceCell next[4];
            PieceCell expected[4];
            piece_cells((PieceType)type, rotation, current);
            piece_cells((PieceType)type, rotation + 1, next);

            for (int i = 0; i < 4; ++i) {
                expected[i].x = current[i].y;
                expected[i].y = (int8_t)(box - 1 - current[i].x);
            }
            CHECK(same_cell_set(next, expected));

            /* Four rotations come back to where they started. */
            PieceCell full_turn[4];
            piece_cells((PieceType)type, rotation + 4, full_turn);
            CHECK(same_cell_set(full_turn, current));
        }
    }
}

static void check_spawn_cells(PieceType type, const PieceCell expected[4])
{
    PieceCell cells[4];
    piece_cells(type, 0, cells);
    CHECK(same_cell_set(cells, expected));
}

static void test_piece_spawn_shapes(void)
{
    /* I flat, in the upper half of its 4x4 box. */
    const PieceCell i_cells[4] = { { 0, 2 }, { 1, 2 }, { 2, 2 }, { 3, 2 } };
    check_spawn_cells(PIECE_I, i_cells);

    /* O as a 2x2 block. */
    const PieceCell o_cells[4] = { { 0, 0 }, { 1, 0 }, { 0, 1 }, { 1, 1 } };
    check_spawn_cells(PIECE_O, o_cells);

    /* T pointing up: flat side down, nub on top. */
    const PieceCell t_cells[4] = { { 1, 2 }, { 0, 1 }, { 1, 1 }, { 2, 1 } };
    check_spawn_cells(PIECE_T, t_cells);

    /* S: top row shifted right. */
    const PieceCell s_cells[4] = { { 1, 2 }, { 2, 2 }, { 0, 1 }, { 1, 1 } };
    check_spawn_cells(PIECE_S, s_cells);

    /* Z: top row shifted left. */
    const PieceCell z_cells[4] = { { 0, 2 }, { 1, 2 }, { 1, 1 }, { 2, 1 } };
    check_spawn_cells(PIECE_Z, z_cells);

    /* J: corner at the top left, 3-wide side at the bottom. */
    const PieceCell j_cells[4] = { { 0, 2 }, { 0, 1 }, { 1, 1 }, { 2, 1 } };
    check_spawn_cells(PIECE_J, j_cells);

    /* L: corner at the top right, 3-wide side at the bottom. */
    const PieceCell l_cells[4] = { { 2, 2 }, { 0, 1 }, { 1, 1 }, { 2, 1 } };
    check_spawn_cells(PIECE_L, l_cells);

    /* T rotated clockwise points right. */
    const PieceCell t_right[4] = { { 1, 2 }, { 1, 1 }, { 2, 1 }, { 1, 0 } };
    PieceCell cells[4];
    piece_cells(PIECE_T, 1, cells);
    CHECK(same_cell_set(cells, t_right));
}

static void test_piece_cell_types(void)
{
    CHECK_EQ_INT(piece_cell_type(PIECE_I), CELL_I);
    CHECK_EQ_INT(piece_cell_type(PIECE_O), CELL_O);
    CHECK_EQ_INT(piece_cell_type(PIECE_T), CELL_T);
    CHECK_EQ_INT(piece_cell_type(PIECE_S), CELL_S);
    CHECK_EQ_INT(piece_cell_type(PIECE_Z), CELL_Z);
    CHECK_EQ_INT(piece_cell_type(PIECE_J), CELL_J);
    CHECK_EQ_INT(piece_cell_type(PIECE_L), CELL_L);
    CHECK_EQ_INT(piece_cell_type(PIECE_COUNT), CELL_EMPTY);

    /* Every colour exists exactly once in the piece enum. */
    bool seen[CELL_TYPE_COUNT] = { false };
    for (int type = 0; type < PIECE_COUNT; ++type) {
        const CellType cell = piece_cell_type((PieceType)type);
        CHECK(cell >= CELL_I && cell < CELL_TYPE_COUNT);
        CHECK(!seen[cell]);
        seen[cell] = true;
    }
}

static void test_bag_is_seven_permutations(void)
{
    uint64_t rng = 0;
    rng_seed(&rng, 1);
    Bag bag;
    bag_init(&bag, &rng);

    for (int group = 0; group < 10; ++group) {
        int counts[PIECE_COUNT] = { 0 };
        for (int i = 0; i < PIECE_COUNT; ++i) {
            const PieceType type = bag_next(&bag, &rng);
            CHECK((int)type >= 0 && (int)type < PIECE_COUNT);
            ++counts[type];
        }
        for (int type = 0; type < PIECE_COUNT; ++type) {
            CHECK_EQ_INT(counts[type], 1);
        }
    }
}

static void test_bag_is_deterministic(void)
{
    uint64_t first_rng = 0;
    uint64_t second_rng = 0;
    rng_seed(&first_rng, 77);
    rng_seed(&second_rng, 77);
    Bag first_bag;
    Bag second_bag;
    bag_init(&first_bag, &first_rng);
    bag_init(&second_bag, &second_rng);

    int mismatches = 0;
    for (int i = 0; i < 70; ++i) {
        if (bag_next(&first_bag, &first_rng) != bag_next(&second_bag, &second_rng)) {
            ++mismatches;
        }
    }
    CHECK_EQ_INT(mismatches, 0);

    /* A different seed must produce a different sequence somewhere early. */
    uint64_t other_rng = 0;
    rng_seed(&other_rng, 78);
    Bag other_bag;
    bag_init(&other_bag, &other_rng);
    rng_seed(&first_rng, 77);
    bag_init(&first_bag, &first_rng);
    int differences = 0;
    for (int i = 0; i < 14; ++i) {
        if (bag_next(&first_bag, &first_rng) != bag_next(&other_bag, &other_rng)) {
            ++differences;
        }
    }
    CHECK(differences > 0);
}

void test_piece_suite(void)
{
    test_piece_shapes_are_sane();
    test_piece_rotations_are_consistent();
    test_piece_spawn_shapes();
    test_piece_cell_types();
    test_bag_is_seven_permutations();
    test_bag_is_deterministic();
}
