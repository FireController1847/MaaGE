#include "world_internal.h"
#include "maage/entities.h"
#include "maage/world_store.h"
#include "maage/log.h"
#include "maage/time.h"
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

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
    unsigned long incarnation, revision, light_epoch, mesh_revision, data_epoch;
    unsigned long mesh_failed_revision;
    maage_chunk_snapshot *data;
    maage_world_task *work, *save;
    maage_world_mesh *pending_mesh;
    maage_chunk *edit;
    int edit_blocks, update_linked;
    struct maage_world_slot *edit_next, *update_next, *update_previous;
    struct maage_world_slot *next, *previous;
} maage_world_slot;
struct maage_world_task {
    int used, kind, x, z, cancel_requested;
    maage_job_id id;
    unsigned long incarnation, revision, light_epoch;
    maage_leaves_mode leaves_mode;
    const maage_block_registry *blocks;
    const maage_biome_registry *biomes;
    maage_world_seed seed;
    maage_generation_policy generation_policy;
    maage_world_store *store;
    maage_chunk_snapshot *inputs[9]; /* center, cardinal and diagonal mesh halo */
    unsigned long input_epochs[9]; /* geometry/light identity, independent of queues */
    maage_chunk *output;
    maage_world_mesh *mesh;
    maage_status status;
    unsigned boundaries;
    unsigned long tick_low, tick_high;
    int initialized_environment;
    char error[256];
};
typedef struct maage_chunk_bucket { maage_world_slot *slot; } maage_chunk_bucket;
struct maage_world {
    maage_world_config config;
    maage_jobs *jobs;
    maage_chunk_bucket *buckets;
    size_t bucket_count;
    maage_world_slot *chunks, *save_cursor;
    maage_world_task *tasks;
    maage_job_observation *observations;
    unsigned *free_tasks;
    unsigned count, cache_limit, active, saves_active, free_count, task_limit;
    unsigned scan_step;
    int scan_ring, save_turn;
    unsigned long clock, rate_generated;
    double rate_time, generation_rate;
    int center_x, center_z, stopped, failed;
    maage_leaves_mode leaves_mode;
    double yaw, spawn[3];
    maage_world_stats stats;
    maage_status reported_failures[5]; /* One retained identity per task kind. */
    maage_status_trace trace, last_job_trace;
    maage_environment *environment;
    maage_entities *entities;
    maage_world_slot *edits, *update_head, *update_cursor;
    unsigned edit_depth, update_columns;
};
static void maage_world_update_membership(maage_world *world, maage_world_slot *slot);
static void maage_world_commit_edits(maage_world *world);
static maage_status maage_world_cancel(maage_world *world, maage_world_task *task);
static void maage_world_failure(maage_world *world, maage_world_slot *slot, const char *error);
static void maage_world_failure_status(maage_world *world, maage_world_slot *slot,
    maage_status result, const char *error);
static unsigned long maage_world_revision(maage_world *world)
{
    /* Never wrap into an old incarnation/result token. Exhaustion is explicit. */
    if (world->clock == ~0UL) {
        maage_world_failure_status(world, NULL, maage_status_make(MAAGE_STATUS_INVARIANT_VIOLATION,
            MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_INITIALIZE, 0), "World revision identity exhausted.");
        return 0;
    }
    return ++world->clock;
}
static maage_status maage_world_snapshot(maage_chunk *chunk, maage_chunk_snapshot **output)
{
    maage_chunk_snapshot *snapshot;
    *output = NULL;
    snapshot = (maage_chunk_snapshot *)malloc(sizeof(*snapshot));
    if (!snapshot) return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_SNAPSHOT, 0);
    snapshot->chunk = chunk; snapshot->references = 1; *output = snapshot;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_SNAPSHOT, 0);
}
static void maage_world_release(maage_chunk_snapshot *snapshot)
{
    if (snapshot && !--snapshot->references) { maage_chunk_destroy(snapshot->chunk); free(snapshot); }
}
static size_t maage_world_bucket(int x, int z, size_t count)
{
    unsigned long hash = (unsigned long)x * 0x8da6b343UL ^ (unsigned long)z * 0xd8163841UL;
    hash ^= hash >> 16;
    return (size_t)hash & (count - 1);
}
/* Open-addressed owner-only map. Rehash before occupancy reaches one half;
 * both allocation and bucket count grow with actual resident membership. */
static int maage_world_grow_buckets(maage_world *world)
{
    maage_chunk_bucket *buckets;
    maage_world_slot *slot;
    size_t count, bucket;
    if (world->bucket_count && (size_t)world->count < world->bucket_count / 2) return 1;
    if (world->bucket_count > (size_t)-1 / 2) return 0;
    count = world->bucket_count ? world->bucket_count * 2 : 16;
    if (count > (size_t)-1 / sizeof(*buckets)) return 0;
    buckets = (maage_chunk_bucket *)calloc(count, sizeof(*buckets));
    if (!buckets) return 0;
    for (slot = world->chunks; slot; slot = slot->next) {
        bucket = maage_world_bucket(slot->x, slot->z, count);
        while (buckets[bucket].slot) bucket = (bucket + 1) & (count - 1);
        buckets[bucket].slot = slot;
    }
    free(world->buckets);
    world->buckets = buckets; world->bucket_count = count;
    return 1;
}
static maage_world_slot *maage_world_find(const maage_world *world, int x, int z)
{
    size_t bucket, probes;
    maage_world_slot *slot;
    if (!world->bucket_count) return NULL;
    bucket = maage_world_bucket(x, z, world->bucket_count);
    for (probes = 0; probes < world->bucket_count; ++probes) {
        slot = world->buckets[bucket].slot;
        if (!slot || (slot->x == x && slot->z == z)) return slot;
        bucket = (bucket + 1) & (world->bucket_count - 1);
    }
    return NULL;
}
static maage_world_slot *maage_world_insert(maage_world *world, int x, int z)
{
    maage_world_slot *slot;
    size_t bucket;
    if (world->count >= world->cache_limit) return NULL;
    if (!maage_world_grow_buckets(world)) {
        maage_world_failure(world, NULL, "World column index allocation failed."); return NULL;
    }
    slot = (maage_world_slot *)calloc(1, sizeof(*slot));
    if (!slot) { maage_world_failure(world, NULL, "World column allocation failed."); return NULL; }
    slot->x = x; slot->z = z; slot->stage = MAAGE_JOB_LOAD;
    slot->incarnation = slot->revision = maage_world_revision(world);
    bucket = maage_world_bucket(x, z, world->bucket_count);
    while (world->buckets[bucket].slot) bucket = (bucket + 1) & (world->bucket_count - 1);
    world->buckets[bucket].slot = slot;
    slot->next = world->chunks;
    if (world->chunks) world->chunks->previous = slot;
    world->chunks = slot; ++world->count;
    return slot;
}
static void maage_world_remove(maage_world *world, maage_world_slot *slot)
{
    size_t bucket = maage_world_bucket(slot->x, slot->z, world->bucket_count), destination;
    maage_world_slot *displaced;
    if (slot->update_linked) {
        if (slot->update_previous) slot->update_previous->update_next = slot->update_next;
        else world->update_head = slot->update_next;
        if (slot->update_next) slot->update_next->update_previous = slot->update_previous;
        if (world->update_cursor == slot) world->update_cursor = slot->update_next ? slot->update_next : world->update_head;
        --world->update_columns;
    }
    while (world->buckets[bucket].slot != slot) bucket = (bucket + 1) & (world->bucket_count - 1);
    world->buckets[bucket].slot = NULL;
    bucket = (bucket + 1) & (world->bucket_count - 1);
    while (world->buckets[bucket].slot) {
        displaced = world->buckets[bucket].slot; world->buckets[bucket].slot = NULL;
        destination = maage_world_bucket(displaced->x, displaced->z, world->bucket_count);
        while (world->buckets[destination].slot) destination = (destination + 1) & (world->bucket_count - 1);
        world->buckets[destination].slot = displaced;
        bucket = (bucket + 1) & (world->bucket_count - 1);
    }
    if (slot->previous) slot->previous->next = slot->next;
    else world->chunks = slot->next;
    if (slot->next) slot->next->previous = slot->previous;
    --world->count;
    if (world->save_cursor == slot)
        world->save_cursor = slot->next ? slot->next : world->chunks;
    maage_world_release(slot->data); maage_world_mesh_destroy(slot->pending_mesh); free(slot);
}
static int maage_world_distance(const maage_world *world, const maage_world_slot *slot)
{
    int x = abs(slot->x - world->center_x), z = abs(slot->z - world->center_z);
    return x > z ? x : z;
}
static int maage_world_complete(const maage_world_slot *slot)
{ return slot && slot->data && !slot->failed; }
static void maage_world_neighbors(const maage_world *world, int x, int z, maage_world_slot *neighbors[8])
{
    static const int offsets[8][2] = {{-1,0},{1,0},{0,-1},{0,1},
        {-1,-1},{1,-1},{-1,1},{1,1}};
    int side;
    for (side = 0; side < 8; ++side) {
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
    maage_world_slot *neighbors[8];
    int side;
    maage_world_neighbors(world, x, z, neighbors);
    for (side = 0; side < 8; ++side) {
        unsigned affected = side < 4 ? 1U << side :
            ((1U << ((side - 4) & 1)) | (1U << (side < 6 ? 2 : 3)));
        if (neighbors[side] && (mask & affected))
            maage_world_invalidate(world, neighbors[side], side < 4);
    }
}
static void maage_world_failure_status(maage_world *world, maage_world_slot *slot,
    maage_status result, const char *error)
{
    world->failed = 1;
    if (slot) slot->failed = 1;
    if (world->stats.last_result != result) {
        world->stats.last_result = result; ++world->stats.failure_reports;
        maage_status_trace_init(&world->trace);
        MAAGE_STATUS_TRACE(&world->trace, result, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_MUTATE);
        maage_log_trace_status(MAAGE_LOG_FATAL, "maage.world", result, &world->trace, "%s", error);
    }
}
static void maage_world_failure(maage_world *world, maage_world_slot *slot, const char *error)
{
    maage_world_failure_status(world, slot, maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED,
        MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_INITIALIZE, 0), error);
}
static maage_status maage_world_copy_chunk(const maage_chunk *source, maage_chunk **output)
{
    maage_chunk *copy = NULL;
    maage_status result;
    int section;
    *output = NULL;
    result = maage_chunk_create(source->x, source->z, &copy);
    if (maage_status_failed(result)) return result;
    memcpy(copy->biomes, source->biomes, sizeof(copy->biomes));
    memcpy(copy->heightmap, source->heightmap, sizeof(copy->heightmap));
    result = maage_chunk_copy_environment(copy, source);
    if (maage_status_failed(result)) { maage_chunk_destroy(copy); return result; }
    for (section = 0; section < 16; ++section) if (source->sections[section]) {
        copy->sections[section] = (maage_section *)malloc(sizeof(maage_section));
        if (!copy->sections[section]) {
            maage_chunk_destroy(copy);
            return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_COPY, 0);
        }
        memcpy(copy->sections[section], source->sections[section], sizeof(maage_section));
    }
    *output = copy;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_COPY, 0);
}
/* One private edit per touched column in a simulation phase. Workers retain
 * published snapshots; queue and fluid changes publish together at its end. */
static maage_status maage_world_edit(maage_world *world, maage_world_slot *slot, maage_chunk **output)
{
    maage_status result;
    *output = slot->edit;
    if (slot->edit) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_SNAPSHOT, 0);
    result = maage_world_copy_chunk(slot->data->chunk, &slot->edit);
    if (maage_status_failed(result)) {
        maage_world_failure_status(world, slot, result, "World edit snapshot allocation failed."); return result;
    }
    slot->edit_next = world->edits; world->edits = slot; *output = slot->edit;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_SNAPSHOT, 0);
}
static void maage_world_update_membership(maage_world *world, maage_world_slot *slot)
{
    const maage_chunk *chunk = slot->edit ? slot->edit : slot->data->chunk;
    if (chunk->update_count && !slot->update_linked) {
        slot->update_next = world->update_head; slot->update_previous = NULL;
        if (world->update_head) world->update_head->update_previous = slot;
        world->update_head = slot; slot->update_linked = 1; ++world->update_columns;
        if (!world->update_cursor) world->update_cursor = slot;
    } else if (!chunk->update_count && slot->update_linked) {
        if (slot->update_previous) slot->update_previous->update_next = slot->update_next;
        else world->update_head = slot->update_next;
        if (slot->update_next) slot->update_next->update_previous = slot->update_previous;
        if (world->update_cursor == slot) world->update_cursor = slot->update_next ? slot->update_next : world->update_head;
        slot->update_next = slot->update_previous = NULL;
        slot->update_linked = 0; --world->update_columns;
    }
}
static void maage_world_commit_edits(maage_world *world)
{
    maage_world_slot *slot;
    maage_chunk_snapshot *snapshot;
    while (world->edits) {
        slot = world->edits; world->edits = slot->edit_next; slot->edit_next = NULL;
        if (maage_status_failed(maage_world_snapshot(slot->edit, &snapshot))) {
            maage_chunk_destroy(slot->edit); slot->edit = NULL;
            maage_world_failure(world, slot, "Environment snapshot publication allocation failed.");
            maage_world_update_membership(world, slot);
            continue;
        }
        maage_world_release(slot->data); slot->data = snapshot; slot->edit = NULL;
        slot->dirty = 1;
        if (slot->edit_blocks) {
            maage_world_invalidate(world, slot, 1);
            slot->data_epoch = slot->revision;
            maage_world_invalidate_neighbors(world, slot->x, slot->z, 15U);
            maage_world_cancel(world, slot->work); slot->work = NULL;
            world->scan_ring = 0; world->scan_step = 0;
        }
        slot->edit_blocks = 0;
        maage_world_update_membership(world, slot);
    }
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
    const maage_chunk *neighbors[8];
    maage_section *added[16];
    int side, section;
    maage_status_trace *trace = maage_job_trace(token);
    task->status = maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_WORLD,
        task->kind == MAAGE_JOB_GENERATE ? MAAGE_OPERATION_GENERATE :
        (task->kind == MAAGE_JOB_LIGHT ? MAAGE_OPERATION_LIGHT : MAAGE_OPERATION_MESH), 0);
    if (maage_job_cancelled(token)) {
        task->status = maage_status_make(MAAGE_STATUS_INTERRUPTED, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_JOB, MAAGE_DETAIL_CANCELLED);
        goto finished;
    }
    if (task->kind == MAAGE_JOB_LOAD) {
        task->status = task->store ? maage_world_store_load_chunk(task->store, task->x, task->z,
            task->blocks, task->biomes, &task->output, task->error, trace) :
            maage_status_make(MAAGE_STATUS_NOT_FOUND, MAAGE_CONTEXT_WORLD_IO, MAAGE_OPERATION_LOAD, 0);
    } else if (task->kind == MAAGE_JOB_GENERATE) {
        task->status = maage_chunk_create(task->x, task->z, &task->output);
        if (!maage_status_failed(task->status)) task->status = maage_generate_chunk_job(task->output,
            task->seed, task->blocks, task->biomes, &task->generation_policy, token, trace);
        if (maage_status_failed(task->status)) strcpy(task->error, "Chunk generation failed.");
    } else if (task->kind == MAAGE_JOB_SAVE) {
        task->status = task->store ? maage_world_store_save_chunk_at(task->store,
            task->inputs[0]->chunk, task->blocks, task->biomes, task->tick_low, task->tick_high, task->error, trace) :
            maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_SAVE, 0);
    } else {
        for (side = 0; side < 8; ++side) neighbors[side] = task->inputs[side + 1] ? task->inputs[side + 1]->chunk : NULL;
        if (task->kind == MAAGE_JOB_MESH) {
            task->status = maage_world_mesh_create_chunk_job(task->blocks, task->biomes,
                task->inputs[0]->chunk, neighbors, task->leaves_mode, token, &task->mesh, trace);
            if (maage_status_failed(task->status)) strcpy(task->error, "Chunk CPU mesh construction failed.");
        } else if (task->kind == MAAGE_JOB_LIGHT) {
            task->status = maage_world_copy_chunk(task->inputs[0]->chunk, &task->output);
            if (maage_status_failed(task->status)) { strcpy(task->error, "Chunk lighting snapshot allocation failed."); goto finished; }
            task->status = maage_light_chunk_job(task->output, task->blocks, neighbors, added, token, trace);
            for (section = 0; section < 16; ++section)
                if (added[section]) task->output->sections[section] = added[section];
            if (maage_status_failed(task->status)) strcpy(task->error, "Chunk lighting failed.");
            else if (maage_status_code(task->status) == MAAGE_STATUS_SUCCESS)
                task->boundaries = maage_world_light_boundaries(task->inputs[0]->chunk, task->output);
        }
    }
    if ((task->kind == MAAGE_JOB_LOAD || task->kind == MAAGE_JOB_GENERATE) &&
        !maage_status_failed(task->status) && task->output &&
        (!task->output->environment_initialized || task->output->updates_relative)) {
        task->status = maage_chunk_environment_initialize(task->output, task->blocks, task->tick_low, task->tick_high);
        if (maage_status_failed(task->status)) strcpy(task->error, "Initial block update initialization failed.");
        else task->initialized_environment = 1;
    }
finished:
    if (!maage_status_is_success(task->status))
        MAAGE_STATUS_TRACE(trace, task->status, MAAGE_CONTEXT_WORLD,
            task->kind == MAAGE_JOB_GENERATE ? MAAGE_OPERATION_GENERATE :
            (task->kind == MAAGE_JOB_LIGHT ? MAAGE_OPERATION_LIGHT :
            (task->kind == MAAGE_JOB_MESH ? MAAGE_OPERATION_MESH :
            (task->kind == MAAGE_JOB_SAVE ? MAAGE_OPERATION_SAVE : MAAGE_OPERATION_LOAD))));
    maage_job_set_result(token, task->status);
}
static void maage_world_task_release(maage_world *world, maage_world_task *task)
{
    int input;
    for (input = 0; input < 9; ++input) maage_world_release(task->inputs[input]);
    maage_chunk_destroy(task->output); maage_world_mesh_destroy(task->mesh);
    if (task->kind == MAAGE_JOB_SAVE) --world->saves_active;
    world->free_tasks[world->free_count++] = (unsigned)(task - world->tasks);
    memset(task, 0, sizeof(*task)); --world->active;
}
static int maage_world_inputs_current(const maage_world *world, const maage_world_slot *slot, const maage_world_task *task)
{
    maage_world_slot *neighbors[8];
    int side;
    if (slot->data_epoch != task->input_epochs[0]) return 0;
    maage_world_neighbors(world, slot->x, slot->z, neighbors);
    for (side = 0; side < (task->kind == MAAGE_JOB_MESH ? 8 : 4); ++side)
        if ((neighbors[side] ? neighbors[side]->data_epoch : 0) != task->input_epochs[side + 1]) return 0;
    return 1;
}
static maage_status maage_world_publish(maage_world *world, maage_world_slot *slot, maage_world_task *task)
{
    maage_chunk_snapshot *snapshot;
    maage_status result = maage_world_snapshot(task->output, &snapshot);
    if (maage_status_failed(result)) {
        maage_world_failure_status(world, slot, result, "Chunk snapshot publication allocation failed."); return result;
    }
    maage_world_release(slot->data); slot->data = snapshot; task->output = NULL;
    slot->dirty = 1;
    maage_world_invalidate(world, slot, 0);
    slot->data_epoch = slot->revision;
    maage_world_update_membership(world, slot);
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_UPDATE, 0);
}
static maage_status maage_world_collect(maage_world *world)
{
    maage_job_completion completion;
    maage_world_task *task;
    maage_world_slot *slot;
    int stale, obsolete, owns;
    maage_status result, disposition, outcome = maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_POLL, 0);
    unsigned long failures = world->stats.failure_reports;
    while (1) {
        result = maage_jobs_poll(world->jobs, &completion);
        if (maage_status_failed(result)) {
            maage_world_failure_status(world, NULL, result, "World completion poll failed."); return result;
        }
        if (maage_status_code(result) != MAAGE_STATUS_SUCCESS) break;
        outcome = result;
        task = (maage_world_task *)completion.context;
        task->status = completion.operation_result;
        disposition = completion.result;
        slot = maage_world_find(world, task->x, task->z);
        stale = !slot || slot->incarnation != task->incarnation;
        obsolete = slot && maage_world_distance(world, slot) > world->config.render_distance + 2;
        owns = !stale && (task->kind == MAAGE_JOB_SAVE ?
            slot->save == task : slot->work == task);
        if (owns) {
            if (task->kind == MAAGE_JOB_SAVE) slot->save = NULL;
            else slot->work = NULL;
        }
        if (owns && (completion.cancelled || obsolete)) {
            if (task->kind == MAAGE_JOB_LIGHT) slot->light_dirty = 1;
            if (task->kind == MAAGE_JOB_MESH) slot->mesh_dirty = 1;
        }
        if (completion.cancelled) { ++world->stats.cancelled_jobs; }
        else if (stale || (obsolete && task->kind != MAAGE_JOB_SAVE)) {
            ++world->stats.stale_jobs;
            disposition = maage_status_make(MAAGE_STATUS_INTERRUPTED, MAAGE_CONTEXT_WORLD,
                MAAGE_OPERATION_JOB, MAAGE_DETAIL_STALE);
        } else if ((task->kind == MAAGE_JOB_LIGHT || task->kind == MAAGE_JOB_MESH) &&
            (!maage_world_inputs_current(world, slot, task) ||
            (task->kind == MAAGE_JOB_LIGHT && slot->light_epoch != task->light_epoch) ||
            (task->kind == MAAGE_JOB_MESH && (slot->revision != task->revision || slot->light_dirty)))) {
            ++world->stats.stale_jobs;
            disposition = maage_status_make(MAAGE_STATUS_INTERRUPTED, MAAGE_CONTEXT_WORLD,
                MAAGE_OPERATION_JOB, MAAGE_DETAIL_STALE);
            if (task->kind == MAAGE_JOB_LIGHT) slot->light_dirty = 1;
            else slot->mesh_dirty = 1;
        } else if (task->kind == MAAGE_JOB_LOAD && maage_status_code(task->status) == MAAGE_STATUS_NOT_FOUND &&
            maage_status_context(task->status) == MAAGE_CONTEXT_WORLD_IO &&
            maage_status_operation(task->status) == MAAGE_OPERATION_LOAD) {
            slot->stage = MAAGE_JOB_GENERATE;
        } else if (maage_status_failed(task->status)) {
            int optional = task->kind == MAAGE_JOB_MESH &&
                (maage_status_code(task->status) == MAAGE_STATUS_ALLOCATION_FAILED ||
                 maage_status_code(task->status) == MAAGE_STATUS_BUFFER_OVERFLOW);
            world->stats.last_result = task->status;
            world->trace = completion.trace;
            MAAGE_STATUS_TRACE(&world->trace, task->status, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_UPDATE);
            if (world->reported_failures[task->kind - 1] != task->status) {
                world->reported_failures[task->kind - 1] = task->status;
                ++world->stats.failure_reports;
                maage_log_trace_status(optional ? MAAGE_LOG_WARNING : MAAGE_LOG_FATAL,
                    "maage.world.jobs", task->status, &world->trace, "Job %lu; column %d,%d: %s%s",
                    task->id, task->x, task->z, *task->error ? task->error : "World task failed.",
                    optional ? " Retaining the previous visual; retry after column invalidation." : " World stopped safely.");
            }
            if (optional) {
                slot->mesh_dirty = 1; slot->mesh_failed_revision = task->revision;
            } else {
                world->failed = 1;
                if (task->kind != MAAGE_JOB_SAVE) slot->failed = 1;
            }
        }
        else if (task->kind == MAAGE_JOB_SAVE) {
            /* A newer light/block snapshot remains dirty while its old coherent
             * revision finishes saving. Never overlap writes to one file. */
            if (slot->data == task->inputs[0]) slot->dirty = 0;
        } else if (task->kind == MAAGE_JOB_LOAD || task->kind == MAAGE_JOB_GENERATE) {
            if (!maage_status_failed(maage_world_publish(world, slot, task))) {
                slot->stage = 0; slot->dirty = task->kind == MAAGE_JOB_GENERATE || task->initialized_environment;
                slot->light_dirty = 1; slot->light_epoch = slot->revision;
                if (task->kind == MAAGE_JOB_LOAD) ++world->stats.loaded_from_disk;
                else {
                    ++world->stats.generated;
                    if (slot->data->chunk->generation_stages & (1U << MAAGE_GENERATION_STAGE_VEGETATION))
                        ++world->stats.decorated_columns;
                    world->stats.structure_fragments += slot->data->chunk->generation_features[MAAGE_GENERATION_STAGE_STRUCTURES];
                }
                maage_world_invalidate_neighbors(world, slot->x, slot->z, 15U);
            }
        } else if (task->kind == MAAGE_JOB_LIGHT) {
            if (maage_status_code(task->status) == MAAGE_STATUS_NOOP) slot->light_ready = 1;
            else if (slot->data != task->inputs[0] &&
                maage_status_failed(maage_chunk_replace_environment(task->output, slot->data->chunk)))
                maage_world_failure(world, slot, "Lighting result auxiliary snapshot allocation failed.");
            else if (!maage_status_failed(maage_world_publish(world, slot, task))) {
                slot->light_ready = 1;
                maage_world_invalidate_neighbors(world, slot->x, slot->z, task->boundaries);
            }
        } else if (task->kind == MAAGE_JOB_MESH) {
            maage_world_mesh_destroy(slot->pending_mesh); slot->pending_mesh = task->mesh; task->mesh = NULL;
            slot->mesh_revision = task->revision; ++world->stats.meshes;
        }
        ++world->stats.completed_jobs;
        world->stats.last_job_result = disposition;
        world->stats.last_job_operation_result = completion.operation_result;
        world->last_job_trace = completion.trace;
        if (disposition != completion.operation_result)
            MAAGE_STATUS_TRACE(&world->last_job_trace, disposition, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_POLL);
        maage_world_task_release(world, task);
    }
    if (world->failed) return world->stats.last_result;
    if (world->stats.failure_reports != failures)
        return maage_status_make(MAAGE_STATUS_PARTIAL, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_POLL, 0);
    return outcome;
}
static maage_status maage_world_submit(maage_world *world, maage_world_slot *slot, int kind, unsigned priority)
{
    maage_world_task *task = NULL;
    maage_world_slot *neighbors[8];
    unsigned index;
    int side;
    maage_environment_state environment;
    maage_status_trace parent;
    maage_status result;
    if (!world->free_count) { ++world->stats.queue_saturation_events; return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_SUBMIT, 0); }
    index = world->free_tasks[--world->free_count]; task = &world->tasks[index];
    memset(task, 0, sizeof(*task)); task->used = 1; task->kind = kind; task->x = slot->x; task->z = slot->z;
    task->incarnation = slot->incarnation; task->revision = slot->revision; task->light_epoch = slot->light_epoch;
    task->blocks = world->config.registry; task->biomes = world->config.biomes; task->seed = world->config.seed; task->store = world->config.store;
    task->generation_policy = world->config.generation_policy;
    task->leaves_mode = world->leaves_mode;
    maage_environment_snapshot(world->environment, &environment);
    task->tick_low = environment.ticks_low; task->tick_high = environment.ticks_high;
    if (kind >= MAAGE_JOB_LIGHT) {
        task->inputs[0] = slot->data; ++slot->data->references;
        task->input_epochs[0] = slot->data_epoch;
        if (kind != MAAGE_JOB_SAVE) {
            maage_world_neighbors(world, slot->x, slot->z, neighbors);
            for (side = 0; side < (kind == MAAGE_JOB_MESH ? 8 : 4); ++side) if (neighbors[side]) {
                task->inputs[side + 1] = neighbors[side]->data; ++task->inputs[side + 1]->references;
                task->input_epochs[side + 1] = neighbors[side]->data_epoch;
            }
        }
    }
    ++world->active;
    if (kind == MAAGE_JOB_SAVE) ++world->saves_active;
    maage_status_trace_init(&parent);
    MAAGE_STATUS_TRACE(&parent, maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_UPDATE, 0), MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_UPDATE);
    result = maage_jobs_submit(world->jobs, priority, maage_world_run, task, &task->id, &parent);
    if (maage_status_code(result) != MAAGE_STATUS_SUCCESS) {
        ++world->stats.queue_saturation_events;
        maage_world_task_release(world, task);
        world->trace = parent;
        return MAAGE_STATUS_PROPAGATE(&world->trace, result, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_SUBMIT);
    }
    if (kind == MAAGE_JOB_SAVE) slot->save = task;
    else slot->work = task;
    if (kind == MAAGE_JOB_LIGHT) slot->light_dirty = 0;
    if (kind == MAAGE_JOB_MESH) slot->mesh_dirty = 0;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_SUBMIT, 0);
}
static maage_status maage_world_cancel(maage_world *world, maage_world_task *task)
{
    maage_status result;
    if (!task || task->cancel_requested) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_CANCEL, 0);
    result = maage_jobs_cancel(world->jobs, task->id);
    if (!maage_status_failed(result)) task->cancel_requested = 1;
    return MAAGE_STATUS_PROPAGATE(&world->trace, result, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_CANCEL);
}
static void maage_world_retire(maage_world *world)
{
    maage_world_slot *slot, *next;
    int radius = world->config.render_distance + 2;
    for (slot = world->chunks; slot; slot = next) {
        int x, z;
        next = slot->next;
        if (maage_world_distance(world, slot) <= radius) continue;
        maage_world_cancel(world, slot->work);
        /* A cancelled worker borrows snapshots, not the membership slot.
         * Remove clean columns immediately; old results fail incarnation
         * validation when their completion eventually reaches the owner. */
        if ((slot->data && slot->dirty) || slot->save) continue;
        x = slot->x; z = slot->z;
        maage_world_remove(world, slot);
        maage_world_invalidate_neighbors(world, x, z, 15U);
    }
}
static int maage_world_make_room(maage_world *world)
{
    maage_world_slot *slot, *victim = NULL;
    int distance, farthest = world->config.render_distance;
    int x, z;
    if (world->count < world->cache_limit) return 1;
    for (slot = world->chunks; slot; slot = slot->next) {
        if (slot->dirty || slot->save) continue;
        distance = maage_world_distance(world, slot);
        if (distance > farthest) { farthest = distance; victim = slot; }
    }
    if (!victim) { ++world->stats.cache_pressure_events; return 0; }
    maage_world_cancel(world, victim->work);
    x = victim->x; z = victim->z;
    maage_world_remove(world, victim);
    maage_world_invalidate_neighbors(world, x, z, 15U);
    return 1;
}
/* Walk concentric square rings incrementally. A single pump examines at
 * most a workload-derived budget, never rescanning the entire grid per job.
 * On movement the cursor restarts at the nearest column. */
static void maage_world_scan_next(maage_world *world, int *dx, int *dz)
{
    int ring = world->scan_ring;
    unsigned step, span;
    if (!ring) {
        *dx = *dz = 0;
        world->scan_ring = 1; world->scan_step = 0;
        return;
    }
    step = world->scan_step++;
    span = (unsigned)ring * 2U + 1U;
    if (step < span) {
        *dx = (int)step - ring; *dz = -ring;
    } else if (step < span + (unsigned)ring * 2U) {
        *dx = ring; *dz = (int)(step - span) - ring + 1;
    } else if (step < span + (unsigned)ring * 4U) {
        *dx = ring - 1 - (int)(step - span - (unsigned)ring * 2U); *dz = ring;
    } else {
        *dx = -ring;
        *dz = ring - 1 - (int)(step - span - (unsigned)ring * 4U);
    }
    if (world->scan_step == (unsigned)ring * 8U) {
        ++world->scan_ring; world->scan_step = 0;
    }
}
static maage_status maage_world_schedule_saves(maage_world *world, unsigned reserve)
{
    maage_world_slot *slot, *first, *next;
    maage_status result = maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_SUBMIT, 0);
    if (world->failed || !world->chunks || world->active >= world->task_limit ||
        world->saves_active >= reserve) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_SUBMIT, 0);
    first = world->save_cursor ? world->save_cursor : world->chunks;
    slot = first;
    do {
        next = slot->next ? slot->next : world->chunks;
        if (slot->dirty && slot->data && !slot->save && !slot->failed) {
            result = maage_world_submit(world, slot, MAAGE_JOB_SAVE, 0);
            if (maage_status_failed(result)) {
                maage_world_failure_status(world, slot, result, "World save submission failed."); return result;
            }
            if (maage_status_code(result) != MAAGE_STATUS_SUCCESS) break;
        }
        slot = next;
    } while (slot != first && world->active < world->task_limit &&
        world->saves_active < reserve);
    /* One pass and a persistent cursor: dirty columns eventually receive a
     * writer even during continuous nearby streaming and insertions. */
    world->save_cursor = slot;
    return result;
}
static maage_status maage_world_schedule(maage_world *world)
{
    maage_world_slot *slot;
    int radius = world->config.render_distance + 1;
    int dx, dz, x, z, kind, ring, no_room = 0;
    size_t span = (size_t)radius * 2U + 1U;
    unsigned limit, reserve, work_limit, budget, examined;
    unsigned priority;
    double forward_x, forward_z;
    maage_status result;
    if (world->failed) return world->stats.last_result;
    reserve = world->task_limit > 1 ? world->task_limit / 8U + (world->task_limit % 8U != 0) : 0;
    work_limit = world->task_limit - reserve;
    if (world->active >= world->task_limit ||
        world->active - world->saves_active >= work_limit)
        ++world->stats.queue_saturation_events;
    /* A single context cannot reserve a separate save slot. Alternate which
     * class is admitted first when constrained to that explicit capacity. */
    if (world->task_limit == 1 && !world->active) {
        world->save_turn = !world->save_turn;
        if (world->save_turn) {
            result = maage_world_schedule_saves(world, 1);
            if (maage_status_failed(result)) return result;
        }
    }
    limit = (unsigned)(span * span); /* validated at creation */
    budget = world->task_limit > UINT_MAX / 16U ? UINT_MAX : world->task_limit * 16U;
    if (budget < 64U) budget = 64U;
    if (budget > limit) budget = limit;
    forward_x = sin(world->yaw); forward_z = -cos(world->yaw);
    for (examined = 0; examined < budget &&
        world->active - world->saves_active < work_limit &&
        world->active < world->task_limit; ++examined) {
        if (world->scan_ring > radius) {
            world->scan_ring = 0; world->scan_step = 0;
        }
        maage_world_scan_next(world, &dx, &dz);
        x = world->center_x + dx; z = world->center_z + dz;
        if (abs(x) > MAAGE_WORLD_LIMIT / MAAGE_CHUNK_EDGE - 1 ||
            abs(z) > MAAGE_WORLD_LIMIT / MAAGE_CHUNK_EDGE - 1) continue;
        slot = maage_world_find(world, x, z);
        if (slot && (slot->failed || slot->work)) continue;
        kind = 0;
        if (!slot) {
            /* Cache pressure must not stop work on columns already resident. */
            if (no_room) continue;
            if (!maage_world_make_room(world)) { no_room = 1; continue; }
            kind = MAAGE_JOB_LOAD;
        } else if (!slot->data) kind = slot->stage;
        else if (slot->light_dirty) kind = MAAGE_JOB_LIGHT;
        else if (slot->mesh_dirty && slot->mesh_failed_revision != slot->revision &&
            abs(dx) <= world->config.render_distance &&
            abs(dz) <= world->config.render_distance) kind = MAAGE_JOB_MESH;
        if (!kind) continue;
        if (!slot) slot = maage_world_insert(world, x, z);
        if (!slot) break;
        ring = abs(dx) > abs(dz) ? abs(dx) : abs(dz);
        priority = (unsigned)ring * 16U;
        if (dx * forward_x + dz * forward_z < 0) priority += 3U;
        if (kind == MAAGE_JOB_LOAD || kind == MAAGE_JOB_GENERATE) priority += 4U;
        else if (kind == MAAGE_JOB_MESH) ++priority;
        result = maage_world_submit(world, slot, kind, priority);
        if (maage_status_failed(result)) {
            maage_world_failure_status(world, slot, result, "World task submission failed."); return result;
        }
        if (maage_status_code(result) != MAAGE_STATUS_SUCCESS) break;
    }
    /* Saves are accepted independently of the near-first work quota. A
     * reserved share prevents an endless stream of arrivals from starving
     * durable dirty data or filling all contexts with low-value I/O. */
    result = maage_world_schedule_saves(world, reserve ? reserve : 1U);
    return world->failed ? world->stats.last_result : result;
}
maage_status maage_world_create(const maage_world_config *config, maage_world **output,
    maage_status_trace *trace)
{
    maage_world *world;
    maage_jobs_stats stats;
    unsigned workers, index, cache_limit, job_limit;
    size_t span, halo, footprint, extra, automatic_jobs;
    int axis;
    maage_generation_policy policy;
    maage_status result;
    if (output) *output = NULL;
    if (!config || !output) return MAAGE_STATUS_TRACE(trace,
        maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_CREATE, config ? 1 : 0),
        MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_CREATE);
    policy = config->generation_policy;
    if (!policy.sea_level && !policy.spawn_search_radius && !policy.spawn_search_step &&
        maage_status_failed(maage_generation_policy_default(&policy)))
        return MAAGE_STATUS_TRACE(trace, maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT,
            MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_CREATE, 0), MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_CREATE);
    if (!maage_generation_policy_valid(&policy)) return MAAGE_STATUS_TRACE(trace,
        maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_CREATE, 0),
        MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_CREATE);
    if (!maage_block_registry_is_finalized(config->registry) || !config->biomes ||
        !config->biomes->finalized || config->render_distance < 1 ||
        config->render_distance > MAAGE_WORLD_MAX_RADIUS) return MAAGE_STATUS_TRACE(trace,
            maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_CREATE, 0),
            MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_CREATE);
    span = (size_t)config->render_distance * 2U + 3U;
    if (span > (size_t)UINT_MAX / span) goto overflow;
    footprint = span * span;
    if (config->cache_capacity) cache_limit = config->cache_capacity;
    else {
        halo = span + 2U;
        if (halo > (size_t)UINT_MAX / halo) goto overflow;
        extra = halo * halo;
        if (footprint > (size_t)UINT_MAX - extra) goto overflow;
        cache_limit = (unsigned)(footprint + extra);
    }
    workers = maage_jobs_resolve_workers(config->worker_count);
    if ((size_t)workers > (size_t)-1 / 4U ||
        (size_t)workers * 4U > (size_t)-1 - span * 2U) goto overflow;
    automatic_jobs = (size_t)workers * 4U + span * 2U;
    if (automatic_jobs > cache_limit) automatic_jobs = cache_limit;
    job_limit = config->job_capacity ? config->job_capacity : (unsigned)automatic_jobs;
    if (!job_limit || (size_t)job_limit > (size_t)-1 / sizeof(maage_world_task) ||
        (size_t)job_limit > (size_t)-1 / sizeof(maage_job_observation) ||
        (size_t)job_limit > (size_t)-1 / sizeof(unsigned)) goto overflow;
    world = (maage_world *)calloc(1, sizeof(*world));
    if (!world) goto allocation_failed;
    result = maage_entities_create(&world->entities);
    if (maage_status_failed(result)) { maage_entities_destroy(world->entities); free(world); return result; }
    world->config = *config; world->config.generation_policy = policy;
    world->leaves_mode = MAAGE_LEAVES_FANCY;
    world->cache_limit = cache_limit; world->task_limit = job_limit;
    memcpy(world->spawn, config->position, sizeof(world->spawn));
    if (config->new_world &&
        maage_status_failed(maage_generation_spawn(config->seed, config->biomes, &policy,
            world->spawn))) { maage_entities_destroy(world->entities); free(world); goto invalid_spawn; }
    for (axis = 0; axis < 3; ++axis)
        if (!(world->spawn[axis] > -MAAGE_WORLD_LIMIT && world->spawn[axis] < MAAGE_WORLD_LIMIT)) { maage_entities_destroy(world->entities); free(world); goto invalid_spawn; }
    if (!(world->spawn[1] >= 0 && world->spawn[1] < MAAGE_WORLD_LIMIT)) { maage_entities_destroy(world->entities); free(world); goto invalid_spawn; }
    world->center_x = maage_world_chunk_coordinate((int)floor(world->spawn[0]));
    world->center_z = maage_world_chunk_coordinate((int)floor(world->spawn[2]));
    world->tasks = (maage_world_task *)calloc(job_limit, sizeof(*world->tasks));
    world->observations = (maage_job_observation *)malloc((size_t)job_limit * sizeof(*world->observations));
    world->free_tasks = (unsigned *)malloc((size_t)job_limit * sizeof(*world->free_tasks));
    if (!world->tasks || !world->observations || !world->free_tasks) {
        free(world->free_tasks); free(world->observations); free(world->tasks); maage_entities_destroy(world->entities); free(world); goto allocation_failed;
    }
    for (index = 0; index < job_limit; ++index) world->free_tasks[index] = index;
    world->free_count = job_limit;
    result = maage_jobs_create(workers, job_limit, &world->jobs, trace);
    if (maage_status_failed(result)) {
        free(world->free_tasks); free(world->observations); free(world->tasks); maage_entities_destroy(world->entities); free(world);
        return MAAGE_STATUS_TRACE(trace, result, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_CREATE);
    }
    result = maage_environment_create(config->seed, config->initial_environment, &world->environment);
    if (maage_status_failed(result)) {
        maage_jobs_destroy(world->jobs, NULL);
        free(world->free_tasks); free(world->observations); free(world->tasks); maage_entities_destroy(world->entities); free(world);
        return MAAGE_STATUS_TRACE(trace, result, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_CREATE);
    }
    world->config.initial_environment = NULL;
    world->rate_time = maage_time_monotonic_seconds();
    maage_jobs_snapshot(world->jobs, &stats);
    maage_log_info("maage.world.jobs", "Logical CPUs=%u%s; workers=%u; job capacity=%u; cache capacity=%u; immutable commits.",
        stats.logical_processors, stats.logical_processors ? "" : " (unknown)",
        stats.workers, job_limit, cache_limit);
    result = maage_world_schedule(world);
    *output = world;
    if (maage_status_failed(result)) {
        if (trace) *trace = world->trace;
        return result;
    }
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_CREATE, 0);
overflow:
    return MAAGE_STATUS_TRACE(trace, maage_status_make(MAAGE_STATUS_BUFFER_OVERFLOW,
        MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_CREATE, 0), MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_CREATE);
allocation_failed:
    return MAAGE_STATUS_TRACE(trace, maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED,
        MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_CREATE, 0), MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_CREATE);
invalid_spawn:
    return MAAGE_STATUS_TRACE(trace, maage_status_make(MAAGE_STATUS_INVALID_STATE,
        MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_CREATE, 0), MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_CREATE);
}
maage_status maage_world_set_leaves_mode(maage_world *world, maage_leaves_mode mode)
{
    maage_world_slot *slot;
    if (!world || world->stopped || world->failed || mode < MAAGE_LEAVES_FAST || mode > MAAGE_LEAVES_FANCY)
        return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_SET, 0);
    if (world->leaves_mode == mode)
        return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_SET, 0);
    world->leaves_mode = mode;
    for (slot = world->chunks; slot; slot = slot->next) {
        if (!maage_world_complete(slot)) continue;
        maage_world_invalidate(world, slot, 0);
        /* Pending mesh jobs hold the old mode by value. Cancel without waiting;
         * stale completions cannot publish across the new mesh revision. */
        if (slot->work && slot->work->kind == MAAGE_JOB_MESH)
            maage_world_cancel(world, slot->work);
    }
    world->scan_ring = 0; world->scan_step = 0;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_SET, 0);
}
maage_status maage_world_stream(maage_world *world, double x, double z, double yaw, maage_status_trace *trace)
{
    maage_world_slot *slot;
    int cx, cz, old_x, old_z, radius;
    unsigned long failures;
    if (!world || world->stopped || !(x > -MAAGE_WORLD_LIMIT && x < MAAGE_WORLD_LIMIT && z > -MAAGE_WORLD_LIMIT && z < MAAGE_WORLD_LIMIT)) return MAAGE_STATUS_TRACE(trace, maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_UPDATE, 0), MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_UPDATE);
    failures = world->stats.failure_reports;
    cx = maage_world_chunk_coordinate((int)floor(x)); cz = maage_world_chunk_coordinate((int)floor(z));
    old_x = world->center_x; old_z = world->center_z; radius = world->config.render_distance;
    world->center_x = cx; world->center_z = cz; world->yaw = yaw;
    maage_world_collect(world);
    if (cx != old_x || cz != old_z) { world->scan_ring = 0; world->scan_step = 0; }
    if (cx != old_x || cz != old_z) {
        for (slot = world->chunks; slot; slot = slot->next)
            if (maage_world_distance(world, slot) <= radius &&
                (abs(slot->x - old_x) > radius || abs(slot->z - old_z) > radius)) maage_world_invalidate(world, slot, 0);
    }
    maage_world_retire(world);
    maage_world_schedule(world);
    if (world->failed || world->stats.failure_reports != failures) {
        if (trace) *trace = world->trace;
        return world->failed ? world->stats.last_result : maage_status_make(MAAGE_STATUS_PARTIAL,
            MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_UPDATE, 0);
    }
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_UPDATE, 0);
}
int maage_world_failed(const maage_world *world) { return world && world->failed; }
maage_status maage_world_status(const maage_world *world)
{
    return world ? world->stats.last_result : maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT,
        MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_INITIALIZE, 0);
}
const maage_status_trace *maage_world_trace(const maage_world *world)
{ return world ? &world->trace : NULL; }
const maage_status_trace *maage_world_last_job_trace(const maage_world *world)
{ return world ? &world->last_job_trace : NULL; }
maage_status maage_world_tick(maage_world *world)
{
    maage_status result;
    if (!world) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_TICK, 0);
    if (world->failed) return world->stats.last_result;
    if (world->stopped) return maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_TICK, 0);
    ++world->edit_depth;
    result = maage_environment_tick(world->environment, world, 128U);
    if (!--world->edit_depth) maage_world_commit_edits(world);
    return world->failed ? world->stats.last_result : result;
}
void maage_world_environment(const maage_world *world, maage_environment_state *state)
{
    if (world && state) maage_environment_snapshot(world->environment, state);
}
void maage_world_get_stats(maage_world *world, maage_world_stats *stats)
{
    maage_world_slot *slot;
    const maage_world_task *task;
    maage_jobs_stats jobs;
    unsigned index, count;
    unsigned *queued, *running;
    int section, radius, lower_x, upper_x, lower_z, upper_z, bound;
    size_t pending;
    double now;
    if (!world || !stats) return;
    *stats = world->stats; stats->failed = world->failed;
    stats->resident_columns = world->count;
    stats->cache_capacity = world->cache_limit; stats->job_capacity = world->task_limit;
    radius = world->config.render_distance + 1;
    bound = MAAGE_WORLD_LIMIT / MAAGE_CHUNK_EDGE - 1;
    lower_x = world->center_x - radius; upper_x = world->center_x + radius;
    lower_z = world->center_z - radius; upper_z = world->center_z + radius;
    if (lower_x < -bound) lower_x = -bound;
    if (lower_z < -bound) lower_z = -bound;
    if (upper_x > bound) upper_x = bound;
    if (upper_z > bound) upper_z = bound;
    pending = (size_t)(upper_x - lower_x + 1) * (size_t)(upper_z - lower_z + 1);
    for (slot = world->chunks; slot; slot = slot->next) {
        if (slot->dirty) ++stats->dirty_columns;
        if (maage_world_complete(slot)) {
            ++stats->loaded;
            stats->pending_block_updates += slot->data->chunk->update_count;
            stats->pending_fluid_updates += slot->data->chunk->fluid_update_count;
            stats->block_entities += slot->data->chunk->entity_count;
            for (section = 0; section < 16; ++section)
                if (slot->data->chunk->sections[section] && stats->loaded_sections < UINT_MAX)
                    ++stats->loaded_sections;
            if (slot->x >= lower_x && slot->x <= upper_x &&
                slot->z >= lower_z && slot->z <= upper_z) --pending;
        }
        if (maage_world_complete(slot) && !slot->light_dirty && !slot->mesh_dirty && !slot->work) ++stats->ready;
    }
    stats->pending = (unsigned)pending;
    count = 0;
    maage_jobs_observe(world->jobs, &jobs, world->observations, world->task_limit, &count);
    for (index = 0; index < count; ++index) {
        /* Only immutable owner-assigned kind is read, never worker outputs. */
        task = (const maage_world_task *)world->observations[index].context;
        if (task->kind == MAAGE_JOB_GENERATE) {
            ++stats->generation_jobs; queued = &stats->generation_queued; running = &stats->generation_running;
            if (world->observations[index].stage > 0 && world->observations[index].stage <= 8)
                ++stats->generation_stage_jobs[world->observations[index].stage - 1];
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
    stats->completed_uncommitted_jobs = jobs.ready;
    stats->scheduling_latency_ms = jobs.mean_queue_seconds * 1000.0;
    now = maage_time_monotonic_seconds();
    if (now >= world->rate_time + 0.5) {
        world->generation_rate = world->stats.generated >= world->rate_generated ?
            (double)(world->stats.generated - world->rate_generated) / (now - world->rate_time) : 0.0;
        world->rate_generated = world->stats.generated; world->rate_time = now;
    }
    stats->generated_columns_per_second = world->generation_rate;
}
int maage_world_observe(const maage_world *world, int x, int y, int z,
    maage_world_observation *observation)
{
    maage_world_slot *slot;
    const maage_chunk *chunk;
    const maage_biome_definition *biome;
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
    biome = maage_biome_registry_get(world->config.biomes, observation->biome_id);
    if (biome) {
        observation->temperature = biome->temperature - (y > 64 ? (y - 64) / 600.0 : 0.0);
        observation->rainfall = biome->rainfall;
    }
    observation->sky_exposed = y >= chunk->heightmap[local_z * 16 + local_x];
    observation->precipitation_height = chunk->heightmap[local_z * 16 + local_x];
    for (section = 0; section < 16; ++section)
        if (chunk->sections[section]) ++observation->section_count;
    observation->light_available = slot->light_ready && !slot->light_dirty &&
        !(slot->work && slot->work->kind == MAAGE_JOB_LIGHT);
    if (observation->light_available) observation->light = maage_chunk_light(chunk, local_x, y, local_z);
    return 1;
}
unsigned long maage_world_column_revision(const maage_world *world, int x, int z)
{
    maage_world_slot *slot = world ? maage_world_find(world, x, z) : NULL;
    return maage_world_complete(slot) ? slot->revision : 0;
}
maage_status maage_world_sample_camera(const maage_world *world,
    int x, int y, int z, maage_world_camera_sample *sample)
{
    unsigned long revisions[4];
    int dx, dy, dz, i, changed;
    if (!world || !sample || x < -MAAGE_WORLD_LIMIT+5 || x > MAAGE_WORLD_LIMIT-5 ||
        z < -MAAGE_WORLD_LIMIT+5 || z > MAAGE_WORLD_LIMIT-5 ||
        y < -MAAGE_WORLD_LIMIT+5 || y > MAAGE_WORLD_LIMIT-5)
        return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_SNAPSHOT, 0);
    changed = !sample->valid || sample->x != x || sample->y != y || sample->z != z;
    for (i = 0; i < 4; ++i) {
        revisions[i] = maage_world_column_revision(world,
            maage_world_chunk_coordinate(x + (i%2 ? 5 : -5)),
            maage_world_chunk_coordinate(z + (i/2 ? 5 : -5)));
        if (revisions[i] != sample->revisions[i]) changed = 1;
    }
    if (!changed) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_SNAPSHOT, 0);
    memset(sample, 0, sizeof(*sample));
    sample->valid = 1; sample->x = x; sample->y = y; sample->z = z;
    memcpy(sample->revisions, revisions, sizeof(revisions));
    for (dy = -5; dy <= 5; ++dy) for (dz = -5; dz <= 5; ++dz) for (dx = -5; dx <= 5; ++dx) {
        i = dx+5 + 11*(dz+5) + 121*(dy+5);
        if (y+dy < 0) sample->states[i] = MAAGE_STATE_BEDROCK;
        else if (y+dy < MAAGE_WORLD_HEIGHT)
            sample->states[i] = maage_world_get(world, x+dx, y+dy, z+dz);
    }
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_SNAPSHOT, 0);
}

maage_status maage_world_sample_neighborhood(const maage_world *world,
    int x, int y, int z, maage_world_neighborhood *sample)
{
    maage_world_observation observation;
    unsigned long revisions[9];
    int dx, dy, dz, i, changed;
    if (!world || !sample || y < 0 || y >= MAAGE_WORLD_HEIGHT ||
        x <= -MAAGE_WORLD_LIMIT || x >= MAAGE_WORLD_LIMIT || z <= -MAAGE_WORLD_LIMIT || z >= MAAGE_WORLD_LIMIT)
        return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_SNAPSHOT, 0);
    changed = !sample->valid || sample->x != x || sample->y != y || sample->z != z;
    for (dz = -1; dz <= 1; ++dz) for (dx = -1; dx <= 1; ++dx) {
        i = dx + 1 + 3 * (dz + 1);
        revisions[i] = maage_world_column_revision(world,
            maage_world_chunk_coordinate(x + dx), maage_world_chunk_coordinate(z + dz));
        if (!revisions[i] || revisions[i] != sample->revisions[i]) changed = 1;
    }
    if (!changed) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_SNAPSHOT, 0);
    memset(sample, 0, sizeof(*sample)); sample->x = x; sample->y = y; sample->z = z;
    memcpy(sample->revisions, revisions, sizeof(revisions)); sample->valid = 1;
    for (dy = -1; dy <= 1; ++dy) for (dz = -1; dz <= 1; ++dz) for (dx = -1; dx <= 1; ++dx) {
        i = dx + 1 + 3 * (dz + 1) + 9 * (dy + 1);
        if (y + dy < 0) { sample->states[i] = MAAGE_STATE_BEDROCK; continue; }
        if (y + dy >= MAAGE_WORLD_HEIGHT) { sample->light[i] = 240; continue; }
        if (!maage_world_observe(world, x + dx, y + dy, z + dz, &observation) || !observation.light_available) sample->valid = 0;
        sample->states[i] = maage_world_get(world, x + dx, y + dy, z + dz);
        sample->light[i] = observation.light;
    }
    for (dz = -2; dz <= 2; ++dz) for (dx = -2; dx <= 2; ++dx) {
        if (!maage_world_observe(world,x+dx,y,z+dz,&observation))
            (void)maage_world_observe(world,x,y,z,&observation);
        sample->biomes[dx+2+5*(dz+2)] = observation.biome_id;
    }
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_SNAPSHOT, 0);
}

int maage_world_mesh_current(const maage_world *world, int x, int z, unsigned long revision)
{
    maage_world_slot *slot;
    if (!world) return 0;
    slot = maage_world_find(world, x, z);
    return maage_world_complete(slot) && slot->revision == revision && !slot->light_dirty && !slot->mesh_dirty &&
        maage_world_distance(world, slot) <= world->config.render_distance;
}
maage_status maage_world_take_mesh(maage_world *world, maage_world_mesh_update *update)
{
    maage_world_slot *slot, *chosen = NULL;
    int best = 2147483647, distance;
    if (update) memset(update, 0, sizeof(*update));
    if (!world || !update) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_MESH, world ? 1 : 0);
    for (slot = world->chunks; slot; slot = slot->next) {
        distance = maage_world_distance(world, slot);
        if (slot->pending_mesh && distance <= world->config.render_distance && distance < best &&
            maage_world_mesh_current(world, slot->x, slot->z, slot->mesh_revision)) { chosen = slot; best = distance; }
    }
    if (chosen) {
        update->x = chosen->x; update->z = chosen->z; update->revision = chosen->mesh_revision;
        update->mesh = chosen->pending_mesh; chosen->pending_mesh = NULL;
    }
    return maage_status_make(chosen ? MAAGE_STATUS_SUCCESS : MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_MESH, 0);
}
int maage_world_chunk_loaded(const maage_world *world, int x, int z)
{ return world && maage_world_complete(maage_world_find(world, x, z)); }
maage_block_state_id maage_world_get(const maage_world *world, int x, int y, int z)
{
    maage_world_slot *slot;
    if (!world || y < 0 || y >= MAAGE_WORLD_HEIGHT || x <= -MAAGE_WORLD_LIMIT || x >= MAAGE_WORLD_LIMIT || z <= -MAAGE_WORLD_LIMIT || z >= MAAGE_WORLD_LIMIT) return MAAGE_STATE_AIR;
    slot = maage_world_find(world, maage_world_chunk_coordinate(x), maage_world_chunk_coordinate(z));
    return maage_world_complete(slot) ? maage_chunk_get(slot->edit ? slot->edit : slot->data->chunk,
        maage_world_local_coordinate(x), y, maage_world_local_coordinate(z)) : MAAGE_STATE_AIR;
}
int maage_world_solid(const maage_world *world, int x, int y, int z)
{
    maage_world_slot *slot;
    const maage_block_state_definition *state;
    const maage_block_definition *block;
    if (!world || y < 0 || x <= -MAAGE_WORLD_LIMIT || x >= MAAGE_WORLD_LIMIT || z <= -MAAGE_WORLD_LIMIT || z >= MAAGE_WORLD_LIMIT) return 1;
    slot = maage_world_find(world, maage_world_chunk_coordinate(x), maage_world_chunk_coordinate(z));
    if (!maage_world_complete(slot)) return 1;
    state = maage_block_registry_state(world->config.registry, maage_world_get(world, x, y, z));
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
maage_status maage_world_set(maage_world *world,
    int x, int y, int z, maage_block_state_id expected,
    maage_block_state_id replacement)
{
    maage_world_slot *slot;
    maage_chunk *copy;
    int local_x, local_z, top;
    maage_status result;
    if (!world) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_MUTATE, 0);
    if (world->stopped || world->failed || y < 0 || y >= MAAGE_WORLD_HEIGHT ||
        x <= -MAAGE_WORLD_LIMIT || x >= MAAGE_WORLD_LIMIT ||
        z <= -MAAGE_WORLD_LIMIT || z >= MAAGE_WORLD_LIMIT ||
        !maage_block_registry_state(world->config.registry, replacement) ||
        !maage_block_registry_state(world->config.registry, expected)) {
        ++world->stats.rejected_mutations; return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_MUTATE, 0);
    }
    slot = maage_world_find(world, maage_world_chunk_coordinate(x), maage_world_chunk_coordinate(z));
    if (!maage_world_complete(slot)) {
        ++world->stats.rejected_mutations; return maage_status_make(MAAGE_STATUS_NOT_FOUND, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_MUTATE, 0);
    }
    if (world->clock > ~0UL - 5UL) {
        maage_world_failure_status(world, slot, maage_status_make(MAAGE_STATUS_INVARIANT_VIOLATION,
            MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_INITIALIZE, 0), "World revision identity exhausted.");
        ++world->stats.rejected_mutations;
        return maage_world_status(world);
    }
    local_x = maage_world_local_coordinate(x); local_z = maage_world_local_coordinate(z);
    if (maage_chunk_get(slot->edit ? slot->edit : slot->data->chunk, local_x, y, local_z) != expected) {
        ++world->stats.rejected_mutations; return maage_status_make(MAAGE_STATUS_INTERRUPTED, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_MUTATE, MAAGE_DETAIL_STALE);
    }
    if (expected == replacement) return maage_status_make(MAAGE_STATUS_NOOP, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_MUTATE, 0);
    result = maage_world_edit(world, slot, &copy);
    if (!maage_status_failed(result)) result = maage_chunk_set(copy, local_x, y, local_z, replacement);
    if (maage_status_failed(result)) {
        maage_world_failure_status(world, slot, result, "World block mutation allocation failed.");
        ++world->stats.rejected_mutations;
        if (!world->edit_depth) maage_world_commit_edits(world);
        return result;
    }
    if (maage_block_registry_state(world->config.registry, expected)->block_id !=
        maage_block_registry_state(world->config.registry, replacement)->block_id)
        maage_chunk_block_entity_remove(copy, (unsigned int)((y * 16 + local_z) * 16 + local_x));
    top = MAAGE_WORLD_HEIGHT;
    while (top > 0 && maage_chunk_get(copy, local_x, top - 1, local_z) == MAAGE_STATE_AIR) --top;
    copy->heightmap[local_z * MAAGE_CHUNK_EDGE + local_x] = (unsigned short)top;
    slot->edit_blocks = 1;
    ++world->edit_depth;
    result = maage_environment_changed(world->environment, world, x, y, z);
    if (!--world->edit_depth) maage_world_commit_edits(world);
    ++world->stats.mutations;
    return maage_status_failed(result) ? result : maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_MUTATE, 0);
}
static maage_world_slot *maage_world_entity_slot(const maage_world *world, int x, int y, int z)
{
    maage_world_slot *slot;
    if (!world || y < 0 || y >= MAAGE_WORLD_HEIGHT ||
        x <= -MAAGE_WORLD_LIMIT || x >= MAAGE_WORLD_LIMIT || z <= -MAAGE_WORLD_LIMIT || z >= MAAGE_WORLD_LIMIT) return NULL;
    slot = maage_world_find(world, maage_world_chunk_coordinate(x), maage_world_chunk_coordinate(z));
    return maage_world_complete(slot) ? slot : NULL;
}
maage_status maage_world_block_entity_set(maage_world *world, int x, int y, int z,
    unsigned long type, unsigned long version, const void *data, size_t length)
{
    maage_world_slot *slot = maage_world_entity_slot(world, x, y, z);
    maage_chunk *chunk;
    unsigned cell;
    maage_status result;
    if (!slot || world->stopped || world->failed || !type || !version || (length && !data) ||
        maage_world_get(world, x, y, z) == MAAGE_STATE_AIR) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_SET, 0);
    result = maage_world_edit(world, slot, &chunk);
    if (maage_status_failed(result)) return result;
    cell = (unsigned)((y * 16 + maage_world_local_coordinate(z)) * 16 + maage_world_local_coordinate(x));
    result = maage_chunk_block_entity_set(chunk, cell, type, version, data, length);
    if (maage_status_failed(result)) {
        if (!world->edit_depth) maage_world_commit_edits(world);
        return MAAGE_STATUS_PROPAGATE(&world->trace, result, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_SET);
    }
    if (!world->edit_depth) maage_world_commit_edits(world);
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_SET, 0);
}
maage_status maage_world_block_entity_get(const maage_world *world, int x, int y, int z,
    unsigned long *type, unsigned long *version, void *data, size_t capacity, size_t *length)
{
    maage_world_slot *slot = maage_world_entity_slot(world, x, y, z);
    const maage_chunk *chunk;
    const maage_block_entity *entity;
    size_t index;
    unsigned cell;
    if (length) *length = 0;
    if (!slot || !type || !version || !length) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_READ, 0);
    chunk = slot->edit ? slot->edit : slot->data->chunk;
    cell = (unsigned)((y * 16 + maage_world_local_coordinate(z)) * 16 + maage_world_local_coordinate(x));
    for (index = 0; index < chunk->entity_count; ++index) {
        entity = &chunk->entities[index];
        if (entity->cell != cell) continue;
        *length = entity->length; *type = entity->type; *version = entity->version;
        if (capacity < entity->length || (entity->length && !data)) return maage_status_make(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_READ, 0);
        if (entity->length) memcpy(data, entity->data, entity->length);
        return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_READ, 0);
    }
    return maage_status_make(MAAGE_STATUS_NOT_FOUND, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_READ, 0);
}
maage_status maage_world_block_entity_remove(maage_world *world, int x, int y, int z)
{
    maage_world_slot *slot = maage_world_entity_slot(world, x, y, z);
    maage_chunk *chunk;
    unsigned cell;
    maage_status removed;
    if (!slot || world->stopped || world->failed) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_REMOVE, 0);
    removed = maage_world_edit(world, slot, &chunk);
    if (maage_status_failed(removed)) return removed;
    cell = (unsigned)((y * 16 + maage_world_local_coordinate(z)) * 16 + maage_world_local_coordinate(x));
    removed = maage_chunk_block_entity_remove(chunk, cell);
    if (!world->edit_depth) maage_world_commit_edits(world);
    return removed;
}
maage_status maage_world_schedule_update(maage_world *world, int x, int y, int z,
    unsigned int kind, unsigned long due_low, unsigned long due_high)
{
    maage_world_slot *slot;
    maage_chunk *chunk;
    maage_scheduled_update update;
    maage_status result;
    if (!world || world->stopped || world->failed || kind != MAAGE_BLOCK_UPDATE_FLUID || due_low > 4294967295UL || due_high > 4294967295UL ||
        y < 0 || y >= MAAGE_WORLD_HEIGHT || x <= -MAAGE_WORLD_LIMIT || x >= MAAGE_WORLD_LIMIT ||
        z <= -MAAGE_WORLD_LIMIT || z >= MAAGE_WORLD_LIMIT) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_SUBMIT, 0);
    slot = maage_world_find(world, maage_world_chunk_coordinate(x), maage_world_chunk_coordinate(z));
    if (!maage_world_complete(slot)) return maage_status_make(MAAGE_STATUS_NOT_FOUND, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_SUBMIT, 0);
    result = maage_world_edit(world, slot, &chunk);
    if (maage_status_failed(result)) return result;
    update.kind = kind;
    update.cell = (unsigned int)((y * 16 + maage_world_local_coordinate(z)) * 16 + maage_world_local_coordinate(x));
    update.due_low = due_low; update.due_high = due_high;
    result = maage_chunk_schedule_update(chunk, &update);
    if (maage_status_failed(result)) {
        maage_world_failure_status(world, slot, result, "Scheduled block update allocation failed.");
        if (!world->edit_depth) maage_world_commit_edits(world);
        return result;
    }
    maage_world_update_membership(world, slot);
    if (!world->edit_depth) maage_world_commit_edits(world);
    return result;
}
maage_status maage_world_take_update(maage_world *world, unsigned long now_low,
    unsigned long now_high, int *column_x, int *column_z, maage_scheduled_update *update)
{
    maage_world_slot *slot;
    const maage_chunk *current;
    maage_chunk *chunk;
    unsigned examined, limit;
    maage_status result;
    if (!world || world->stopped || world->failed || !column_x || !column_z || !update) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_POLL, 0);
    /* Only active columns participate. Resume the cursor on the next tick if
     * a bounded inspection finds no due work, even with a very large cache. */
    limit = world->update_columns < 64U ? world->update_columns : 64U;
    for (examined = 0; examined < limit && world->update_head; ++examined) {
        slot = world->update_cursor ? world->update_cursor : world->update_head;
        world->update_cursor = slot->update_next ? slot->update_next : world->update_head;
        current = slot->edit ? slot->edit : slot->data->chunk;
        if (!current->update_count) { maage_world_update_membership(world, slot); continue; }
        if (current->updates[0].due_high > now_high ||
            (current->updates[0].due_high == now_high && current->updates[0].due_low > now_low)) continue;
        result = maage_world_edit(world, slot, &chunk);
        if (maage_status_failed(result)) return result;
        result = maage_chunk_take_update(chunk, now_low, now_high, update);
        if (maage_status_code(result) != MAAGE_STATUS_SUCCESS) {
            if (!maage_status_failed(result)) result = maage_status_make(MAAGE_STATUS_INVARIANT_VIOLATION, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_POLL, 0);
            maage_world_failure_status(world, slot, result, "Due block update could not be consumed."); return result;
        }
        *column_x = slot->x; *column_z = slot->z;
        ++world->stats.block_updates_processed;
        maage_world_update_membership(world, slot);
        if (!world->edit_depth) maage_world_commit_edits(world);
        return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_POLL, 0);
    }
    return maage_status_make(MAAGE_STATUS_NOOP, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_POLL, 0);
}
const maage_block_registry *maage_world_registry(const maage_world *world) { return world ? world->config.registry : NULL; }
void maage_world_spawn(const maage_world *world, double feet[3])
{ if (world && feet) memcpy(feet, world->spawn, sizeof(world->spawn)); }
maage_status maage_world_flush(maage_world *world, maage_status_trace *trace)
{
    maage_world_slot *slot;
    unsigned index;
    maage_status result;
    if (!world) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_STOP, 0);
    if (world->stopped) {
        if (trace && world->failed) *trace = world->trace;
        return world->failed ? world->stats.last_result : maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_STOP, 0);
    }
    /* Lifecycle boundary, outside the render loop: finish saves, discard CPU
     * work and drain its pinned snapshots before closing the store/registries. */
    for (index = 0; index < world->task_limit; ++index)
        if (world->tasks[index].used && world->tasks[index].kind != MAAGE_JOB_SAVE) maage_world_cancel(world, &world->tasks[index]);
    while (world->active) {
        result = maage_jobs_wait(world->jobs);
        if (maage_status_failed(result)) return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_STOP);
        maage_world_collect(world);
    }
    for (slot = world->chunks; slot; slot = slot->next) slot->stage = 0;
    for (;;) {
        for (slot = world->chunks; slot; slot = slot->next)
            if (slot->dirty && slot->data && !slot->save && slot->stage != MAAGE_JOB_SAVE) {
                result = maage_world_submit(world, slot, MAAGE_JOB_SAVE, 0);
                if (maage_status_code(result) != MAAGE_STATUS_SUCCESS) {
                    if (maage_status_failed(result) || !world->active) {
                        if (!maage_status_failed(result)) result = maage_status_make(MAAGE_STATUS_INVARIANT_VIOLATION,
                            MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_SUBMIT, 0);
                        maage_world_failure_status(world, slot, result, "Dirty shutdown snapshot could not be scheduled; previous files preserved.");
                        slot->stage = MAAGE_JOB_SAVE;
                    }
                    break;
                }
                /* One final attempt per snapshot. Failed data stays dirty but
                 * an unavailable device must not cause a shutdown retry loop. */
                slot->stage = MAAGE_JOB_SAVE;
            }
        if (!world->active) break;
        result = maage_jobs_wait(world->jobs);
        if (maage_status_failed(result)) return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_STOP);
        maage_world_collect(world);
    }
    result = maage_jobs_stop(world->jobs);
    if (maage_status_failed(result)) {
        MAAGE_STATUS_PROPAGATE(&world->trace, result, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_STOP);
        if (trace) *trace = world->trace;
        return result;
    }
    world->stopped = 1;
    if (trace && world->failed) *trace = world->trace;
    return world->failed ? world->stats.last_result : maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_STOP, 0);
}
maage_status maage_world_destroy(maage_world *world)
{
    maage_world_slot *slot, *next;
    maage_status result;
    if (!world) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_DESTROY, 0);
    result = maage_world_flush(world, NULL);
    if (!world->stopped) return result;
    result = maage_jobs_destroy(world->jobs, NULL);
    if (maage_status_failed(result)) return result;
    for (slot = world->chunks; slot; slot = next) {
        next = slot->next; maage_world_release(slot->data); maage_chunk_destroy(slot->edit);
        maage_world_mesh_destroy(slot->pending_mesh); free(slot);
    }
    free(world->buckets); free(world->free_tasks);
    maage_environment_destroy(world->environment);
    maage_entities_destroy(world->entities);
    free(world->observations); free(world->tasks); free(world);
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_DESTROY, 0);
}

maage_entities *maage_world_entities(const maage_world *world) { return world ? world->entities : NULL; }
