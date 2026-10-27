#ifndef MAAGE_WORLD_STORE_H
#define MAAGE_WORLD_STORE_H
#include "maage/world.h"
#include "maage/gameplay.h"
#include "maage/environment.h"
#include "maage/entities.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct maage_world_record {
    char id[37], name[128], created_utc[32];
    maage_world_seed seed;
    maage_generation_policy generation_policy;
    maage_environment_state environment;
    double position[3], yaw, pitch;
    int new_world; /* pending initial spawn; persists until first player save */
    maage_gameplay_save gameplay;
    maage_entity_save entities;
} maage_world_record;
/* Menu/admin I/O boundary: copied display metadata, never live gameplay data.
 * List owns a growable array released with free(); corrupt entries retain their
 * UUID and failure status so they remain visible without becoming playable.
 * Call while holding the instance lease; mutations also acquire the world lock. */
typedef struct maage_world_summary {
    char id[37], name[128], created_utc[32];
    unsigned int game_mode;
    maage_status status;
} maage_world_summary;
maage_status maage_world_store_list(const char *instance_root,
    const maage_item_registry *items, maage_world_summary **entries, size_t *count,
    char error[256], maage_status_trace *trace);
maage_status maage_world_store_rename(const char *instance_root, const char *id,
    const char *name, const maage_item_registry *items, char error[256], maage_status_trace *trace);
maage_status maage_world_store_delete(const char *instance_root, const char *id,
    char error[256], maage_status_trace *trace);
/* Metadata/import boundary only. instance_root is an absolute physical instance
 * directory containing instance.json; global application-data roots are rejected.
 * World IDs are canonical UUIDs. NULL id resumes the instance's last selection,
 * creating a unique world if none exists. A seed with NULL id creates a unique
 * new world. create requests a new world; existing IDs are never overwritten.
 * generation_policy supplies creation defaults only; resumed worlds always use
 * their saved policy. An explicit existing id requires NULL policy.
 * Borrows finalized item definitions until close. Owns an exclusive world lock
 * until close. Returned records borrow the store's initial entity array.
 * Errors are static, credential-free text (256 bytes); optional traces are
 * caller-owned. Only the current metadata/chunk encodings are accepted. */
maage_status maage_world_store_open(const char *instance_root, const char *id,
    int create, const char *name, const maage_world_seed *seed,
    const maage_generation_policy *generation_policy, const maage_item_registry *items,
    maage_world_store **store, maage_world_record *record, char error[256], maage_status_trace *trace);
/* Saves one complete caller-owned snapshot. Metadata writes must be serialized;
 * independent chunk files may be written concurrently. Player inventories and
 * World entities use stable namespaced item identities; no numeric mapping is persisted.
 * Environment and generation settings share the same atomic metadata replacement. */
maage_status maage_world_store_save(maage_world_store *store,
    const double position[3], double yaw, double pitch, const maage_gameplay *game, const maage_world *world,
    const maage_environment_state *environment, char error[256], maage_status_trace *trace);
/* All jobs borrowing this store must finish before close releases its lock. */
maage_status maage_world_store_close(maage_world_store *store);
/* Distinct coordinates may load/save concurrently: each job owns its buffers,
 * error storage and an immutable chunk snapshot, borrowing finalized registries.
 * The task owner serializes/coalesces writes to the same coordinate and keeps
 * store/registries alive until completion; no store-wide I/O mutex is needed.
 * Detached loaded columns transfer to caller. Only WORLD_IO/LOAD/NOT_FOUND
 * permits generation; registry NOT_FOUND means an unmapped save identity.
 * Per-column namespaced biome/block+metadata palettes resolve at I/O boundaries;
 * 8-bit biome/16-bit state indices are file-local rather than runtime IDs.
 * Columns retain completed generation stages, relative pending fluid delays and
 * chunk-owned numeric block entities with checked versioned opaque payloads.
 * Malformed/unmapped saves and inaccessible files remain failures and preserve
 * the original data. Old MaaGE encodings are rejected without conversion. */
maage_status maage_world_store_load_chunk(maage_world_store *store, int x, int z,
    const maage_block_registry *blocks, const maage_biome_registry *biomes,
    maage_chunk **chunk, char error[256], maage_status_trace *trace);
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
