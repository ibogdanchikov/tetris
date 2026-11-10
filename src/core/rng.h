#pragma once

#include <stdint.h>

/*
 * splitmix64. Counter based, so the whole generator state is one uint64_t and
 * a state snapshot is enough to replay the sequence.
 */

#define RNG_DEFAULT_SEED UINT64_C(0x853c49e6748fea9b)

/* Sets the generator state. Any seed value is valid, including 0. */
void rng_seed(uint64_t *state, uint64_t seed);

/* Advances the state and returns the next 64-bit value. */
uint64_t rng_next_u64(uint64_t *state);

/* Advances the state and returns a value in [0, bound). bound must be > 0. */
uint32_t rng_next_bounded(uint64_t *state, uint32_t bound);
