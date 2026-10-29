#ifndef MAAGE_RANDOM_TICKS_H
#define MAAGE_RANDOM_TICKS_H
#include "maage/world.h"
#ifdef __cplusplus
extern "C" {
#endif

/* Borrowed only during a callback. World owns and checkpoints both streams;
 * selection is the 1.7.10 32-bit coordinate LCG, behavior uses seeded xorshift.
 * Weather has a separate stream. Callbacks must not retain this pointer. */
typedef struct maage_random_tick_context {
    unsigned long selection, random;
    unsigned int sky_subtracted;
} maage_random_tick_context;
/* Uniform [0,bound), with nonzero bound required by the callback contract.
 * The callback's operation result covers this infallible numeric step. */
unsigned int maage_random_tick_next(maage_random_tick_context *context, unsigned int bound);
#ifdef __cplusplus
}
#endif
#endif
