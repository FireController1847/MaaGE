#include "world_internal.h"
#include "maage/world_store.h"
#include "maage/log.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define MAAGE_CHUNK_BUCKETS 2048
#define MAAGE_JOB_LOAD 1
#define MAAGE_JOB_GENERATE 2
#define MAAGE_JOB_LIGHT 3
#define MAAGE_JOB_MESH 4
#define MAAGE_JOB_SAVE 5

/* Only the World owner changes membership, revisions and reference counts.
 * Workers borrow immutable payloads; references are released AFTER completion.
 * No world lock, worker-to-worker wait, or per-frame barrier is necessary. */
typedef struct maage_chunk_snapshot {
    maage_chunk *chunk;
    unsigned references;
} maage_chunk_snapshot;
typedef struct maage_world_task maage_world_task;
typedef struct maage_world_slot {
    int x, z, stage, dirty, light_dirty, light_ready, mesh_dirty, failed;
    unsigned long incarnation, revision, light_epoch, mesh_revision;
    maage_chunk_snapshot *data;
    maage_world_task *work, *save;
    maage_world_mesh *pending_mesh;
    struct maage_world_slot *next;
} maage_world_slot;
struct maage_world_task {
    int used, kind, x, z, cancel_requested;
    maage_job_id id;
    unsigned long incarnation, revision, light_epoch;
    const maage_block_registry *blocks;
    const maage_biome_registry *biomes;
    maage_world_seed seed;
    maage_world_store *store;
    maage_chunk_snapshot *inputs[5]; /* center, -X,+X,-Z,+Z */
    maage_chunk *output;
    maage_world_mesh *mesh;
    int result;
    unsigned boundaries;
    char error[256];
};
typedef struct maage_chunk_bucket { maage_world_slot *slot; } maage_chunk_bucket;
struct maage_world {
    maage_world_config config;
    maage_jobs *jobs;
    maage_chunk_bucket buckets[MAAGE_CHUNK_BUCKETS];
    maage_world_slot *chunks;
    maage_world_task *tasks;
    maage_job_observation *observations;
    unsigned count, active, task_limit;
    unsigned long clock;
    int center_x, center_z, stopped, failed;
    double yaw, spawn[3];
    maage_world_stats stats;
};
static unsigned long maage_world_revision(maage_world *world)
{
    /* Never wrap into an old incarnation/result token. Exhaustion is explicit. */
    if (world->clock == ~0UL) { world->failed = 1; return 0; }
    return ++world->clock;
}
static maage_chunk_snapshot *maage_world_snapshot(maage_chunk *chunk)
{
    maage_chunk_snapshot *snapshot = (maage_chunk_snapshot *)malloc(sizeof(*snapshot));
    if (snapshot) { snapshot->chunk = chunk; snapshot->references = 1; }
    return snapshot;
}
static void maage_world_release(maage_chunk_snapshot *snapshot)
{
    if (snapshot && !--snapshot->references) { maage_chunk_destroy(snapshot->chunk); free(snapshot); }
}
static size_t maage_world_bucket(int x, int z)
{
    unsigned long hash = (unsigned long)x * 0x8da6b343UL ^ (unsigned long)z * 0xd8163841UL;
    hash ^= hash >> 16;
    return (size_t)(hash & (MAAGE_CHUNK_BUCKETS - 1));
}
/* Integer-keyed open addressing: expected O(1), owner thread only. */
static maage_world_slot *maage_world_find(const maage_world *world, int x, int z)
{
    size_t bucket = maage_world_bucket(x, z), probes;
    maage_world_slot *slot;
    for (probes = 0; probes < MAAGE_CHUNK_BUCKETS; ++probes) {
        slot = world->buckets[bucket].slot;
        if (!slot || (slot->x == x && slot->z == z)) return slot;
        bucket = (bucket + 1) & (MAAGE_CHUNK_BUCKETS - 1);
    }
    return NULL;
}
static maage_world_slot *maage_world_insert(maage_world *world, int x, int z)
{
    maage_world_slot *slot;
    size_t bucket = maage_world_bucket(x, z);
    if (world->count >= MAAGE_WORLD_MAX_CHUNKS) return NULL;
    slot = (maage_world_slot *)calloc(1, sizeof(*slot));
    if (!slot) { world->failed = 1; return NULL; }
    slot->x = x; slot->z = z; slot->stage = MAAGE_JOB_LOAD;
    slot->incarnation = slot->revision = maage_world_revision(world);
    while (world->buckets[bucket].slot) bucket = (bucket + 1) & (MAAGE_CHUNK_BUCKETS - 1);
    world->buckets[bucket].slot = slot;
    slot->next = world->chunks; world->chunks = slot; ++world->count;
    return slot;
}
static void maage_world_remove(maage_world *world, maage_world_slot *slot)
{
    size_t bucket = maage_world_bucket(slot->x, slot->z), destination;
    maage_world_slot *displaced, **link = &world->chunks;
    while (world->buckets[bucket].slot != slot) bucket = (bucket + 1) & (MAAGE_CHUNK_BUCKETS - 1);
    world->buckets[bucket].slot = NULL;
    bucket = (bucket + 1) & (MAAGE_CHUNK_BUCKETS - 1);
    /* Repair probe clusters instead of accumulating tombstones on long trips. */
    while (world->buckets[bucket].slot) {
        displaced = world->buckets[bucket].slot; world->buckets[bucket].slot = NULL;
        destination = maage_world_bucket(displaced->x, displaced->z);
        while (world->buckets[destination].slot) destination = (destination + 1) & (MAAGE_CHUNK_BUCKETS - 1);
        world->buckets[destination].slot = displaced;
        bucket = (bucket + 1) & (MAAGE_CHUNK_BUCKETS - 1);
    }
    while (*link != slot) link = &(*link)->next;
    *link = slot->next; --world->count;
    maage_world_release(slot->data); maage_world_mesh_destroy(slot->pending_mesh); free(slot);
}
static int maage_world_distance(const maage_world *world, const maage_world_slot *slot)
{
    int x = abs(slot->x - world->center_x), z = abs(slot->z - world->center_z);
    return x > z ? x : z;
}
static int maage_world_complete(const maage_world_slot *slot)
{ return slot && slot->data && !slot->failed; }
static void maage_world_neighbors(const maage_world *world, int x, int z, maage_world_slot *neighbors[4])
{
    static const int offsets[4][2] = {{-1,0},{1,0},{0,-1},{0,1}};
    int side;
    for (side = 0; side < 4; ++side) {
        neighbors[side] = maage_world_find(world, x + offsets[side][0], z + offsets[side][1]);
        if (!maage_world_complete(neighbors[side])) neighbors[side] = NULL;
    }
}
static void maage_world_invalidate(maage_world *world, maage_world_slot *slot, int light)
{
    slot->mesh_dirty = 1; slot->revision = maage_world_revision(world);
    maage_world_mesh_destroy(slot->pending_mesh); slot->pending_mesh = NULL;
    if (light) { slot->light_dirty = 1; slot->light_ready = 0; slot->light_epoch = slot->revision; }
}
static void maage_world_invalidate_neighbors(maage_world *world, int x, int z, unsigned mask)
{
    maage_world_slot *neighbors[4];
    int side;
    maage_world_neighbors(world, x, z, neighbors);
    for (side = 0; side < 4; ++side)
        if (neighbors[side] && (mask & (1U << side))) maage_world_invalidate(world, neighbors[side], 1);
}
static void maage_world_failure(maage_world *world, maage_world_slot *slot, const char *error)
{
    world->failed = 1;
    if (slot) slot->failed = 1;
    maage_log_error("maage.world", "%s", error);
}
static maage_chunk *maage_world_copy_chunk(const maage_chunk *source)
{
    maage_chunk *copy = maage_chunk_create(source->x, source->z);
    int section;
    if (!copy) return NULL;
    memcpy(copy->biomes, source->biomes, sizeof(copy->biomes));
    memcpy(copy->heightmap, source->heightmap, sizeof(copy->heightmap));
    for (section = 0; section < 16; ++section) if (source->sections[section]) {
        copy->sections[section] = (maage_section *)malloc(sizeof(maage_section));
        if (!copy->sections[section]) { maage_chunk_destroy(copy); return NULL; }
        memcpy(copy->sections[section], source->sections[section], sizeof(maage_section));
    }
    return copy;
}
static unsigned maage_world_light_boundaries(const maage_chunk *before, const maage_chunk *after)
{
    unsigned changed = 0;
    int side, y, p, x, z;
    for (side = 0; side < 4; ++side) {
        for (y = 0; y < 256 && !(changed & (1U << side)); ++y) for (p = 0; p < 16; ++p) {
            x = side < 2 ? (side ? 15 : 0) : p;
            z = side < 2 ? p : (side == 3 ? 15 : 0);
            if (maage_chunk_light(before, x, y, z) != maage_chunk_light(after, x, y, z)) {
                changed |= 1U << side; break;
            }
        }
    }
    return changed;
}
/* Generation, lighting snapshot copying, propagation, meshing, serialization
 * and filesystem work run without a world/scheduler lock. Owner mutations
 * separately copy a published column before its atomic owner-thread replacement. */
static void maage_world_run(void *context, const maage_job_token *token)
{
    maage_world_task *task = (maage_world_task *)context;
    const maage_chunk *neighbors[4];
    maage_section *added[16];
    int side, section;
    task->result = -1;
    if (maage_job_cancelled(token)) return;
    if (task->kind == MAAGE_JOB_LOAD) {
        task->result = task->store ? maage_world_store_load_chunk(task->store, task->x, task->z,
            task->blocks, task->biomes, &task->output, task->error) : 0;
    } else if (task->kind == MAAGE_JOB_GENERATE) {
        task->output = maage_chunk_create(task->x, task->z);
        if (task->output && maage_generate_chunk_job(task->output, task->seed, task->blocks, task->biomes, token))
            task->result = 1;
        else strcpy(task->error, "Chunk generation failed.");
    } else if (task->kind == MAAGE_JOB_SAVE) {
        task->result = !task->store || maage_world_store_save_chunk(task->store,
            task->inputs[0]->chunk, task->blocks, task->biomes, task->error) ? 1 : -1;
    } else {
        for (side = 0; side < 4; ++side) neighbors[side] = task->inputs[side + 1] ? task->inputs[side + 1]->chunk : NULL;
        if (task->kind == MAAGE_JOB_MESH) {
            task->mesh = maage_world_mesh_create_chunk_job(task->blocks, task->biomes, task->inputs[0]->chunk, neighbors, token);
            if (task->mesh) task->result = 1;
            else strcpy(task->error, "Chunk CPU mesh allocation failed.");
        } else if (task->kind == MAAGE_JOB_LIGHT) {
            task->output = maage_world_copy_chunk(task->inputs[0]->chunk);
            if (!task->output) { strcpy(task->error, "Chunk lighting snapshot allocation failed."); return; }
            task->result = maage_light_chunk_job(task->output, task->blocks, neighbors, added, token);
            for (section = 0; section < 16; ++section)
                if (added[section]) task->output->sections[section] = added[section];
            if (task->result < 0) strcpy(task->error, "Chunk lighting allocation failed.");
            else if (task->result) task->boundaries = maage_world_light_boundaries(task->inputs[0]->chunk, task->output);
        }
    }
}
static void maage_world_task_release(maage_world *world, maage_world_task *task)
{
    int input;
    for (input = 0; input < 5; ++input) maage_world_release(task->inputs[input]);
    maage_chunk_destroy(task->output); maage_world_mesh_destroy(task->mesh);
    memset(task, 0, sizeof(*task)); --world->active;
}
static int maage_world_inputs_current(const maage_world *world, const maage_world_slot *slot, const maage_world_task *task)
{
    maage_world_slot *neighbors[4];
    int side;
    if (slot->data != task->inputs[0]) return 0;
    maage_world_neighbors(world, slot->x, slot->z, neighbors);
    for (side = 0; side < 4; ++side)
        if ((neighbors[side] ? neighbors[side]->data : NULL) != task->inputs[side + 1]) return 0;
    return 1;
}
static int maage_world_publish(maage_world *world, maage_world_slot *slot, maage_world_task *task)
{
    maage_chunk_snapshot *snapshot = maage_world_snapshot(task->output);
    if (!snapshot) { maage_world_failure(world, slot, "Chunk snapshot publication allocation failed."); return 0; }
    maage_world_release(slot->data); slot->data = snapshot; task->output = NULL;
    slot->dirty = 1;
    maage_world_invalidate(world, slot, 0);
    return 1;
}
static void maage_world_collect(maage_world *world)
{
    maage_job_completion completion;
    maage_world_task *task;
    maage_world_slot *slot;
    int stale, obsolete;
    while (maage_jobs_poll(world->jobs, &completion)) {
        task = (maage_world_task *)completion.context;
        slot = maage_world_find(world, task->x, task->z);
        stale = !slot || slot->incarnation != task->incarnation;
        obsolete = slot && maage_world_distance(world, slot) > world->config.render_distance + 2;
        if (!stale) {
            if (task->kind == MAAGE_JOB_SAVE) slot->save = NULL;
            else slot->work = NULL;
        }
        if (!stale && (completion.cancelled || obsolete)) {
            if (task->kind == MAAGE_JOB_LIGHT) slot->light_dirty = 1;
            if (task->kind == MAAGE_JOB_MESH) slot->mesh_dirty = 1;
        }
        if (completion.cancelled) { ++world->stats.cancelled_jobs; }
        else if (stale || (obsolete && task->kind != MAAGE_JOB_SAVE)) { ++world->stats.stale_jobs; }
        else if (task->result < 0) maage_world_failure(world, task->kind == MAAGE_JOB_SAVE ? NULL : slot,
            *task->error ? task->error : "World task failed.");
        else if (task->kind == MAAGE_JOB_SAVE) {
            /* A newer light/block snapshot remains dirty while its old coherent
             * revision finishes saving. Never overlap writes to one file. */
            if (slot->data == task->inputs[0]) slot->dirty = 0;
        } else if (task->kind == MAAGE_JOB_LOAD && !task->result) slot->stage = MAAGE_JOB_GENERATE;
        else if (task->kind == MAAGE_JOB_LOAD || task->kind == MAAGE_JOB_GENERATE) {
            if (maage_world_publish(world, slot, task)) {
                slot->stage = 0; slot->dirty = task->kind == MAAGE_JOB_GENERATE;
                slot->light_dirty = 1; slot->light_epoch = slot->revision;
                if (task->kind == MAAGE_JOB_LOAD) ++world->stats.loaded_from_disk;
                else ++world->stats.generated;
                maage_world_invalidate_neighbors(world, slot->x, slot->z, 15U);
            }
        } else if (!maage_world_inputs_current(world, slot, task) ||
            (task->kind == MAAGE_JOB_LIGHT && slot->light_epoch != task->light_epoch) ||
            (task->kind == MAAGE_JOB_MESH && (slot->revision != task->revision || slot->light_dirty))) {
            ++world->stats.stale_jobs;
            if (task->kind == MAAGE_JOB_LIGHT) slot->light_dirty = 1;
            else slot->mesh_dirty = 1;
        } else if (task->kind == MAAGE_JOB_LIGHT) {
            if (!task->result) slot->light_ready = 1;
            else if (maage_world_publish(world, slot, task)) {
                slot->light_ready = 1;
                maage_world_invalidate_neighbors(world, slot->x, slot->z, task->boundaries);
            }
        } else if (task->kind == MAAGE_JOB_MESH) {
            maage_world_mesh_destroy(slot->pending_mesh); slot->pending_mesh = task->mesh; task->mesh = NULL;
            slot->mesh_revision = task->revision; ++world->stats.meshes;
        }
        ++world->stats.completed_jobs;
        maage_world_task_release(world, task);
    }
}
static int maage_world_submit(maage_world *world, maage_world_slot *slot, int kind, unsigned priority)
{
    maage_world_task *task = NULL;
    maage_world_slot *neighbors[4];
    unsigned index;
    int side;
    if (world->active >= world->task_limit) return 0;
    for (index = 0; index < world->task_limit; ++index) if (!world->tasks[index].used) { task = &world->tasks[index]; break; }
    if (!task) return 0;
    memset(task, 0, sizeof(*task)); task->used = 1; task->kind = kind; task->x = slot->x; task->z = slot->z;
    task->incarnation = slot->incarnation; task->revision = slot->revision; task->light_epoch = slot->light_epoch;
    task->blocks = world->config.registry; task->biomes = world->config.biomes; task->seed = world->config.seed; task->store = world->config.store;
    if (kind >= MAAGE_JOB_LIGHT) {
        task->inputs[0] = slot->data; ++slot->data->references;
        if (kind != MAAGE_JOB_SAVE) {
            maage_world_neighbors(world, slot->x, slot->z, neighbors);
            for (side = 0; side < 4; ++side) if (neighbors[side]) {
                task->inputs[side + 1] = neighbors[side]->data; ++task->inputs[side + 1]->references;
            }
        }
    }
    ++world->active;
    if (!maage_jobs_submit(world->jobs, priority, maage_world_run, task, &task->id)) {
        maage_world_task_release(world, task); return 0;
    }
    if (kind == MAAGE_JOB_SAVE) slot->save = task;
    else slot->work = task;
    if (kind == MAAGE_JOB_LIGHT) slot->light_dirty = 0;
    if (kind == MAAGE_JOB_MESH) slot->mesh_dirty = 0;
    return 1;
}
static void maage_world_cancel(maage_world *world, maage_world_task *task)
{
    if (task && !task->cancel_requested) {
        maage_jobs_cancel(world->jobs, task->id); task->cancel_requested = 1;
    }
}
static void maage_world_retire(maage_world *world)
{
    maage_world_slot *slot, *next;
    int radius = world->config.render_distance + 2;
    for (slot = world->chunks; slot; slot = next) {
        next = slot->next;
        if (maage_world_distance(world, slot) <= radius) continue;
        maage_world_cancel(world, slot->work);
        if (slot->work || slot->save) continue;
        if (slot->data && slot->dirty) {
            /* Failed saves retain dirty data. Global failure stops new loads;
             * shutdown retries each dirty column once before reporting failure. */
            if (!world->failed) maage_world_submit(world, slot, MAAGE_JOB_SAVE, 0);
        } else {
            int x = slot->x, z = slot->z;
            maage_world_remove(world, slot); maage_world_invalidate_neighbors(world, x, z, 15U);
        }
    }
}
/* Bounded desired grid coalesces coordinates. Only a small near-first window
 * enters the scheduler, so distant work cannot fill an unbounded backlog. */
static void maage_world_schedule(maage_world *world)
{
    maage_world_slot *slot, *chosen;
    int radius = world->config.render_distance + 1;
    int dx, dz, x, z, kind, chosen_kind, score, best;
    double forward_x = sin(world->yaw), forward_z = -cos(world->yaw);
    while (!world->failed && world->active < world->task_limit) {
        chosen = NULL; chosen_kind = 0; best = 2147483647; x = z = 0;
        for (dz = -radius; dz <= radius; ++dz) for (dx = -radius; dx <= radius; ++dx) {
            if (abs(world->center_x + dx) > MAAGE_WORLD_LIMIT / 16 - 1 ||
                abs(world->center_z + dz) > MAAGE_WORLD_LIMIT / 16 - 1) continue;
            slot = maage_world_find(world, world->center_x + dx, world->center_z + dz);
            if (slot && (slot->failed || slot->work)) continue;
            kind = 0; score = (dx * dx + dz * dz) * 8;
            if (dx * forward_x + dz * forward_z < 0) score += 3;
            if (!slot) {
                if (world->count < MAAGE_WORLD_MAX_CHUNKS) kind = MAAGE_JOB_LOAD;
            } else if (!slot->data) kind = slot->stage;
            else if (slot->light_dirty) kind = MAAGE_JOB_LIGHT;
            else if (slot->mesh_dirty && abs(dx) <= world->config.render_distance && abs(dz) <= world->config.render_distance) kind = MAAGE_JOB_MESH;
            else if (slot->dirty && !slot->save) kind = MAAGE_JOB_SAVE;
            if (kind == MAAGE_JOB_LOAD || kind == MAAGE_JOB_GENERATE) score += 4;
            else if (kind == MAAGE_JOB_MESH) ++score;
            else if (kind == MAAGE_JOB_SAVE) score += 16;
            if (kind && score < best) { best = score; chosen = slot; chosen_kind = kind; x = world->center_x + dx; z = world->center_z + dz; }
        }
        if (!chosen_kind) break;
        if (!chosen) chosen = maage_world_insert(world, x, z);
        if (!chosen || !maage_world_submit(world, chosen, chosen_kind, (unsigned)best)) break;
    }
}
maage_world *maage_world_create(const maage_world_config *config)
{
    maage_world *world;
    maage_jobs_stats stats;
    unsigned workers;
    int axis;
    if (!config || !maage_block_registry_is_finalized(config->registry) || !config->biomes ||
        !config->biomes->finalized || config->render_distance < 1 || config->render_distance > MAAGE_WORLD_MAX_RADIUS) return NULL;
    world = (maage_world *)calloc(1, sizeof(*world));
    if (!world) return NULL;
    world->config = *config;
    memcpy(world->spawn, config->position, sizeof(world->spawn));
    if (config->new_world) maage_generator_spawn(config->seed, config->biomes, world->spawn);
    for (axis = 0; axis < 3; ++axis)
        if (!(world->spawn[axis] > -MAAGE_WORLD_LIMIT && world->spawn[axis] < MAAGE_WORLD_LIMIT)) { free(world); return NULL; }
    if (!(world->spawn[1] >= 0 && world->spawn[1] < MAAGE_WORLD_LIMIT)) { free(world); return NULL; }
    world->center_x = maage_world_chunk_coordinate((int)floor(world->spawn[0]));
    world->center_z = maage_world_chunk_coordinate((int)floor(world->spawn[2]));
    workers = maage_jobs_resolve_workers(config->worker_count);
    /* Each resident column can own one CPU/load job and one snapshot save.
     * Bound contexts by that actual workload, independently of pool size. */
    world->task_limit = workers >= MAAGE_WORLD_MAX_CHUNKS ?
        MAAGE_WORLD_MAX_CHUNKS * 2U : workers * 2U;
    if ((size_t)world->task_limit > (size_t)-1 / sizeof(*world->tasks) ||
        (size_t)world->task_limit > (size_t)-1 / sizeof(*world->observations)) { free(world); return NULL; }
    world->tasks = (maage_world_task *)calloc(world->task_limit, sizeof(*world->tasks));
    world->observations = (maage_job_observation *)malloc(world->task_limit * sizeof(*world->observations));
    if (!world->tasks || !world->observations) { free(world->observations); free(world->tasks); free(world); return NULL; }
    world->jobs = maage_jobs_create(workers, world->task_limit);
    if (!world->jobs) { free(world->observations); free(world->tasks); free(world); return NULL; }
    maage_jobs_snapshot(world->jobs, &stats);
    maage_log_info("maage.world.jobs", "Detected logical CPUs=%u%s; World workers=%u; in-flight limit=%u; immutable snapshots, owner-thread commits.",
        stats.logical_processors, stats.logical_processors ? "" : " (unknown)", stats.workers, world->task_limit);
    maage_world_schedule(world);
    return world;
}
void maage_world_stream(maage_world *world, double x, double z, double yaw)
{
    maage_world_slot *slot;
    int cx, cz, old_x, old_z, radius;
    if (!world || world->stopped || !(x > -MAAGE_WORLD_LIMIT && x < MAAGE_WORLD_LIMIT && z > -MAAGE_WORLD_LIMIT && z < MAAGE_WORLD_LIMIT)) return;
    cx = maage_world_chunk_coordinate((int)floor(x)); cz = maage_world_chunk_coordinate((int)floor(z));
    old_x = world->center_x; old_z = world->center_z; radius = world->config.render_distance;
    world->center_x = cx; world->center_z = cz; world->yaw = yaw;
    maage_world_collect(world);
    if (cx != old_x || cz != old_z) {
        for (slot = world->chunks; slot; slot = slot->next)
            if (maage_world_distance(world, slot) <= radius &&
                (abs(slot->x - old_x) > radius || abs(slot->z - old_z) > radius)) maage_world_invalidate(world, slot, 0);
    }
    maage_world_retire(world);
    maage_world_schedule(world);
}
int maage_world_failed(const maage_world *world) { return world && world->failed; }
void maage_world_get_stats(maage_world *world, maage_world_stats *stats)
{
    maage_world_slot *slot;
    const maage_world_task *task;
    maage_jobs_stats jobs;
    unsigned index, count;
    unsigned *queued, *running;
    int x, z, section, radius;
    if (!world || !stats) return;
    *stats = world->stats; stats->failed = world->failed;
    for (slot = world->chunks; slot; slot = slot->next) {
        if (slot->dirty) ++stats->dirty_columns;
        if (maage_world_complete(slot)) {
            ++stats->loaded;
            for (section = 0; section < 16; ++section)
                if (slot->data->chunk->sections[section]) ++stats->loaded_sections;
        }
        if (maage_world_complete(slot) && !slot->light_dirty && !slot->mesh_dirty && !slot->work) ++stats->ready;
    }
    radius = world->config.render_distance + 1;
    for (z = -radius; z <= radius; ++z) for (x = -radius; x <= radius; ++x) {
        if (abs(world->center_x + x) > MAAGE_WORLD_LIMIT / 16 - 1 ||
            abs(world->center_z + z) > MAAGE_WORLD_LIMIT / 16 - 1) continue;
        if (!maage_world_complete(maage_world_find(world, world->center_x + x, world->center_z + z))) ++stats->pending;
    }
    count = maage_jobs_observe(world->jobs, &jobs, world->observations, world->task_limit);
    for (index = 0; index < count; ++index) {
        /* Only immutable owner-assigned kind is read, never worker outputs.
         * Completion polling/release cannot run concurrently with this owner. */
        task = (const maage_world_task *)world->observations[index].context;
        if (task->kind == MAAGE_JOB_GENERATE) {
            ++stats->generation_jobs; queued = &stats->generation_queued; running = &stats->generation_running;
        } else if (task->kind == MAAGE_JOB_MESH) {
            ++stats->meshing_jobs; queued = &stats->meshing_queued; running = &stats->meshing_running;
        } else if (task->kind == MAAGE_JOB_LIGHT) {
            ++stats->lighting_jobs; queued = &stats->lighting_queued; running = &stats->lighting_running;
        } else {
            ++stats->io_jobs; queued = &stats->io_queued; running = &stats->io_running;
        }
        if (world->observations[index].state == MAAGE_JOB_STATE_QUEUED) ++*queued;
        else if (world->observations[index].state == MAAGE_JOB_STATE_RUNNING) ++*running;
    }
    stats->logical_processors = jobs.logical_processors; stats->worker_count = jobs.workers;
    stats->queued_jobs = jobs.queued; stats->running_jobs = jobs.running;
    stats->scheduling_latency_ms = jobs.mean_queue_seconds * 1000.0;
}
int maage_world_observe(const maage_world *world, int x, int y, int z,
    maage_world_observation *observation)
{
    maage_world_slot *slot;
    const maage_chunk *chunk;
    int local_x, local_z, section;
    if (!observation) return 0;
    memset(observation, 0, sizeof(*observation));
    if (!world || y < 0 || y >= MAAGE_WORLD_HEIGHT || x <= -MAAGE_WORLD_LIMIT ||
        x >= MAAGE_WORLD_LIMIT || z <= -MAAGE_WORLD_LIMIT || z >= MAAGE_WORLD_LIMIT) return 0;
    slot = maage_world_find(world, maage_world_chunk_coordinate(x), maage_world_chunk_coordinate(z));
    if (!maage_world_complete(slot)) return 0;
    chunk = slot->data->chunk;
    local_x = maage_world_local_coordinate(x); local_z = maage_world_local_coordinate(z);
    observation->biome_id = chunk->biomes[local_z * 16 + local_x];
    for (section = 0; section < 16; ++section)
        if (chunk->sections[section]) ++observation->section_count;
    observation->light_available = slot->light_ready && !slot->light_dirty &&
        !(slot->work && slot->work->kind == MAAGE_JOB_LIGHT);
    if (observation->light_available) observation->light = maage_chunk_light(chunk, local_x, y, local_z);
    return 1;
}
int maage_world_mesh_current(const maage_world *world, int x, int z, unsigned long revision)
{
    maage_world_slot *slot;
    if (!world) return 0;
    slot = maage_world_find(world, x, z);
    return maage_world_complete(slot) && slot->revision == revision && !slot->light_dirty && !slot->mesh_dirty &&
        maage_world_distance(world, slot) <= world->config.render_distance;
}
int maage_world_take_mesh(maage_world *world, maage_world_mesh_update *update)
{
    maage_world_slot *slot, *chosen = NULL;
    int best = 2147483647, distance;
    if (!world || !update) return 0;
    for (slot = world->chunks; slot; slot = slot->next) {
        distance = maage_world_distance(world, slot);
        if (slot->pending_mesh && distance <= world->config.render_distance && distance < best &&
            maage_world_mesh_current(world, slot->x, slot->z, slot->mesh_revision)) { chosen = slot; best = distance; }
    }
    if (chosen) {
        update->x = chosen->x; update->z = chosen->z; update->revision = chosen->mesh_revision;
        update->mesh = chosen->pending_mesh; chosen->pending_mesh = NULL;
    }
    return chosen != NULL;
}
int maage_world_chunk_loaded(const maage_world *world, int x, int z)
{ return world && maage_world_complete(maage_world_find(world, x, z)); }
maage_block_state_id maage_world_get(const maage_world *world, int x, int y, int z)
{
    maage_world_slot *slot;
    if (!world || y < 0 || y >= MAAGE_WORLD_HEIGHT || x <= -MAAGE_WORLD_LIMIT || x >= MAAGE_WORLD_LIMIT || z <= -MAAGE_WORLD_LIMIT || z >= MAAGE_WORLD_LIMIT) return MAAGE_STATE_AIR;
    slot = maage_world_find(world, maage_world_chunk_coordinate(x), maage_world_chunk_coordinate(z));
    return maage_world_complete(slot) ? maage_chunk_get(slot->data->chunk, maage_world_local_coordinate(x), y, maage_world_local_coordinate(z)) : MAAGE_STATE_AIR;
}
int maage_world_solid(const maage_world *world, int x, int y, int z)
{
    maage_world_slot *slot;
    const maage_block_state_definition *state;
    const maage_block_definition *block;
    if (!world || y < 0 || x <= -MAAGE_WORLD_LIMIT || x >= MAAGE_WORLD_LIMIT || z <= -MAAGE_WORLD_LIMIT || z >= MAAGE_WORLD_LIMIT) return 1;
    slot = maage_world_find(world, maage_world_chunk_coordinate(x), maage_world_chunk_coordinate(z));
    if (!maage_world_complete(slot)) return 1;
    state = maage_block_registry_state(world->config.registry, maage_chunk_get(slot->data->chunk, maage_world_local_coordinate(x), y, maage_world_local_coordinate(z)));
    block = state ? maage_block_registry_block(world->config.registry, state->block_id) : NULL;
    return !block || block->solid;
}
int maage_world_collision_shape(const maage_world *world, int x, int y, int z,
    maage_block_shape *shape)
{
    const maage_block_state_definition *state;
    const maage_block_definition *block;
    int axis;
    if (!shape) return 0;
    for (axis = 0; axis < 3; ++axis) {
        shape->minimum[axis] = 0.0; shape->maximum[axis] = 1.0;
    }
    if (!world || y < 0 || x <= -MAAGE_WORLD_LIMIT || x >= MAAGE_WORLD_LIMIT ||
        z <= -MAAGE_WORLD_LIMIT || z >= MAAGE_WORLD_LIMIT) return 1;
    if (!maage_world_chunk_loaded(world, maage_world_chunk_coordinate(x),
        maage_world_chunk_coordinate(z))) return 1;
    if (y >= MAAGE_WORLD_HEIGHT) return 0;
    state = maage_block_registry_state(world->config.registry, maage_world_get(world, x, y, z));
    block = state ? maage_block_registry_block(world->config.registry, state->block_id) : NULL;
    if (!block) return 1;
    if (!block->solid) return 0;
    *shape = block->collision_shape;
    return 1;
}
maage_world_mutation_result maage_world_set(maage_world *world,
    int x, int y, int z, maage_block_state_id expected,
    maage_block_state_id replacement)
{
    maage_world_slot *slot;
    maage_chunk *copy;
    maage_chunk_snapshot *snapshot;
    int local_x, local_z, top;
    if (!world) return MAAGE_WORLD_MUTATION_INVALID;
    if (world->stopped || world->failed || y < 0 || y >= MAAGE_WORLD_HEIGHT ||
        x <= -MAAGE_WORLD_LIMIT || x >= MAAGE_WORLD_LIMIT ||
        z <= -MAAGE_WORLD_LIMIT || z >= MAAGE_WORLD_LIMIT ||
        !maage_block_registry_state(world->config.registry, replacement) ||
        !maage_block_registry_state(world->config.registry, expected)) {
        ++world->stats.rejected_mutations; return MAAGE_WORLD_MUTATION_INVALID;
    }
    slot = maage_world_find(world, maage_world_chunk_coordinate(x), maage_world_chunk_coordinate(z));
    if (!maage_world_complete(slot)) {
        ++world->stats.rejected_mutations; return MAAGE_WORLD_MUTATION_UNAVAILABLE;
    }
    if (world->clock > ~0UL - 5UL) {
        world->failed = 1; ++world->stats.rejected_mutations;
        return MAAGE_WORLD_MUTATION_INVALID;
    }
    local_x = maage_world_local_coordinate(x); local_z = maage_world_local_coordinate(z);
    if (maage_chunk_get(slot->data->chunk, local_x, y, local_z) != expected) {
        ++world->stats.rejected_mutations; return MAAGE_WORLD_MUTATION_STALE;
    }
    if (expected == replacement) return MAAGE_WORLD_MUTATION_OK;
    copy = maage_world_copy_chunk(slot->data->chunk);
    if (!copy || !maage_chunk_set(copy, local_x, y, local_z, replacement)) {
        maage_chunk_destroy(copy); ++world->stats.rejected_mutations;
        return MAAGE_WORLD_MUTATION_OUT_OF_MEMORY;
    }
    snapshot = maage_world_snapshot(copy);
    if (!snapshot) {
        maage_chunk_destroy(copy); ++world->stats.rejected_mutations;
        return MAAGE_WORLD_MUTATION_OUT_OF_MEMORY;
    }
    top = MAAGE_WORLD_HEIGHT;
    while (top > 0 && maage_chunk_get(copy, local_x, top - 1, local_z) == MAAGE_STATE_AIR) --top;
    copy->heightmap[local_z * MAAGE_CHUNK_EDGE + local_x] = (unsigned short)top;
    maage_world_release(slot->data); slot->data = snapshot; slot->dirty = 1;
    maage_world_invalidate(world, slot, 1);
    /* A light change can eventually reach any horizontal column boundary;
     * immutable neighbor pointers also reject already submitted old meshes. */
    maage_world_invalidate_neighbors(world, slot->x, slot->z, 15U);
    maage_world_cancel(world, slot->work);
    ++world->stats.mutations;
    return MAAGE_WORLD_MUTATION_OK;
}
const maage_block_registry *maage_world_registry(const maage_world *world) { return world ? world->config.registry : NULL; }
void maage_world_spawn(const maage_world *world, double feet[3])
{ if (world && feet) memcpy(feet, world->spawn, sizeof(world->spawn)); }
int maage_world_flush(maage_world *world)
{
    maage_world_slot *slot;
    unsigned index;
    if (!world || world->stopped) return !world || !world->failed;
    /* Lifecycle boundary, outside the render loop: finish saves, discard CPU
     * work and drain its pinned snapshots before closing the store/registries. */
    for (index = 0; index < world->task_limit; ++index)
        if (world->tasks[index].used && world->tasks[index].kind != MAAGE_JOB_SAVE) maage_world_cancel(world, &world->tasks[index]);
    while (world->active) { maage_jobs_wait(world->jobs); maage_world_collect(world); }
    for (slot = world->chunks; slot; slot = slot->next) slot->stage = 0;
    for (;;) {
        for (slot = world->chunks; slot; slot = slot->next)
            if (slot->dirty && slot->data && !slot->save && slot->stage != MAAGE_JOB_SAVE) {
                if (!maage_world_submit(world, slot, MAAGE_JOB_SAVE, 0)) break;
                /* One final attempt per snapshot. Failed data stays dirty but
                 * an unavailable device must not cause a shutdown retry loop. */
                slot->stage = MAAGE_JOB_SAVE;
            }
        if (!world->active) break;
        maage_jobs_wait(world->jobs); maage_world_collect(world);
    }
    maage_jobs_stop(world->jobs); world->stopped = 1;
    return !world->failed;
}
void maage_world_destroy(maage_world *world)
{
    maage_world_slot *slot, *next;
    if (!world) return;
    maage_world_flush(world); maage_jobs_destroy(world->jobs, NULL);
    for (slot = world->chunks; slot; slot = next) {
        next = slot->next; maage_world_release(slot->data); maage_world_mesh_destroy(slot->pending_mesh); free(slot);
    }
    free(world->observations); free(world->tasks); free(world);
}
