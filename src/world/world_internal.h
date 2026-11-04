#ifndef MAAGE_WORLD_INTERNAL_H
#define MAAGE_WORLD_INTERNAL_H

#include "maage/world.h"
#include "maage/generation.h"
#include "maage/biome.h"
#include "maage/seed.h"
#include "maage/jobs.h"
#include "maage/environment.h"
#include "maage/random_ticks.h"
#include "maage/content.h"
#define MAAGE_SECTION_VOLUME 4096
/* Mutable sky uses two 4-bit cells per byte; immutable uniform sections elide cells. */
#define MAAGE_DIRECT_SKY_BYTES (MAAGE_WORLD_HEIGHT * MAAGE_CHUNK_EDGE * MAAGE_CHUNK_EDGE / 2)
/* All block/light arrays are numeric and owned by their column. Empty sections
 * are NULL; implicit light uses cached direct sky when initialized, otherwise
 * the initial heightmap sky seed. */
/* Mutable sections have direct 32-bit state arrays; published sections use a
 * dynamically sized, bit-packed palette. Lighting remains independently mutable.
 * A physical section has at most 4096 unique states; registries remain unbounded
 * within their existing 32-bit identity domain. */
typedef struct maage_section_payload maage_section_payload;
typedef struct maage_section_light_payload maage_section_light_payload;
typedef struct maage_sky_payload maage_sky_payload;
typedef struct maage_section {
    unsigned char *light; /* Private on first mutation; shared across immutable snapshots. */
    maage_section_light_payload *light_payload;
    maage_section_payload *payload; /* Immutable shared block palette, never lighting. */
    unsigned int *states;
    unsigned int *palette;
    unsigned char *packed;
    unsigned int palette_count, bits;
} maage_section;
maage_section *maage_section_create(maage_block_state_id air);
void maage_section_destroy(maage_section *section);
maage_section *maage_section_clone(const maage_section *section);
maage_block_state_id maage_section_get(const maage_section *section, size_t cell);
maage_status maage_section_set(maage_section *section, size_t cell, maage_block_state_id state);
maage_status maage_section_expand(maage_section *section);
void maage_chunk_compact(maage_chunk *chunk);
maage_status maage_section_export(const maage_section *section, maage_column_section *packet);
maage_status maage_section_import(const maage_column_section *packet, const maage_block_registry *registry, maage_section **output);
size_t maage_section_payload_resident_bytes(const maage_section *section);
size_t maage_section_light_resident_bytes(const maage_section *section);
maage_status maage_section_light_writable(maage_section *section);
size_t maage_chunk_sky_resident_bytes(const maage_chunk *chunk);
maage_status maage_chunk_sky_writable(maage_chunk *chunk);
void maage_chunk_sky_share(maage_chunk *destination, const maage_chunk *source);
int maage_chunk_sky_equal(const maage_chunk *left, const maage_chunk *right);
typedef struct maage_update_index {
    unsigned int kind, cell;
    size_t heap_index; /* index+1, 0 empty, (size_t)-1 tombstone */
} maage_update_index;
/* One exact transition, with independent completion bits for CHANGED, self
 * support and six neighbors. Distinct transitions are never coalesced. A
 * deferred provider resumes only while its receiving state is unchanged. */
typedef struct maage_block_change {
    unsigned int cell, pending;
    maage_block_state_id before, after;
    unsigned long origin;
    size_t continuation[MAAGE_FACE_COUNT];
    maage_block_state_id receiver[MAAGE_FACE_COUNT];
} maage_block_change;
#define MAAGE_BLOCK_CHANGE_PENDING 255U
struct maage_chunk {
    int x, z;
    maage_block_state_id air;       /* Implicit empty sections use the registered state. */
    unsigned char *direct_sky;      /* Sky payload present; access only through the numeric helpers. */
    maage_sky_payload *sky_payload; /* Shared until lighting explicitly detaches. */
    maage_section *sections[16];
    maage_biome_id biomes[256];
    unsigned short heightmap[256]; /* first air Y, 0..256 */
    /* Derived once after load/generation, then maintained by owner mutations. */
    unsigned short random_tick_counts[16];
    maage_scheduled_update *updates;
    size_t update_count, update_capacity;
    size_t fluid_update_count;
    maage_update_index *update_index;
    size_t update_index_capacity, update_index_used;
    maage_block_change *changes;
    size_t change_count, change_capacity;
    maage_block_entity *entities;
    size_t entity_count, entity_capacity;
    int environment_initialized;
    int updates_relative; /* file import only; rebase before owner publication */
    unsigned int generation_stages;
    unsigned long generation_features[8];
};
unsigned char maage_chunk_direct_sky_get(const maage_chunk *chunk, size_t cell);
/* Requires a private dense cache obtained through maage_chunk_sky_writable. */
void maage_chunk_direct_sky_set(maage_chunk *chunk, size_t cell, unsigned char value);
maage_status maage_chunk_clone(const maage_chunk *source, maage_chunk **output);
/* Seed one edited cell from effective opacity/emission, its sky shaft and
 * retained neighboring light. The asynchronous solver still owns convergence. */
maage_status maage_light_provisional_cell(maage_chunk *chunk, const maage_chunk *neighbors[4], const maage_block_registry *blocks, int x, int y, int z);
maage_status maage_light_column_job(maage_chunk *chunk, const maage_chunk *neighbors[4], const unsigned char dirty[8192], unsigned boundaries, const maage_block_registry *blocks, const maage_job_token *token, maage_status_trace *trace);
maage_status maage_chunk_create(int x, int z, maage_block_state_id air, maage_chunk **output);
maage_status maage_chunk_destroy(maage_chunk *chunk);
size_t maage_chunk_cell(int x, int y, int z);
maage_block_state_id maage_chunk_get(const maage_chunk *chunk, int x, int y, int z);
maage_status maage_chunk_set(maage_chunk *chunk, int x, int y, int z, maage_block_state_id state);
unsigned char maage_chunk_light(const maage_chunk *chunk, int x, int y, int z);
/* Presentation-only approximation until a numerical cache exists. */
unsigned char maage_chunk_presentation_light(const maage_chunk *chunk, const maage_block_registry *blocks, int x, int y, int z);
/* Private/owner-edit columns only. Queue entries are unique per (kind,cell),
 * ordered by due time; the finite voxel domain bounds fluid pending work. */
maage_status maage_chunk_schedule_update(maage_chunk *chunk, const maage_scheduled_update *update);
maage_status maage_chunk_take_update(maage_chunk *chunk, unsigned long now_low, unsigned long now_high, maage_scheduled_update *update);
maage_status maage_chunk_copy_environment(maage_chunk *destination, const maage_chunk *source);
maage_status maage_chunk_replace_environment(maage_chunk *destination, const maage_chunk *source);
maage_status maage_chunk_environment_initialize(maage_chunk *chunk, const maage_block_registry *blocks, unsigned long now_low, unsigned long now_high);
maage_status maage_chunk_random_ticks_initialize(maage_chunk *chunk, const maage_block_registry *blocks, int *leaf_rechecks);
/* Missing destinations recheck on column import, without removal-event queues. */
struct maage_item_stack;
maage_status maage_random_tick_spawn_drop(maage_world *world, int x, int y, int z, const struct maage_item_stack *drop, maage_random_tick_context *context);
maage_status maage_chunk_reserve_change(maage_chunk *chunk);
maage_status maage_world_support_changed(maage_world *world, int x, int y, int z, unsigned int face, int *deferred);
maage_status maage_chunk_block_entity_set(maage_chunk *chunk, unsigned int cell, unsigned long type, unsigned long version, const void *data, size_t length);
maage_status maage_chunk_block_entity_remove(maage_chunk *chunk, unsigned int cell);
/* Workers mutate only detached columns. Published columns are immutable and
 * pinned by the owner until every snapshot job has returned its completion.
 * Generation is original and has no vanilla seed-parity requirement. */
maage_status maage_generate_chunk_job(maage_chunk *chunk, maage_world_seed seed, const maage_block_registry *blocks, const maage_biome_registry *biomes, const maage_generation_policy *policy, const maage_job_token *token, maage_status_trace *trace);
maage_status maage_generation_spawn(maage_world_seed seed, const maage_block_registry *blocks, const maage_biome_registry *biomes, const maage_generation_policy *policy, double feet[3]);
/* Feature stages replay bounded regional owners in coordinate order and
 * clip writes to the private output column; no neighbor storage is borrowed. */
/* All inputs are pinned immutable snapshots. Client retains copied CPU/GPU
 * geometry, never borrowed columns. Cancellation leaves partial output private. */
maage_status maage_world_mesh_create_chunk_job(const maage_block_registry *blocks, const maage_biome_registry *biomes, const maage_chunk *chunk, const maage_chunk *neighbors[8], maage_leaves_mode mode, maage_biome_blend blend, const maage_job_token *token, size_t reserved_bytes, size_t *required_bytes, maage_world_mesh **output, maage_status_trace *trace);
size_t maage_world_mesh_resident_bytes(const maage_world_mesh *mesh);
/* Owner-thread borrowed effective inputs for bounded presentation patches;
 * a new column participates only after its whole mesh is prepared. */
int maage_world_present_inputs(const maage_world *world, int x, int z, const maage_chunk **chunk, const maage_chunk *neighbors[8], const maage_block_registry **blocks, const maage_biome_registry **biomes);
#endif
