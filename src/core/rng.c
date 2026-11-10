#include "core/rng.h"

#define SPLITMIX_GAMMA UINT64_C(0x9e3779b97f4a7c15)
#define SPLITMIX_MIX1 UINT64_C(0xbf58476d1ce4e5b9)
#define SPLITMIX_MIX2 UINT64_C(0x94d049bb133111eb)

void rng_seed(uint64_t *state, uint64_t seed)
{
    *state = seed;
}

uint64_t rng_next_u64(uint64_t *state)
{
    uint64_t z = (*state += SPLITMIX_GAMMA);

    z = (z ^ (z >> 30)) * SPLITMIX_MIX1;
    z = (z ^ (z >> 27)) * SPLITMIX_MIX2;
    return z ^ (z >> 31);
}

uint32_t rng_next_bounded(uint64_t *state, uint32_t bound)
{
    if (bound == 0) {
        return 0;
    }
    /* Modulo bias is irrelevant for gameplay decisions at these sizes. */
    return (uint32_t)(rng_next_u64(state) % bound);
}
