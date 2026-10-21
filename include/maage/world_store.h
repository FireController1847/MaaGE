#ifndef MAAGE_WORLD_STORE_H
#define MAAGE_WORLD_STORE_H
#include "maage/world.h"
#include "maage/gameplay.h"
#include "maage/environment.h"
#ifdef __cplusplus
extern "C" {
#endif
#define MAAGE_WORLD_FORMAT_VERSION 3UL
#define MAAGE_PLAYER_FORMAT_VERSION 4UL
#define MAAGE_WORLD_CONFIGURED_FORMAT_VERSION 5UL
#define MAAGE_WORLD_ENVIRONMENT_FORMAT_VERSION 6UL
typedef struct maage_world_record {
    char id[65], name[128], created_utc[32];
    maage_world_seed seed;
    maage_generation_policy generation_policy;
    maage_environment_state environment;
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

/* With NULL id, generation_policy supplies creation defaults only if automatic
 * selection creates a world; resuming always uses the saved policy. An explicit
 * existing id requires NULL policy, rejecting overrides. NULL selects frozen
 * generator 1 for new worlds. Configured generators 2/3 require item definitions. */
maage_status maage_world_store_open_configured(const char *instance_root, const char *id,
    int create, const char *name, const maage_world_seed *seed,
    const maage_generation_policy *generation_policy, const maage_item_registry *items,
    maage_world_store **store, maage_world_record *record, char error[256], maage_status_trace *trace);
/* Borrows finalized item definitions until close so schemas 4-6 resolve stable
 * names to runtime IDs at import. NULL items retains legacy numeric mapping 1
 * and refuses named schemas. Caller owns optional occurrence trace; no logging. */

/* Startup/import only, before opening an instance world. The caller owns the
 * destination instance lease and must establish unambiguous/explicit ownership.
 * Legacy selection and every world lock exclude active writers. Atomic moves
 * preserve data in place; cross-filesystem fallback verifies copies before
 * promotion and retains originals. Journaled retries never overwrite worlds. */
int maage_world_store_legacy_pending(const char *data_root);
maage_status maage_world_store_migrate_legacy(const char *data_root,
    const char *instance_root, char error[256], maage_status_trace *trace);
/* Saves a caller-owned player snapshot without changing shared store data.
 * Serialize metadata saves with each other; chunk files are independent. */
maage_status maage_world_store_save_player(maage_world_store *store,
    const double position[3], double yaw, double pitch, char error[256], maage_status_trace *trace);
/* Metadata schemas 1-5 remain readable; schema 1 starts with Creative and empty
 * inventory, schema 2 has at most 64 pickups, schema 3 has a checked 32-bit
 * count and legacy numeric item mapping 1. Schema 4 writes stable namespaced
 * item names in each inventory/cursor/pickup stack and a checked pickup count.
 * Schema 5 adds generator-2 policy. New named writes use schema 6, preserving
 * generator 1/2/3 policy and the clock/weather snapshot alongside player data.
 * Named schemas require item definitions; missing names fail without overwriting.
 * Without item definitions writes use schema 3 and reject unmapped modded items. */

maage_status maage_world_store_save_environment_gameplay(maage_world_store *store,
    const double position[3], double yaw, double pitch,
    const maage_gameplay *game, const maage_item_registry *items,
    const maage_environment_state *environment, char error[256], maage_status_trace *trace);

/* All jobs borrowing this store must finish before close releases its lock. */
maage_status maage_world_store_close(maage_world_store *store);
/* Distinct coordinates may load/save concurrently: each job owns its buffers,
 * error storage and an immutable chunk snapshot, borrowing finalized registries.
 * The task owner serializes/coalesces writes to the same coordinate and keeps
 * store/registries alive until completion; no store-wide I/O mutex is needed.
 * Detached loaded columns transfer to caller. SUCCESS loads; dedicated
 * WORLD_IO/LOAD/NOT_FOUND means absent. Corruption never means air.
 * Format 1 remains readable with its original fixed numeric mappings (states
 * 0,1,2,3,7,9,12,13,24,256..261; biomes 0..5). Format 2 remains readable.
 * New writes use format 3, retaining format-2 identity palettes:
 * per-chunk palettes of stable namespaced biome names and block names plus
 * state metadata, with 8-bit biome and 16-bit palette indices. These indices
 * are local to a chunk, not runtime IDs; a chunk contains at most 65536 cells.
 * Unknown identities reject load/save without replacing existing data.
 * Format 3 adds generation-stage counts, relative pending fluid delays and
 * chunk-owned numeric block entities with checked versioned opaque payloads.
 * Older chunks migrate atomically on save; unknown/corrupt data is preserved.
 * Each chunk header generator version matches its parent world metadata. */
maage_status maage_world_store_load_chunk(maage_world_store *store, int x, int z,
    const maage_block_registry *blocks, const maage_biome_registry *biomes,
    maage_chunk **chunk, char error[256], maage_status_trace *trace);
/* Only WORLD_IO/LOAD/NOT_FOUND permits generation; registry NOT_FOUND is unmapped save data. Malformed/unmapped
 * saves and inaccessible files remain failures and preserve the original data.
 * Caller-owned outputs keep concurrent column I/O independent. */


/* Snapshot clock is captured when the async save is submitted. Pending updates
 * serialize remaining delays, so an older clock checkpoint after a crash cannot
 * postpone them by an entire previous session. Import rebases on column load. */
maage_status maage_world_store_save_chunk_at(maage_world_store *store,
    const maage_chunk *chunk, const maage_block_registry *blocks,
    const maage_biome_registry *biomes, unsigned long tick_low,
    unsigned long tick_high, char error[256], maage_status_trace *trace);

#ifdef __cplusplus
}
#endif
#endif
