#ifndef MAAGE_WORLD_STORE_H
#define MAAGE_WORLD_STORE_H
#include "maage/world.h"
#include "maage/gameplay.h"
#define MAAGE_WORLD_FORMAT_VERSION 1UL
#define MAAGE_PLAYER_FORMAT_VERSION 2UL
typedef struct maage_world_record {
    char id[65], name[128], created_utc[32];
    maage_world_seed seed;
    double position[3], yaw, pitch;
    int new_world; /* pending initial spawn; persists until first player save */
    maage_gameplay_save gameplay;
} maage_world_record;
/* Metadata/import boundary only. IDs contain lowercase ASCII letters, digits,
 * '-' or '_'. NULL id resumes the last selection, creating a unique world if
 * none exists. A seed with NULL id creates a unique new world. create requests
 * a new world; an existing requested ID is never overwritten. Owns a lifetime
 * exclusive world lock. Errors are static, credential-free text (256 bytes). */
int maage_world_store_open(const char *data_root, const char *id, int create,
    const char *name, const maage_world_seed *seed, maage_world_store **store,
    maage_world_record *record, char error[256]);
/* Saves a caller-owned player snapshot without changing shared store data.
 * Serialize metadata saves with each other; chunk files are independent. */
int maage_world_store_save_player(maage_world_store *store,
    const double position[3], double yaw, double pitch, char error[256]);
/* Atomic schema-2 metadata snapshot, used after rendering stops. Schema-1 worlds
 * migrate to Creative with empty inventory and full vitals; chunk format stays 1.
 * Numeric item mapping 1 fixes 1,2,3,7,12,13,24, metadata/damage zero. Unknown
 * mappings fail without overwriting originals. Cursor and overflow pickups persist. */
int maage_world_store_save_gameplay(maage_world_store *store,
    const double position[3], double yaw, double pitch,
    const maage_gameplay *game, const maage_item_registry *items, char error[256]);
/* All jobs borrowing this store must finish before close releases its lock. */
void maage_world_store_close(maage_world_store *store);
/* Distinct coordinates may load/save concurrently: each job owns its buffers,
 * error storage and an immutable chunk snapshot, borrowing finalized registries.
 * The task owner serializes/coalesces writes to the same coordinate and keeps
 * store/registries alive until completion; no store-wide I/O mutex is needed.
 * Detached loaded columns transfer to caller. 1 loaded, 0 absent, -1 invalid/I/O
 * error; corruption never means air.
 * Format 1 fixes legacy numeric states 0,1,2,3,7,9,12,13,24 and grass states
 * 256..261 (type 2, metadata biome+1), plus biome IDs 0..5. Reordering or adding
 * saved identities requires a versioned migration, never reinterpretation. */
int maage_world_store_load_chunk(maage_world_store *store, int x, int z,
    const maage_block_registry *blocks, const maage_biome_registry *biomes,
    maage_chunk **chunk, char error[256]);
int maage_world_store_save_chunk(maage_world_store *store,
    const maage_chunk *chunk, const maage_block_registry *blocks,
    const maage_biome_registry *biomes, char error[256]);
#endif
