#ifndef MAAGE_WORLD_REGION_STORE_H
#define MAAGE_WORLD_REGION_STORE_H
#include <stddef.h>
#include "maage/status.h"
/* Private region I/O for one authoritative World store. The caller owns
 * immutable serialization buffers; records are individually zlib-compressed
 * and indexed by 32x32 chunk coordinates. Unmapped chunks return NOT_FOUND. */
typedef struct maage_region_store maage_region_store;
maage_status maage_region_store_create(const char *directory, const char *map_id, maage_region_store **output);
maage_status maage_region_store_destroy(maage_region_store *store);
maage_status maage_region_store_load(maage_region_store *store, int x, int z, unsigned char **output, size_t *length);
maage_status maage_region_store_save(maage_region_store *store, int x, int z, const unsigned char *data, size_t length);
#endif
