#ifndef MAAGE_SEED_H
#define MAAGE_SEED_H
#include <stddef.h>
#include "maage/status.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Signed 64-bit two's-complement value, carried as two unsigned 32-bit words
 * for strict C90. Only the low 32 bits of each word are significant. */
typedef struct maage_world_seed {
    unsigned long high, low;
} maage_world_seed;
maage_status maage_world_seed_parse(const char *decimal, maage_world_seed *seed);
maage_status maage_world_seed_format(maage_world_seed seed, char *text, size_t capacity);
#ifdef __cplusplus
}
#endif
#endif
