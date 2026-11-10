#pragma once

#include <stdint.h>

#include "core/piece.h"

/*
 * 7-bag randomiser. The bag holds one permutation of the seven pieces and
 * deals them out; when it runs dry it is reshuffled, so every aligned group of
 * seven draws contains each piece exactly once. The generator state lives in
 * the game (one uint64_t), never in the bag, so a state snapshot is enough to
 * replay a game.
 */
typedef struct Bag {
    uint8_t pieces[PIECE_COUNT];
    uint8_t index;
} Bag;

/* Fills and shuffles a fresh bag. Consumes rng_state. */
void bag_init(Bag *bag, uint64_t *rng_state);

/* Deals the next piece, reshuffling first when the bag is empty. */
PieceType bag_next(Bag *bag, uint64_t *rng_state);
