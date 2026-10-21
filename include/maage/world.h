#ifndef MAAGE_WORLD_H
#define MAAGE_WORLD_H
#include "maage/blocks.h"
#include "maage/geometry.h"
#include "maage/seed.h"
#include "maage/biome.h"
#include <limits.h>
/* The column/save domain needs 8-bit bytes, signed coordinates through 30M,
 * and allocations larger than 64 KiB. Narrower targets can still build Core. */
#if CHAR_BIT != 8 || INT_MAX < 30000000L
#error World requires 8-bit bytes and an int coordinate range through 30000000
#endif
typedef char maage_world_required_address_space[((size_t)-1 >= 262144UL) ? 1 : -1];
#define MAAGE_CHUNK_EDGE 16
#define MAAGE_WORLD_HEIGHT 256
#define MAAGE_WORLD_LIMIT 30000000
/* Legacy Client compile-time compatibility only. Neither constant bounds World
 * cache occupancy; use the runtime capacity in maage_world_stats instead. */
#define MAAGE_WORLD_MAX_RADIUS (MAAGE_WORLD_LIMIT / MAAGE_CHUNK_EDGE - 4)
#define MAAGE_WORLD_MAX_CHUNKS 512

#ifdef __cplusplus
extern "C" {
#endif
typedef struct maage_world maage_world;
typedef struct maage_chunk maage_chunk;
typedef struct maage_world_mesh maage_world_mesh;
typedef struct maage_world_store maage_world_store;
typedef enum maage_chunk_state {
    MAAGE_CHUNK_REQUESTED = 0, MAAGE_CHUNK_LOADING, MAAGE_CHUNK_GENERATING,
    MAAGE_CHUNK_GENERATED, MAAGE_CHUNK_LIGHTING, MAAGE_CHUNK_MESH_PENDING,
    MAAGE_CHUNK_READY, MAAGE_CHUNK_SAVING, MAAGE_CHUNK_UNLOADING, MAAGE_CHUNK_FAILED
} maage_chunk_state;
typedef struct maage_world_config {
    const maage_block_registry *registry;
    const maage_biome_registry *biomes;
    maage_world_seed seed;
    maage_world_store *store; /* borrowed; close after world destruction */
    /* 0: logical processors minus one (minimum one), or eight if detection fails. */
    unsigned int worker_count;
    int render_distance;
    int new_world;
    double position[3];
    /* 0 derives a bounded default from the streaming footprint and workers.
     * Cache and accepted-job limits are independent runtime allocations. */
    unsigned int cache_capacity;
    unsigned int job_capacity;
} maage_world_config;
typedef struct maage_world_stats {
    unsigned int loaded, loaded_sections, pending, ready;
    unsigned int logical_processors, worker_count, queued_jobs, running_jobs;
    /* Accepted contexts include completed work awaiting owner-thread commits. */
    unsigned int generation_jobs, lighting_jobs, meshing_jobs, io_jobs;
    unsigned int generation_queued, generation_running;
    unsigned int lighting_queued, lighting_running;
    unsigned int meshing_queued, meshing_running;
    unsigned int io_queued, io_running;
    unsigned long generated, loaded_from_disk, meshes;
    unsigned long completed_jobs, cancelled_jobs, stale_jobs;
    unsigned long mutations, rejected_mutations;
    unsigned int dirty_columns;
    unsigned int resident_columns, cache_capacity, job_capacity;
    unsigned int completed_uncommitted_jobs;
    unsigned long queue_saturation_events, cache_pressure_events;
    double generated_columns_per_second;
    double scheduling_latency_ms;
    int failed;
} maage_world_stats;
typedef struct maage_world_observation {
    maage_biome_id biome_id;
    unsigned int section_count;
    unsigned char light; /* skylight in high nibble, block light in low nibble */
    int light_available;
} maage_world_observation;
typedef struct maage_world_mesh_update {
    int x, z;
    unsigned long revision;
    maage_world_mesh *mesh; /* ownership transfers to caller */
} maage_world_mesh_update;
typedef struct maage_world_target {
    int x, y, z;
    maage_block_state_id state_id;
    maage_block_id block_id;
    maage_block_face face;
    double distance;
    maage_block_shape bounds; /* Local selection shape. */
} maage_world_target;
typedef enum maage_world_mutation_result {
    MAAGE_WORLD_MUTATION_OK = 0,
    MAAGE_WORLD_MUTATION_UNAVAILABLE,
    MAAGE_WORLD_MUTATION_STALE,
    MAAGE_WORLD_MUTATION_INVALID,
    MAAGE_WORLD_MUTATION_OUT_OF_MEMORY
} maage_world_mutation_result;
/* Floor division/positive modulo, including negative world coordinates. */
int maage_world_chunk_coordinate(int coordinate);
int maage_world_local_coordinate(int coordinate);
/* One caller thread owns World membership, simulation observations and result
 * commits. All public World calls belong to that owner. A bounded pool executes
 * generation/loading/lighting/CPU meshing/saving on privately owned data; tasks
 * never hold graphical objects or mutate published columns. Finalized registry,
 * biomes and store must outlive World shutdown. */
maage_world *maage_world_create(const maage_world_config *config);
/* Nonblocking owner pump: commit completed work and schedule near-player jobs.
 * Call regularly, including while simulation is paused; no frame-wide waits. */
void maage_world_stream(maage_world *world, double x, double z, double yaw);
/* Cheap owner status; full diagnostic snapshots may be sampled less often. */
int maage_world_failed(const maage_world *world);
void maage_world_get_stats(maage_world *world, maage_world_stats *stats);
/* Owner-thread read-only diagnostics of published numeric column data. Returns
 * zero for an unavailable column or out-of-world coordinates. Biome zero is a
 * valid ID. Light is unavailable until the column lighting result is current.
 * No job submission, world lock, allocation or filesystem work. */
int maage_world_observe(const maage_world *world, int x, int y, int z,
    maage_world_observation *observation);
int maage_world_take_mesh(maage_world *world, maage_world_mesh_update *update);
/* Validate a detached CPU mesh immediately before owner-thread GPU upload. */
int maage_world_mesh_current(const maage_world *world, int x, int z, unsigned long revision);
int maage_world_chunk_loaded(const maage_world *world, int x, int z);
/* Blocking lifecycle boundary only: cancel obsolete work, finish saves and join
 * the pool. Never call inside the render loop. Zero means a failed world/save. */
int maage_world_flush(maage_world *world);
void maage_world_destroy(maage_world *world);
const maage_block_registry *maage_world_registry(const maage_world *world);
void maage_world_spawn(const maage_world *world, double feet[3]);
/* Missing/loading columns return air for visual queries but are solid barriers
 * for collision, preventing falls/teleports. Only complete columns are visible. */
maage_block_state_id maage_world_get(const maage_world *world, int x, int y, int z);
int maage_world_solid(const maage_world *world, int x, int y, int z);
/* Allocation-free DDA through selection shapes. Direction is normalized here;
 * range and returned distance are blocks. Missing columns stop traversal with
 * -1, never permit targeting through unavailable terrain. 0 miss, 1 hit. */
int maage_world_raycast(const maage_world *world, const double origin[3],
    const double direction[3], double range, maage_world_target *target);
/* Owner simulation phase only. Compare expected numeric state, then publish a
 * private copy; workers retain their old immutable snapshots. Invalidates
 * lighting/meshes and obsolete jobs without waiting. No filesystem work. */
maage_world_mutation_result maage_world_set(maage_world *world,
    int x, int y, int z, maage_block_state_id expected,
    maage_block_state_id replacement);
/* Local AABB; unavailable cells are full-cube collision barriers. */
int maage_world_collision_shape(const maage_world *world, int x, int y, int z,
    maage_block_shape *shape);
const maage_mesh_data *maage_world_mesh_data(const maage_world_mesh *mesh);
void maage_world_mesh_destroy(maage_world_mesh *mesh);
#ifdef __cplusplus
}
#endif
#endif
