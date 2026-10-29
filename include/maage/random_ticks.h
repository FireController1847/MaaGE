#ifndef MAAGE_RANDOM_TICKS_H
#define MAAGE_RANDOM_TICKS_H
#include "maage/world.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct maage_random_tick_state {
    unsigned long selection, random; /* Portable 32-bit words. */
} maage_random_tick_state;
maage_status maage_random_tick_default(maage_world_seed seed, maage_random_tick_state *state);
int maage_random_tick_state_valid(const maage_random_tick_state *state);
maage_status maage_world_random_tick_snapshot(const maage_world *world, maage_random_tick_state *state);

/* Borrowed only during a callback. World owns and checkpoints both streams;
 * selection uses a 32-bit coordinate LCG; behavior uses seeded xorshift.
 * Weather has a separate stream. Callbacks must not retain this pointer. */
typedef struct maage_random_tick_context {
    maage_random_tick_state state;
    unsigned int sky_subtracted;
} maage_random_tick_context;
/* Uniform [0,bound), with nonzero bound required by the callback contract.
 * The callback's operation result covers this infallible numeric step. */
unsigned int maage_random_tick_next(maage_random_tick_context *context, unsigned int bound);
#ifdef __cplusplus
}
#endif
#endif
