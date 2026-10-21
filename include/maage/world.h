#ifndef MAAGE_WORLD_H
#define MAAGE_WORLD_H
#include "maage/blocks.h"
#include "maage/geometry.h"
#include "maage/seed.h"
#include "maage/biome.h"
#include "maage/status.h"
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
/* Versions 1/2 are frozen; 3 layers regional features over the same base terrain. */
#define MAAGE_WORLD_GENERATOR_VERSION 1UL
#define MAAGE_GENERATION_CONFIGURABLE_VERSION 2UL
#define MAAGE_GENERATION_FEATURES_VERSION 3UL
#define MAAGE_GENERATION_POLICY_SCHEMA 1U
#ifdef __cplusplus
extern "C" {
#endif
typedef enum maage_generation_stage {
    MAAGE_GENERATION_STAGE_BASE = 0,
    MAAGE_GENERATION_STAGE_TERRAIN,
    MAAGE_GENERATION_STAGE_CAVES,
    MAAGE_GENERATION_STAGE_UNDERGROUND,
    MAAGE_GENERATION_STAGE_SURFACE,
    MAAGE_GENERATION_STAGE_VEGETATION,
    MAAGE_GENERATION_STAGE_STRUCTURES,
    MAAGE_GENERATION_STAGE_FINALIZE,
    MAAGE_GENERATION_STAGE_COUNT
} maage_generation_stage;
typedef struct maage_generation_policy {
    unsigned int schema_version;
    unsigned long generator_version;
    int sea_level;
    int spawn_search_radius;
    int spawn_search_step;
} maage_generation_policy;
maage_status maage_generation_policy_default(unsigned long generator_version,
    maage_generation_policy *policy);
int maage_generation_policy_valid(const maage_generation_policy *policy);

typedef struct maage_world maage_world;
typedef struct maage_chunk maage_chunk;
typedef struct maage_world_mesh maage_world_mesh;
typedef struct maage_world_store maage_world_store;
struct maage_environment_state;
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
    /* All-zero selects frozen generator-1 defaults. */
    maage_generation_policy generation_policy;
    /* Copied during creation; NULL starts the seeded clock/weather defaults. */
    const struct maage_environment_state *initial_environment;
} maage_world_config;
typedef struct maage_world_stats {
    unsigned int loaded, loaded_sections, pending, ready;
    unsigned int logical_processors, worker_count, queued_jobs, running_jobs;
    /* Accepted contexts include completed work awaiting owner-thread commits. */
    unsigned int generation_jobs, lighting_jobs, meshing_jobs, io_jobs;
    unsigned int generation_queued, generation_running;
    /* Coarse stages reported through scheduler-owned progress snapshots.
     * Zero entries mean no accepted worker currently reports that stage. */
    unsigned int generation_stage_jobs[8];
    unsigned long decorated_columns, structure_fragments;
    size_t pending_block_updates, pending_fluid_updates, block_entities;
    unsigned long block_updates_processed;
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
    maage_status last_result;
    maage_status last_job_result; /* Owner disposition, including cancellation/stale. */
    maage_status last_job_operation_result; /* Original callback outcome survives disposition. */
    unsigned long failure_reports; /* Owner failures; cancellation/staleness excluded. */
} maage_world_stats;
typedef struct maage_world_observation {
    maage_biome_id biome_id;
    unsigned int section_count;
    unsigned char light; /* skylight in high nibble, block light in low nibble */
    int light_available;
    double temperature, rainfall; /* temperature includes generator-3 altitude cooling */
    int sky_exposed;
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
/* Floor division/positive modulo, including negative world coordinates. */
int maage_world_chunk_coordinate(int coordinate);
int maage_world_local_coordinate(int coordinate);
/* One caller thread owns World membership, simulation observations and result
 * commits. All public World calls belong to that owner. A bounded pool executes
 * generation/loading/lighting/CPU meshing/saving on privately owned data; tasks
 * never hold graphical objects or mutate published columns. Finalized registry,
 * biomes and store must outlive World shutdown. */
/* Initial scheduling failure may transfer a partially running World through
 * output; always destroy a non-NULL output before releasing borrowed services. */
maage_status maage_world_create(const maage_world_config *config, maage_world **output,
    maage_status_trace *trace);
/* Nonblocking owner pump: commit completed work and schedule near-player jobs.
 * Call regularly, including while simulation is paused; no frame-wide waits. */
maage_status maage_world_stream(maage_world *world, double x, double z, double yaw,
    maage_status_trace *trace);
/* One ordered 20 TPS environment phase, with bounded scheduled block work.
 * Call only while simulation is active; streaming continues while paused. */
maage_status maage_world_tick(maage_world *world);
/* Cheap owner status; full diagnostic snapshots may be sampled less often. */
int maage_world_failed(const maage_world *world);
/* Latest important owner-observed failure, retained across successful jobs.
 * SUCCESS before any failure; no global last-error state. */
maage_status maage_world_status(const maage_world *world);
const maage_status_trace *maage_world_trace(const maage_world *world);
const maage_status_trace *maage_world_last_job_trace(const maage_world *world);
void maage_world_get_stats(maage_world *world, maage_world_stats *stats);
/* Owner-thread read-only diagnostics of published numeric column data. Returns
 * zero for an unavailable column or out-of-world coordinates. Biome zero is a
 * valid ID. Light is unavailable until the column lighting result is current.
 * No job submission, world lock, allocation or filesystem work. */
int maage_world_observe(const maage_world *world, int x, int y, int z,
    maage_world_observation *observation);
maage_status maage_world_take_mesh(maage_world *world, maage_world_mesh_update *update);
/* Validate a detached CPU mesh immediately before owner-thread GPU upload. */
int maage_world_mesh_current(const maage_world *world, int x, int z, unsigned long revision);
int maage_world_chunk_loaded(const maage_world *world, int x, int z);
/* Blocking lifecycle boundary only: cancel obsolete work, finish saves and join
 * the pool. Never call inside the render loop. Failures preserve dirty save data. */
maage_status maage_world_flush(maage_world *world, maage_status_trace *trace);
/* A native join failure retains World and every borrowed dependency for retry.
 * SUCCESS consumes World even if an earlier completed flush reported data errors. */
maage_status maage_world_destroy(maage_world *world);
const maage_block_registry *maage_world_registry(const maage_world *world);
void maage_world_spawn(const maage_world *world, double feet[3]);
/* Missing/loading columns return air for visual queries but are solid barriers
 * for collision, preventing falls/teleports. Only complete columns are visible. */
maage_block_state_id maage_world_get(const maage_world *world, int x, int y, int z);
int maage_world_solid(const maage_world *world, int x, int y, int z);
/* Allocation-free DDA through selection shapes. Direction is normalized here;
 * range and returned distance are blocks. Missing columns stop traversal with
 * NOT_FOUND, never permit targeting through unavailable terrain. SUCCESS hit, NOOP miss. */
maage_status maage_world_raycast(const maage_world *world, const double origin[3],
    const double direction[3], double range, maage_world_target *target);
/* Owner simulation phase only. Compare expected numeric state, then publish a
 * private copy; workers retain their old immutable snapshots. Invalidates
 * lighting/meshes and obsolete jobs without waiting. No filesystem work. */
maage_status maage_world_set(maage_world *world,
    int x, int y, int z, maage_block_state_id expected,
    maage_block_state_id replacement);
/* Local AABB; unavailable cells are full-cube collision barriers. */
int maage_world_collision_shape(const maage_world *world, int x, int y, int z,
    maage_block_shape *shape);
const maage_mesh_data *maage_world_mesh_data(const maage_world_mesh *mesh);
maage_status maage_world_mesh_destroy(maage_world_mesh *mesh);
#ifdef __cplusplus
}
#endif
#endif
