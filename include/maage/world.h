#ifndef MAAGE_WORLD_H
#define MAAGE_WORLD_H
#include "maage/blocks.h"
#include "maage/execution.h"
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
typedef char maage_world_required_address_space[((size_t) - 1 >= 262144UL) ? 1 : -1];
#define MAAGE_CHUNK_EDGE 16
#define MAAGE_WORLD_HEIGHT 256
#define MAAGE_WORLD_LIMIT 30000000
/* Coordinate-derived render bound; cache occupancy is configured at runtime. */
#define MAAGE_WORLD_MAX_RADIUS (MAAGE_WORLD_LIMIT / MAAGE_CHUNK_EDGE - 4)
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
    int sea_level;
    int spawn_search_radius;
    int spawn_search_step;
} maage_generation_policy;
/* One current algorithm; defaults are sea level 63, spawn radius 4096/step 16. */
maage_status maage_generation_policy_default(maage_generation_policy *policy);
int maage_generation_policy_valid(const maage_generation_policy *policy);

typedef struct maage_world maage_world;
typedef struct maage_chunk maage_chunk;
typedef struct maage_world_mesh maage_world_mesh;
typedef enum maage_leaves_mode {
    MAAGE_LEAVES_FAST = 0,
    MAAGE_LEAVES_SMART,
    MAAGE_LEAVES_FANCY
} maage_leaves_mode;
/* Client visual quality; never affects authoritative biome identities. */
typedef enum maage_biome_blend {
    MAAGE_BIOME_BLEND_OFF = 0,
    MAAGE_BIOME_BLEND_VANILLA,
    MAAGE_BIOME_BLEND_SMOOTH_5,
    MAAGE_BIOME_BLEND_SMOOTH_7,
    MAAGE_BIOME_BLEND_SMOOTH_9
} maage_biome_blend;
typedef struct maage_world_store maage_world_store;
struct maage_environment_state;
struct maage_random_tick_state;
struct maage_item_registry;
struct maage_smelting_registry;
struct maage_fuel_registry;
struct maage_entity_type_registry;
struct maage_entity_policy;
const struct maage_entity_type_registry *maage_world_entity_types(const maage_world *world);
const struct maage_smelting_registry *maage_world_smelting_registry(const maage_world *world);
const struct maage_fuel_registry *maage_world_fuel_registry(const maage_world *world);
typedef enum maage_chunk_state {
    MAAGE_CHUNK_REQUESTED = 0,
    MAAGE_CHUNK_LOADING,
    MAAGE_CHUNK_GENERATING,
    MAAGE_CHUNK_GENERATED,
    MAAGE_CHUNK_LIGHTING,
    MAAGE_CHUNK_MESH_PENDING,
    MAAGE_CHUNK_READY,
    MAAGE_CHUNK_SAVING,
    MAAGE_CHUNK_UNLOADING,
    MAAGE_CHUNK_FAILED
} maage_chunk_state;
typedef enum maage_world_cache_policy {
    MAAGE_WORLD_CACHE_RECLAIM_ON_SHRINK = 0, /* Default: reclaim unneeded columns. */
    MAAGE_WORLD_CACHE_RETAIN_ON_SHRINK = 1   /* Preserve fast re-expansion. */
} maage_world_cache_policy;
/* Runtime behavior callbacks receiving World can query its immutable execution
 * value; worker jobs and presentation replicas never borrow Server state. */
const maage_execution_context *maage_world_execution(const maage_world *world);
/* Owner simulation square; does not load or request terrain. */
int maage_world_simulation_contains(const maage_world *world, double x, double z);
typedef struct maage_world_config {
    maage_execution_context execution; /* Required: Client replica or authoritative Server owner. */
    const maage_block_registry *registry;
    const struct maage_item_registry *items; /* finalized; borrowed for block drops */
    const struct maage_smelting_registry *smelting;
    const struct maage_fuel_registry *fuels;
    const struct maage_entity_type_registry *entity_types; /* Immutable logical types, borrowed. */
    const struct maage_entity_policy *entity_policy;       /* Optional copied owner scheduling/population policy. */
    const maage_biome_registry *biomes;
    maage_world_seed seed;
    maage_world_store *store; /* borrowed; close after world destruction */
    /* 0: logical processors minus one (minimum one), or eight if detection fails. */
    unsigned int worker_count;
    int render_distance;
    /* Authoritative active square, independent of Client visual settings.
     * Streaming retains this footprint even with a smaller render distance. */
    int simulation_distance;
    /* Attempts per eligible 16^3 section per 20 TPS tick. Explicit 0 disables.
     * Initialize with maage_world_config_init for the default of 3. */
    unsigned int random_tick_speed;
    /* Callback deliveries per owner drain; zero selects 8192. Excess work
     * remains queued and progresses fairly in subsequent owner ticks. */
    unsigned int neighbor_update_budget;
    int presentation_only; /* Replica: imported states only, no generation/simulation/save. */
    int build_meshes;      /* Initialized to 1; authoritative Server sets 0. */
    int new_world;
    void (*emit_break)(void *, int, int, int, maage_block_state_id);            /* Successful genuine removal; shares sound_context. */
    void (*emit_sound)(void *, unsigned long, const double[3], double, double); /* Owner-thread occurrence sink. */
    void *sound_context;
    double position[3];
    /* 0 derives a bounded default from the streaming footprint and workers.
     * Cache and accepted-job limits are independent runtime allocations. */
    unsigned int cache_capacity;
    unsigned int job_capacity;
    /* Soft byte admission budget for accepted transient work and pending CPU
     * meshes. 0 selects MAAGE_WORLD_JOB_MEMORY_MIB or 64 MiB. One oversized
     * job can proceed alone; saves retain an independent quarter-budget. */
    size_t job_memory_budget;
    maage_world_cache_policy cache_policy; /* Residency policy, not a logical world-size limit. */
    /* All-zero selects the current generation defaults. */
    maage_generation_policy generation_policy;
    /* Copied during creation; NULL starts the seeded clock/weather defaults. */
    const struct maage_environment_state *initial_environment;
    /* Copied independently; NULL uses deterministic seed/domain defaults. */
    const struct maage_random_tick_state *initial_random_ticks;
} maage_world_config;
maage_status maage_world_config_init(maage_world_config *config);
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
    /* Last active simulation tick; both zero when random ticking is disabled. */
    unsigned int random_tick_attempts, random_tick_callbacks;
    unsigned int dirty_columns;
    unsigned int resident_columns, cache_capacity, job_capacity;
    unsigned int completed_uncommitted_jobs;
    unsigned long queue_saturation_events, cache_pressure_events;
    unsigned long memory_pressure_events;
    size_t job_memory_budget, reserved_job_bytes, pending_mesh_bytes;
    double generated_columns_per_second;
    double scheduling_latency_ms;
    int failed;
    maage_status last_result;
    maage_status last_job_result;           /* Owner disposition, including cancellation/stale. */
    maage_status last_job_operation_result; /* Original callback outcome survives disposition. */
    unsigned long failure_reports;          /* Owner failures; cancellation/staleness excluded. */
    size_t resident_storage_bytes;          /* Owner-indexed columns, snapshots and pending meshes; excludes worker outputs and Client upload handoffs. */
} maage_world_stats;
typedef struct maage_world_observation {
    maage_biome_id biome_id;
    unsigned int section_count;
    unsigned char light; /* skylight in high nibble, block light in low nibble */
    int light_available;
    double temperature, rainfall; /* temperature includes altitude cooling */
    int sky_exposed;
    int precipitation_height; /* First cell above the column heightmap. */
} maage_world_observation;
typedef struct maage_world_mesh_update {
    int x, z;
    unsigned long revision;
    unsigned long state_revision, presentation_generation;
    int predicted;
    double completed_at, initialized_at; /* Local completion/initial import times, never remote timestamps. */
    maage_world_mesh *mesh;              /* ownership transfers to caller */
} maage_world_mesh_update;
typedef struct maage_world_target {
    int x, y, z;
    maage_block_state_id state_id;
    maage_block_id block_id;
    maage_block_face face;
    double distance;
    maage_block_shape bounds; /* Local selection shape. */
} maage_world_target;
typedef struct maage_block_position {
    int x, y, z;
} maage_block_position;
/* Bounded replica-owner camera collision volume. Zero column revisions mean
 * unavailable terrain, never confirmed air. No borrowed World storage. */
#define MAAGE_CAMERA_RADIUS 5
#define MAAGE_CAMERA_EDGE 11
#define MAAGE_CAMERA_CELLS 1331
typedef struct maage_world_camera_sample {
    int valid, x, y, z;
    unsigned long revisions[4];
    maage_block_state_id states[MAAGE_CAMERA_CELLS];
} maage_world_camera_sample;
maage_status maage_world_sample_camera(const maage_world *world, int x, int y, int z, maage_world_camera_sample *sample);
unsigned long maage_world_column_revision(const maage_world *world, int x, int z);
/* Source quads retain compact ownership and per-face draw metadata. Filtering
 * regenerates owned scratch indices; resident source vertices are optional
 * after a successful Client upload. */
maage_status maage_world_mesh_filter(const maage_world_mesh *mesh, const maage_block_position *removed, size_t count, maage_mesh_data *data, unsigned int **indices);
/* Exact geometry for the deduplicated 3x3x3 cells and axial foliage dependencies
 * around at most 32 edits.
 * Sources lend retained owner rectangles so edits can replace merged faces
 * without holes outside the local cells. Sources outlive this synchronous call.
 * Uses effective replica state immediately in prepared/presented columns,
 * independent of subsequent lighting refinement.
 * Filter removes these same owner cells from cached complete meshes. */
maage_status maage_world_mesh_patch(const maage_world *replica, const maage_block_position *centers, size_t count, const maage_world_mesh *const *sources, size_t source_count, maage_leaves_mode leaves, maage_biome_blend blend, maage_world_mesh **output, maage_status_trace *trace);
unsigned long maage_world_presentation_revision(const maage_world *replica);
/* Owned numeric state transfer; no light, scheduling, entities or file data.
 * Ordered unloads retire an exact base after all preceding packets.
 * base_revision==0 replaces a column with owned palette-packed sections
 * (absent sections are air). Otherwise sorted cells are exact-base deltas. */
typedef struct maage_column_cell {
    unsigned int cell;
    maage_block_state_id state;
} maage_column_cell;
/* IDs retain their full numeric domain; at most 4096 distinct states occur
 * in one physical section. packed follows palette in the same allocation.
 * bits is 0,1,2,4,8 or 16; indices are little-endian within each byte/pair. */
typedef struct maage_column_section {
    unsigned int palette_count, bits;
    unsigned int *palette;
    unsigned char *packed;
} maage_column_section;
typedef struct maage_column_update {
    int x, z;
    unsigned long revision, base_revision;
    int unload; /* Ordered interest removal; no cells/biomes, exact acknowledged base. */
    size_t count;
    maage_column_cell *cells;
    maage_biome_id *biomes;            /* 256 entries on complete publications only */
    maage_column_section sections[16]; /* Complete publications only; no lighting. */
} maage_column_update;
maage_status maage_world_next_column(maage_world *world, maage_column_update **output);
/* Called after ownership transfer; reads copied identity fields only. */
maage_status maage_world_ack_column(maage_world *world, const maage_column_update *update);
maage_status maage_column_update_destroy(maage_column_update *update);
maage_status maage_world_apply_column(maage_world *replica, const maage_column_update *update);
/* Presentation overlay over the replica's independently retained authority.
 * Ordered expected/replacement pairs compose edits at one cell. Unexpected
 * authoritative states remain visible. Nonzero confirmed revisions fence edits
 * once authority reaches their result; generation owns the whole overlay. */
maage_status maage_world_present_predictions(maage_world *replica, const maage_block_position *positions, const maage_block_state_id *expected, const maage_block_state_id *replacements, const unsigned long *confirmed_revisions, size_t count, unsigned long generation);
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
maage_status maage_world_create(const maage_world_config *config, maage_world **output, maage_status_trace *trace);
/* Nonblocking owner pump: commit completed work and schedule near-player jobs.
 * Call regularly, including while simulation is paused; no frame-wide waits. */
maage_status maage_world_stream(maage_world *world, double x, double z, double yaw, maage_status_trace *trace);
/* Owner-only interest change; simulation distance and accepted Jobs stay fixed.
 * Zero capacity derives the normal footprint bound; replicas supply negotiated
 * source residency plus in-flight retention. Existing capacity never shrinks,
 * so dirty columns and ordered replica bases survive until normal retirement.
 * Does not allocate columns, wait for Jobs or regenerate existing terrain. */
maage_status maage_world_set_render_distance(maage_world *world, int distance, unsigned int cache_capacity);
/* Runtime visual change: invalidate meshes, preserve columns and lighting.
 * The next nonblocking World stream schedules replacement CPU mesh jobs. */
/* Owner-only atomic presentation publication; no job wait or World restart.
 * Numeric logical identity must be unchanged. Retain old views until borrowed()
 * returns zero (or successful World destruction). */
maage_status maage_world_refresh_presentation(maage_world *world, const maage_block_registry *blocks, const maage_biome_registry *biomes);
int maage_world_presentation_borrowed(const maage_world *world, const maage_block_registry *blocks);
maage_status maage_world_set_leaves_mode(maage_world *world, maage_leaves_mode mode);
/* Presentation replicas only; invalidates geometry without scheduling light. */
maage_status maage_world_set_biome_blend(maage_world *world, maage_biome_blend blend);
/* One ordered 20 TPS environment phase, with bounded scheduled block work.
 * Call only while simulation is active; streaming continues while paused. */
maage_status maage_world_tick(maage_world *world);
/* Owner thread only; validates aggregate sampling work against unsigned-int
 * capacity. No change to weather, fixed-step timing or scheduled updates. */
maage_status maage_world_set_random_tick_speed(maage_world *world, unsigned int speed);
/* Replica presentation query retains its last initialized visual light while
 * a replacement is running. Initial/unavailable columns return NOOP. */
/* SUCCESS: calculated; PARTIAL: retained/approximate pending light; NOOP:
 * unavailable terrain (output unchanged). Approximation never enters solver data. */
maage_status maage_world_present_light(const maage_world *world, int x, int y, int z, unsigned char *light);
/* Allocation-free gameplay query. NOOP means unavailable/currently relighting;
 * never interpret it as darkness. Y>=256 clamps to 255. */
maage_status maage_world_sample_light(const maage_world *world, int x, int y, int z, unsigned char *light);
/* Cheap owner status; full diagnostic snapshots may be sampled less often. */
int maage_world_failed(const maage_world *world);
/* Latest important owner-observed failure, retained across successful jobs.
 * SUCCESS before any failure; no global last-error state. */
maage_status maage_world_status(const maage_world *world);
const maage_status_trace *maage_world_trace(const maage_world *world);
const maage_status_trace *maage_world_last_job_trace(const maage_world *world);
void maage_world_get_stats(maage_world *world, maage_world_stats *stats);
/* Owner-thread O(1) retained membership count, including replicas awaiting unload. */
unsigned int maage_world_resident_columns(const maage_world *world);
/* Owner-thread read-only diagnostics of published numeric column data. Returns
 * zero for an unavailable column or out-of-world coordinates. Biome zero is a
 * valid ID. Light is unavailable until the column lighting result is current.
 * No job submission, world lock, allocation or filesystem work. */
int maage_world_observe(const maage_world *world, int x, int y, int z, maage_world_observation *observation);
maage_status maage_world_take_mesh(maage_world *world, maage_world_mesh_update *update);
/* Before first presentation this also requires coherent current lighting and
 * exact center/halo light inputs. Presented columns retain geometry validity
 * while later lighting refines it. */
int maage_world_mesh_current(const maage_world *world, int x, int z, unsigned long revision);
/* First-frame presentation waits for closer rings, not for worker completion
 * order. Previously presented geometry can continue refining independently. */
int maage_world_mesh_can_present(const maage_world *world, int x, int z);
/* Replica owner confirms complete GPU preparation for this CPU revision.
 * Local patches may then accompany its first coherent whole-column frame.
 * Revision zero revokes preparation when an unpresented GPU cache is dropped. */
maage_status maage_world_mesh_prepared(maage_world *world, int x, int z, unsigned long revision);
/* Replica owner acknowledges a successfully submitted first coherent frame,
 * including an empty mesh. No GPU objects or renderer state enter World. */
maage_status maage_world_mesh_presented(maage_world *world, int x, int z, unsigned long revision);
/* Preparation failures can retry without losing previous geometry. A newer
 * pending/running mesh wins; unchanged failures use a short retry backoff. */
maage_status maage_world_retry_mesh(maage_world *world, int x, int z, unsigned long revision);
/* Client replica owner only: reconcile an imported column with the actual
 * Client GPU cache revision (zero when missing). Skip unloaded, out-of-view
 * and in-flight columns. SUCCESS queues missing light/mesh work or restores
 * first-frame preparation; no generation, saving, or worker waits. */
maage_status maage_world_reconcile_mesh(maage_world *world, int x, int z, unsigned long cached_revision);
int maage_world_chunk_loaded(const maage_world *world, int x, int z);
/* Blocking lifecycle boundary only: cancel obsolete work, finish saves and join
 * the pool. Never call inside the render loop. Failures preserve dirty save data. */
maage_status maage_world_flush(maage_world *world, maage_status_trace *trace);
/* A native join failure retains World and every borrowed dependency for retry.
 * SUCCESS consumes World even if an earlier completed flush reported data errors. */
maage_status maage_world_destroy(maage_world *world);
const maage_block_registry *maage_world_registry(const maage_world *world);
const struct maage_item_registry *maage_world_item_registry(const maage_world *world);
void maage_world_spawn(const maage_world *world, double feet[3]);
/* Missing/loading columns return air for visual queries but are solid barriers
 * for collision, preventing falls/teleports. Only complete columns are visible. */
maage_block_state_id maage_world_get(const maage_world *world, int x, int y, int z);
/* Allocation-free local fluid/body observation using actual numeric surface
 * heights. Unknown columns contribute neither air nor flow. Optional flow sums
 * intersecting unit currents then normalizes once; callers apply the impulse. */
int maage_world_fluid_volume(const maage_world *world, const double minimum[3], const double maximum[3], maage_fluid_id kind, double flow[3]);
/* Matches every requested property; zero matches any fluid. */
int maage_world_fluid_volume_properties(const maage_world *world, const double minimum[3], const double maximum[3], unsigned int properties, double flow[3]);
/* Point immersion uses the registered level spacing as surface tolerance. */
maage_fluid_id maage_world_fluid_at(const maage_world *world, const double point[3]);
int maage_world_solid(const maage_world *world, int x, int y, int z);
/* Allocation-free DDA through selection shapes. Direction is normalized here;
 * range and returned distance are blocks. Missing columns stop traversal with
 * NOT_FOUND at that boundary distance, never permit targeting through unavailable
 * terrain. SUCCESS hits; NOOP misses and retains the requested range as distance. */
maage_status maage_world_raycast(const maage_world *world, const double origin[3], const double direction[3], double range, maage_world_target *target);
maage_status maage_world_emit_sound(maage_world *, unsigned long, const double[3], double intensity, double pitch);
/* Read-only ongoing support evaluation; placement uses the independent
 * CAN_PLACE contract. NOOP defers unavailable dependencies without loading;
 * SUCCESS writes validity. Blocks without a rule have no support requirement. */
maage_status maage_world_can_survive(const maage_world *world, int x, int y, int z, maage_block_state_id state, int *valid);
/* Owner simulation phase only. Compare expected numeric state, then publish a
 * private copy; workers retain their old immutable snapshots. Invalidates
 * lighting/meshes and obsolete jobs without waiting. No filesystem work. */
maage_status maage_world_set(maage_world *world, int x, int y, int z, maage_block_state_id expected, maage_block_state_id replacement);
/* Genuine break replacement; callers retain their cause-specific loot policy. */
maage_status maage_world_break(maage_world *, int, int, int, maage_block_state_id, maage_block_state_id);
/* Balanced owner-only scopes defer notification callbacks until borrowed live
 * simulation storage is released. Mutations remain visible inside the scope;
 * the outer end drains available cascades and publishes immutable edits.
 * End is required even after a failed operation. Scopes may nest; finish them
 * before pumping jobs or persisting World state. */
maage_status maage_world_begin_mutations(maage_world *world);
maage_status maage_world_end_mutations(maage_world *world);
/* Local AABB; unavailable cells are full-cube collision barriers. */
int maage_world_collision_shape(const maage_world *world, int x, int y, int z, maage_block_shape *shape);
/* Transfers worker-prepared upload data; it borrows this mesh's vertices.
 * Destroy/cancel the upload before destroying its source mesh. */
maage_mesh_preparation *maage_world_mesh_take_preparation(maage_world_mesh *mesh);
const maage_mesh_data *maage_world_mesh_data(const maage_world_mesh *mesh);
/* Two-bit material category plus quad winding in one byte per source face.
 * Available for full chunk meshes, not temporary patch meshes. */
const unsigned char *maage_world_mesh_face_flags(const maage_world_mesh *mesh);
/* Release CPU vertices and indices only once the native base upload completes.
 * Filtered index views still work from retained owners and face flags. */
void maage_world_mesh_release_upload_data(maage_world_mesh *mesh);
maage_status maage_world_mesh_destroy(maage_world_mesh *mesh);
#ifdef __cplusplus
}
#endif
#endif
