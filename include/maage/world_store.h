#ifndef MAAGE_WORLD_STORE_H
#define MAAGE_WORLD_STORE_H
#include "maage/world.h"
#include "maage/gameplay.h"
#define MAAGE_WORLD_FORMAT_VERSION 2UL
#define MAAGE_PLAYER_FORMAT_VERSION 3UL
typedef struct maage_world_record {
    char id[65], name[128], created_utc[32];
    maage_world_seed seed;
    double position[3], yaw, pitch;
    int new_world; /* pending initial spawn; persists until first player save */
    maage_gameplay_save gameplay;
} maage_world_record;
/* Metadata/import boundary only. instance_root is an absolute physical instance
 * directory containing instance.json; global application-data roots are rejected.
 * New world IDs are canonical UUIDs. Legacy safe IDs remain loadable unchanged.
 * NULL id resumes the instance's last selection, creating a unique world if
 * none exists. A seed with NULL id creates a unique new world. create requests
 * a new world; an existing requested ID is never overwritten. Owns a lifetime
 * exclusive world lock. Errors are static, credential-free text (256 bytes). */
int maage_world_store_open(const char *instance_root, const char *id, int create,
    const char *name, const maage_world_seed *seed, maage_world_store **store,
    maage_world_record *record, char error[256]);
/* Startup/import only, before opening an instance world. The caller owns the
 * destination instance lease and must establish unambiguous/explicit ownership.
 * Legacy selection and every world lock exclude active writers. Atomic moves
 * preserve data in place; cross-filesystem fallback verifies copies before
 * promotion and retains originals. Journaled retries never overwrite worlds. */
int maage_world_store_legacy_pending(const char *data_root);
int maage_world_store_migrate_legacy(const char *data_root,
    const char *instance_root, char error[256]);
/* Saves a caller-owned player snapshot without changing shared store data.
 * Serialize metadata saves with each other; chunk files are independent. */
int maage_world_store_save_player(maage_world_store *store,
    const double position[3], double yaw, double pitch, char error[256]);
/* Atomic schema-3 metadata snapshot; schema 1 and 2 remain readable. Schema 1
 * migrates to Creative/empty inventory/full vitals. Schema 3 uses a checked
 * 32-bit pickup count instead of schema 2's 8-bit count. Item mapping 1 remains
 * the seven original numeric IDs; expanding item persistence identities requires
 * an explicit modpack mapping integration, never silent reinterpretation. */
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
 * Format 1 remains readable with its original fixed numeric mappings (states
 * 0,1,2,3,7,9,12,13,24,256..261; biomes 0..5). New writes use format 2:
 * per-chunk palettes of stable namespaced biome names and block names plus
 * state metadata, with 8-bit biome and 16-bit palette indices. These indices
 * are local to a chunk, not runtime IDs; a chunk contains at most 65536 cells.
 * Unknown identities reject load/save without replacing existing data.
 * Existing format-1 chunks migrate atomically to format 2 on save. */
int maage_world_store_load_chunk(maage_world_store *store, int x, int z,
    const maage_block_registry *blocks, const maage_biome_registry *biomes,
    maage_chunk **chunk, char error[256]);
int maage_world_store_save_chunk(maage_world_store *store,
    const maage_chunk *chunk, const maage_block_registry *blocks,
    const maage_biome_registry *biomes, char error[256]);
#endif
