#include "world_internal.h"
#include "maage/entities.h"
#include <string.h>

static unsigned long maage_random_tick_seed(maage_world_seed seed, unsigned long domain) {
    unsigned long value;
    value = ((seed.low ^ domain) * 1664525UL + seed.high + 1013904223UL) & 0xffffffffUL;
    value ^= value >> 16;
    value = (value * 2246822519UL) & 0xffffffffUL;
    value ^= value >> 13;
    return value & 0xffffffffUL;
}
maage_status maage_random_tick_default(maage_world_seed seed, maage_random_tick_state *state) {
    if (!state || seed.low > 0xffffffffUL || seed.high > 0xffffffffUL) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_INITIALIZE, 0);
    state->selection = maage_random_tick_seed(seed, 0x41c64e6dUL);
    state->random = maage_random_tick_seed(seed, 0x85ebca6bUL);
    if (!state->random) state->random = 1;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_INITIALIZE, 0);
}
int maage_random_tick_state_valid(const maage_random_tick_state *state) {
    /* Zero is a valid coordinate LCG state, but not a valid xorshift state. */
    return state && state->selection <= 0xffffffffUL && state->random && state->random <= 0xffffffffUL;
}

unsigned int maage_random_tick_next(maage_random_tick_context *context, unsigned int bound) {
    unsigned long value, limit = 0xffffffffUL - 0xffffffffUL % bound;
    do {
        value = context->state.random;
        value ^= (value << 13) & 0xffffffffUL;
        value ^= value >> 17;
        value ^= (value << 5) & 0xffffffffUL;
        context->state.random = value & 0xffffffffUL;
        /* Xorshift's nonzero state domain has 2^32-1 values. */
        value = context->state.random - 1UL;
    } while (value >= limit);
    return (unsigned int) (value % bound);
}

maage_status maage_random_tick_spawn_drop(maage_world *world, int x, int y, int z, const maage_item_stack *drop, maage_random_tick_context *context) {
    double position[3], velocity[3] = {0, 0.2, 0};
    position[0] = x + 0.15 + maage_random_tick_next(context, 701) / 1000.0;
    position[1] = y + 0.15 + maage_random_tick_next(context, 701) / 1000.0;
    position[2] = z + 0.15 + maage_random_tick_next(context, 701) / 1000.0;
    return maage_entities_spawn(maage_world_entities(world), maage_world_item_registry(world), drop, position, velocity, 10);
}
