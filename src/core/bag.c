#include "core/bag.h"

#include "core/rng.h"

/* Fisher-Yates over the piece list, in place. */
static void bag_shuffle(Bag *bag, uint64_t *rng_state)
{
    for (int i = 0; i < PIECE_COUNT; ++i) {
        bag->pieces[i] = (uint8_t)i;
    }
    for (int i = PIECE_COUNT - 1; i > 0; --i) {
        const uint32_t j = rng_next_bounded(rng_state, (uint32_t)(i + 1));
        const uint8_t swapped = bag->pieces[i];
        bag->pieces[i] = bag->pieces[j];
        bag->pieces[j] = swapped;
    }
}

void bag_init(Bag *bag, uint64_t *rng_state)
{
    bag_shuffle(bag, rng_state);
    bag->index = 0;
}

PieceType bag_next(Bag *bag, uint64_t *rng_state)
{
    if (bag->index >= PIECE_COUNT) {
        bag_shuffle(bag, rng_state);
        bag->index = 0;
    }
    return (PieceType)bag->pieces[bag->index++];
}
