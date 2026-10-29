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
    unsigned long mesh_failed_revision, block_revision, sent_revision;
    unsigned long mesh_state_revision, mesh_presentation_generation;
    int mesh_predicted, light_changed;
    double mesh_completed_at;
    maage_chunk_snapshot *sent, *authority;
    unsigned char light_cells[8192];
    maage_chunk_snapshot *data;
    maage_world_task *work, *save;
    maage_world_mesh *pending_mesh;
    maage_chunk *edit;
    int edit_blocks, edit_states, edit_light, update_linked, mesh_linked;
    struct maage_world_slot *mesh_next;
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
    int presentation, predicted;
    unsigned long presentation_generation;
    maage_chunk_snapshot *region[25];
    unsigned long region_epochs[25];
    maage_chunk *lit[9];
    unsigned char light_cells[9][8192];
    maage_chunk_snapshot *inputs[9]; /* center, cardinal and diagonal mesh halo */
    unsigned long input_epochs[9];   /* geometry/light identity, independent of queues */
    maage_chunk *output;
    maage_world_mesh *mesh;
    maage_status status;
    unsigned boundaries;
    unsigned long tick_low, tick_high;
    int initialized_environment;
    char error[256];
};
typedef struct maage_chunk_bucket {
    maage_world_slot *slot;
} maage_chunk_bucket;
struct maage_world {
    maage_world_config config;
    maage_jobs *jobs;
    maage_chunk_bucket *buckets;
    size_t bucket_count;
    maage_world_slot *chunks, *save_cursor, *mesh_head, *mesh_tail;
    maage_world_task *tasks;
    maage_job_observation *observations;
    unsigned *free_tasks;
    unsigned count, cache_limit, active, saves_active, free_count, task_limit;
    unsigned scan_step;
    int scan_ring, save_turn;
    unsigned long clock, rate_generated;
    double rate_time, generation_rate;
    int center_x, center_z, stopped, failed, visual_light_active, diagnostics;
    double diagnostic_at;
    maage_block_position predictions[32];
    maage_block_state_id prediction_states[32];
    size_t prediction_count;
    unsigned long prediction_generation;
    maage_leaves_mode leaves_mode;
    double yaw, spawn[3];
    maage_world_stats stats;
    maage_status reported_failures[5]; /* One retained identity per task kind. */
    maage_status_trace trace, last_job_trace;
    maage_environment *environment;
    maage_random_tick_context random_ticks;
    maage_entities *entities;
    maage_world_slot *edits, *update_head, *update_cursor;
    unsigned edit_depth, update_columns;
};
/* Loading/lighting covers both authoritative simulation and presentation;
 * the renderer still receives meshes only inside render_distance. */
static int maage_world_resident_radius(const maage_world *world) {
    return world->config.render_distance > world->config.simulation_distance ? world->config.render_distance : world->config.simulation_distance;
}
maage_status maage_world_config_init(maage_world_config *config) {
    if (!config) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_INITIALIZE, 0);
    memset(config, 0, sizeof(*config));
    config->render_distance = config->simulation_distance = 8;
    config->random_tick_speed = 3;
    config->build_meshes = 1;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_INITIALIZE, 0);
}
/* The aggregate attempt counter is unsigned int. Reject unrepresentable work
 * before entering any loop; never silently clamp or truncate a configured rate. */
static int maage_world_random_speed_valid(int radius, unsigned int speed) {
    size_t span = (size_t) radius * 2U + 1U, columns;
    if (radius < 0 || radius > MAAGE_WORLD_MAX_RADIUS || span > (size_t) UINT_MAX / span) return 0;
    columns = span * span;
    return !speed || (columns <= (size_t) UINT_MAX / 16U && speed <= (size_t) UINT_MAX / (columns * 16U));
}
maage_status maage_world_set_random_tick_speed(maage_world *world, unsigned int speed) {
    if (!world || world->stopped) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_SET, 0);
    if (!maage_world_random_speed_valid(world->config.simulation_distance, speed)) return maage_status_make(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_SET, 0);
    if (world->config.random_tick_speed == speed) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_SET, 0);
    world->config.random_tick_speed = speed;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_SET, 0);
}
static void maage_world_update_membership(maage_world *world, maage_world_slot *slot);
static void maage_world_commit_edits(maage_world *world);
static maage_status maage_world_cancel(maage_world *world, maage_world_task *task);
static void maage_world_failure(maage_world *world, maage_world_slot *slot, const char *error);
static void maage_world_failure_status(maage_world *world, maage_world_slot *slot, maage_status result, const char *error);
static unsigned long maage_world_revision(maage_world *world) {
    /* Never wrap into an old incarnation/result token. Exhaustion is explicit. */
    if (world->clock == ~0UL) {
        maage_world_failure_status(world, NULL, maage_status_make(MAAGE_STATUS_INVARIANT_VIOLATION, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_INITIALIZE, 0), "World revision identity exhausted.");
        return 0;
    }
    return ++world->clock;
}
static maage_status maage_world_snapshot(maage_chunk *chunk, maage_chunk_snapshot **output) {
    maage_chunk_snapshot *snapshot;
    *output = NULL;
    snapshot = (maage_chunk_snapshot *) malloc(sizeof(*snapshot));
    if (!snapshot) return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_SNAPSHOT, 0);
    snapshot->chunk = chunk;
    snapshot->references = 1;
    *output = snapshot;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_SNAPSHOT, 0);
}
static void maage_world_release(maage_chunk_snapshot *snapshot) {
    if (snapshot && !--snapshot->references) {
        maage_chunk_destroy(snapshot->chunk);
        free(snapshot);
    }
}
static size_t maage_world_bucket(int x, int z, size_t count) {
    unsigned long hash = (unsigned long) x * 0x8da6b343UL ^ (unsigned long) z * 0xd8163841UL;
    hash ^= hash >> 16;
    return (size_t) hash & (count - 1);
}
/* Open-addressed owner-only map. Rehash before occupancy reaches one half;
 * both allocation and bucket count grow with actual resident membership. */
static int maage_world_grow_buckets(maage_world *world) {
    maage_chunk_bucket *buckets;
    maage_world_slot *slot;
    size_t count, bucket;
    if (world->bucket_count && (size_t) world->count < world->bucket_count / 2) return 1;
    if (world->bucket_count > (size_t) - 1 / 2) return 0;
    count = world->bucket_count ? world->bucket_count * 2 : 16;
    if (count > (size_t) - 1 / sizeof(*buckets)) return 0;
    buckets = (maage_chunk_bucket *) calloc(count, sizeof(*buckets));
    if (!buckets) return 0;
    for (slot = world->chunks; slot; slot = slot->next) {
        bucket = maage_world_bucket(slot->x, slot->z, count);
        while (buckets[bucket].slot)
            bucket = (bucket + 1) & (count - 1);
        buckets[bucket].slot = slot;
    }
    free(world->buckets);
    world->buckets = buckets;
    world->bucket_count = count;
    return 1;
}
static maage_world_slot *maage_world_find(const maage_world *world, int x, int z) {
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
static maage_world_slot *maage_world_insert(maage_world *world, int x, int z) {
    maage_world_slot *slot;
    size_t bucket;
    if (world->count >= world->cache_limit) return NULL;
    if (!maage_world_grow_buckets(world)) {
        maage_world_failure(world, NULL, "World column index allocation failed.");
        return NULL;
    }
    slot = (maage_world_slot *) calloc(1, sizeof(*slot));
    if (!slot) {
        maage_world_failure(world, NULL, "World column allocation failed.");
        return NULL;
    }
    slot->x = x;
    slot->z = z;
    slot->stage = MAAGE_JOB_LOAD;
    slot->incarnation = slot->revision = maage_world_revision(world);
    bucket = maage_world_bucket(x, z, world->bucket_count);
    while (world->buckets[bucket].slot)
        bucket = (bucket + 1) & (world->bucket_count - 1);
    world->buckets[bucket].slot = slot;
    slot->next = world->chunks;
    if (world->chunks) world->chunks->previous = slot;
    world->chunks = slot;
    ++world->count;
    return slot;
}
static void maage_world_remove(maage_world *world, maage_world_slot *slot) {
    size_t bucket = maage_world_bucket(slot->x, slot->z, world->bucket_count), destination;
    maage_world_slot *displaced;
    if (slot->update_linked) {
        if (slot->update_previous) slot->update_previous->update_next = slot->update_next;
        else
            world->update_head = slot->update_next;
        if (slot->update_next) slot->update_next->update_previous = slot->update_previous;
        if (world->update_cursor == slot) world->update_cursor = slot->update_next ? slot->update_next : world->update_head;
        --world->update_columns;
    }
    if (slot->mesh_linked) {
        maage_world_slot *previous = NULL, *current = world->mesh_head;
        while (current && current != slot) {
            previous = current;
            current = current->mesh_next;
        }
        if (previous) previous->mesh_next = slot->mesh_next;
        else
            world->mesh_head = slot->mesh_next;
        if (world->mesh_tail == slot) world->mesh_tail = previous;
    }
    while (world->buckets[bucket].slot != slot)
        bucket = (bucket + 1) & (world->bucket_count - 1);
    world->buckets[bucket].slot = NULL;
    bucket = (bucket + 1) & (world->bucket_count - 1);
    while (world->buckets[bucket].slot) {
        displaced = world->buckets[bucket].slot;
        world->buckets[bucket].slot = NULL;
        destination = maage_world_bucket(displaced->x, displaced->z, world->bucket_count);
        while (world->buckets[destination].slot)
            destination = (destination + 1) & (world->bucket_count - 1);
        world->buckets[destination].slot = displaced;
        bucket = (bucket + 1) & (world->bucket_count - 1);
    }
    if (slot->previous) slot->previous->next = slot->next;
    else
        world->chunks = slot->next;
    if (slot->next) slot->next->previous = slot->previous;
    --world->count;
    if (world->save_cursor == slot) world->save_cursor = slot->next ? slot->next : world->chunks;
    maage_world_release(slot->data);
    maage_world_release(slot->sent);
    maage_world_release(slot->authority);
    maage_world_mesh_destroy(slot->pending_mesh);
    free(slot);
}
static int maage_world_distance(const maage_world *world, const maage_world_slot *slot) {
    int x = abs(slot->x - world->center_x), z = abs(slot->z - world->center_z);
    return x > z ? x : z;
}
static int maage_world_complete(const maage_world_slot *slot) {
    return slot && slot->data && !slot->failed;
}
static void maage_world_neighbors(const maage_world *world, int x, int z, maage_world_slot *neighbors[8]) {
    static const int offsets[8][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}, {-1, -1}, {1, -1}, {-1, 1}, {1, 1}};
    int side;
    for (side = 0; side < 8; ++side) {
        neighbors[side] = maage_world_find(world, x + offsets[side][0], z + offsets[side][1]);
        if (!maage_world_complete(neighbors[side])) neighbors[side] = NULL;
    }
}
static void maage_world_invalidate(maage_world *world, maage_world_slot *slot, int light) {
    slot->mesh_dirty = 1;
    slot->revision = maage_world_revision(world);
    maage_world_mesh_destroy(slot->pending_mesh);
    slot->pending_mesh = NULL;
    if (light) {
        slot->light_dirty = 1;
        if (!world->config.presentation_only) slot->light_ready = 0;
        slot->light_epoch = slot->revision;
    }
}
static void maage_world_invalidate_neighbors(maage_world *world, int x, int z, unsigned mask) {
    maage_world_slot *neighbors[8];
    int side;
    maage_world_neighbors(world, x, z, neighbors);
    for (side = 0; side < 8; ++side) {
        unsigned affected = side < 4 ? 1U << side : ((1U << ((side - 4) & 1)) | (1U << (side < 6 ? 2 : 3)));
        if (neighbors[side] && (mask & affected)) maage_world_invalidate(world, neighbors[side], side < 4 && !world->config.presentation_only);
    }
}
static void maage_world_failure_status(maage_world *world, maage_world_slot *slot, maage_status result, const char *error) {
    world->failed = 1;
    if (slot) slot->failed = 1;
    if (world->stats.last_result != result) {
        world->stats.last_result = result;
        ++world->stats.failure_reports;
        maage_status_trace_init(&world->trace);
        MAAGE_STATUS_TRACE(&world->trace, result, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_MUTATE);
        maage_log_trace_status(MAAGE_LOG_FATAL, "maage.world", result, &world->trace, "%s", error);
    }
}
static void maage_world_failure(maage_world *world, maage_world_slot *slot, const char *error) {
    maage_world_failure_status(world, slot, maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_INITIALIZE, 0), error);
}
maage_status maage_chunk_clone(const maage_chunk *source, maage_chunk **output) {
    maage_chunk *copy = NULL;
    maage_status result;
    int section;
    *output = NULL;
    result = maage_chunk_create(source->x, source->z, &copy);
    if (maage_status_failed(result)) return result;
    memcpy(copy->biomes, source->biomes, sizeof(copy->biomes));
    memcpy(copy->heightmap, source->heightmap, sizeof(copy->heightmap));
    memcpy(copy->random_tick_counts, source->random_tick_counts, sizeof(copy->random_tick_counts));
    result = maage_chunk_copy_environment(copy, source);
    if (maage_status_failed(result)) {
        maage_chunk_destroy(copy);
        return result;
    }
    for (section = 0; section < 16; ++section)
        if (source->sections[section]) {
            copy->sections[section] = (maage_section *) malloc(sizeof(maage_section));
            if (!copy->sections[section]) {
                maage_chunk_destroy(copy);
                return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_COPY, 0);
            }
            memcpy(copy->sections[section], source->sections[section], sizeof(maage_section));
        }
    if (source->direct_sky) {
        copy->direct_sky = (unsigned char *) malloc(65536UL);
        if (!copy->direct_sky) {
            maage_chunk_destroy(copy);
            return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_COPY, 0);
        }
        memcpy(copy->direct_sky, source->direct_sky, 65536UL);
    }
    *output = copy;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_COPY, 0);
}
/* One private edit per touched column in a simulation phase. Workers retain
 * published snapshots; queue and fluid changes publish together at its end. */
static maage_status maage_world_edit(maage_world *world, maage_world_slot *slot, maage_chunk **output) {
    maage_status result;
    *output = slot->edit;
    if (slot->edit) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_SNAPSHOT, 0);
    result = maage_chunk_clone(slot->data->chunk, &slot->edit);
    if (maage_status_failed(result)) {
        maage_world_failure_status(world, slot, result, "World edit snapshot allocation failed.");
        return result;
    }
    slot->edit_next = world->edits;
    world->edits = slot;
    *output = slot->edit;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_SNAPSHOT, 0);
}
static void maage_world_update_membership(maage_world *world, maage_world_slot *slot) {
    const maage_chunk *chunk = slot->edit ? slot->edit : slot->data->chunk;
    if (chunk->update_count && !slot->update_linked) {
        slot->update_next = world->update_head;
        slot->update_previous = NULL;
        if (world->update_head) world->update_head->update_previous = slot;
        world->update_head = slot;
        slot->update_linked = 1;
        ++world->update_columns;
        if (!world->update_cursor) world->update_cursor = slot;
    } else if (!chunk->update_count && slot->update_linked) {
        if (slot->update_previous) slot->update_previous->update_next = slot->update_next;
        else
            world->update_head = slot->update_next;
        if (slot->update_next) slot->update_next->update_previous = slot->update_previous;
        if (world->update_cursor == slot) world->update_cursor = slot->update_next ? slot->update_next : world->update_head;
        slot->update_next = slot->update_previous = NULL;
        slot->update_linked = 0;
        --world->update_columns;
    }
}
static void maage_world_commit_edits(maage_world *world) {
    maage_world_slot *slot;
    maage_chunk_snapshot *snapshot;
    while (world->edits) {
        slot = world->edits;
        world->edits = slot->edit_next;
        slot->edit_next = NULL;
        if (maage_status_failed(maage_world_snapshot(slot->edit, &snapshot))) {
            maage_chunk_destroy(slot->edit);
            slot->edit = NULL;
            maage_world_failure(world, slot, "Environment snapshot publication allocation failed.");
            maage_world_update_membership(world, slot);
            continue;
        }
        maage_world_release(slot->data);
        slot->data = snapshot;
        slot->edit = NULL;
        slot->dirty = !world->config.presentation_only;
        if (slot->edit_blocks) {
            maage_world_invalidate(world, slot, 1);
            slot->data_epoch = slot->revision;
            slot->block_revision = slot->revision;
            if (world->diagnostics) maage_log_info("maage.world.latency", "owner=server mutation_revision=%lu column=%d,%d mutated_at=%.6f", slot->block_revision, slot->x, slot->z, maage_time_monotonic_seconds());
            maage_world_invalidate_neighbors(world, slot->x, slot->z, 15U);
            maage_world_cancel(world, slot->work);
            slot->work = NULL;
            world->scan_ring = 0;
            world->scan_step = 0;
        }
        if (slot->edit_states && !slot->edit_blocks) slot->block_revision = maage_world_revision(world);
        slot->edit_states = slot->edit_blocks = slot->edit_light = 0;
        maage_world_update_membership(world, slot);
    }
}
static unsigned maage_world_light_boundaries(const maage_chunk *before, const maage_chunk *after) {
    unsigned changed = 0;
    int side, y, p, x, z;
    for (side = 0; side < 4; ++side) {
        for (y = 0; y < 256 && !(changed & (1U << side)); ++y)
            for (p = 0; p < 16; ++p) {
                x = side < 2 ? (side ? 15 : 0) : p;
                z = side < 2 ? p : (side == 3 ? 15 : 0);
                if (maage_chunk_light(before, x, y, z) != maage_chunk_light(after, x, y, z)) {
                    changed |= 1U << side;
                    break;
                }
            }
    }
    return changed;
}
/* Generation, lighting snapshot copying, propagation, meshing, serialization
 * and filesystem work run without a world/scheduler lock. Owner mutations
 * separately copy a published column before its atomic owner-thread replacement. */
static void maage_world_run(void *context, const maage_job_token *token) {
    maage_world_task *task = (maage_world_task *) context;
    const maage_chunk *neighbors[8];
    maage_section *added[16];
    int side, section, leaf_rechecks;
    maage_status_trace *trace = maage_job_trace(token);
    task->status = maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_WORLD,
                                     task->kind == MAAGE_JOB_GENERATE ? MAAGE_OPERATION_GENERATE : (task->kind == MAAGE_JOB_LIGHT ? MAAGE_OPERATION_LIGHT : MAAGE_OPERATION_MESH), 0);
    if (maage_job_cancelled(token)) {
        task->status = maage_status_make(MAAGE_STATUS_INTERRUPTED, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_JOB, MAAGE_DETAIL_CANCELLED);
        goto finished;
    }
    if (task->presentation && task->kind == MAAGE_JOB_LIGHT) {
        const maage_chunk *halo[25];
        int x, z, index;
        for (index = 0; index < 25; ++index)
            halo[index] = task->region[index] ? task->region[index]->chunk : NULL;
        for (z = 0; z < 3; ++z)
            for (x = 0; x < 3; ++x) {
                index = x + 3 * z;
                if (!halo[x + 1 + 5 * (z + 1)]) continue;
                task->status = maage_chunk_clone(halo[x + 1 + 5 * (z + 1)], &task->lit[index]);
                if (maage_status_failed(task->status)) goto finished;
            }
        task->status = maage_light_region_job(task->lit, halo, task->light_cells, task->blocks, token, trace);
        goto finished;
    }
    if (task->kind == MAAGE_JOB_LOAD) {
        task->status = task->store ? maage_world_store_load_chunk(task->store, task->x, task->z, task->blocks, task->biomes, &task->output, task->error, trace) : maage_status_make(MAAGE_STATUS_NOT_FOUND, MAAGE_CONTEXT_WORLD_IO, MAAGE_OPERATION_LOAD, 0);
    } else if (task->kind == MAAGE_JOB_GENERATE) {
        task->status = maage_chunk_create(task->x, task->z, &task->output);
        if (!maage_status_failed(task->status)) task->status = maage_generate_chunk_job(task->output, task->seed, task->blocks, task->biomes, &task->generation_policy, token, trace);
        if (maage_status_failed(task->status)) strcpy(task->error, "Chunk generation failed.");
    } else if (task->kind == MAAGE_JOB_SAVE) {
        task->status = task->store ? maage_world_store_save_chunk_at(task->store, task->inputs[0]->chunk, task->blocks, task->biomes, task->tick_low, task->tick_high, task->error, trace) : maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_SAVE, 0);
    } else {
        for (side = 0; side < 8; ++side)
            neighbors[side] = task->inputs[side + 1] ? task->inputs[side + 1]->chunk : NULL;
        if (task->kind == MAAGE_JOB_MESH) {
            task->status = maage_world_mesh_create_chunk_job(task->blocks, task->biomes, task->inputs[0]->chunk, neighbors, task->leaves_mode, token, &task->mesh, trace);
            if (maage_status_failed(task->status)) strcpy(task->error, "Chunk CPU mesh construction failed.");
        } else if (task->kind == MAAGE_JOB_LIGHT) {
            task->status = maage_chunk_clone(task->inputs[0]->chunk, &task->output);
            if (maage_status_failed(task->status)) {
                strcpy(task->error, "Chunk lighting snapshot allocation failed.");
                goto finished;
            }
            task->status = maage_light_chunk_job(task->output, task->blocks, neighbors, added, token, trace);
            for (section = 0; section < 16; ++section)
                if (added[section]) task->output->sections[section] = added[section];
            if (maage_status_failed(task->status)) strcpy(task->error, "Chunk lighting failed.");
            else if (maage_status_code(task->status) == MAAGE_STATUS_SUCCESS) task->boundaries = maage_world_light_boundaries(task->inputs[0]->chunk, task->output);
        }
    }
    if ((task->kind == MAAGE_JOB_LOAD || task->kind == MAAGE_JOB_GENERATE) &&
        !maage_status_failed(task->status) && task->output &&
        (!task->output->environment_initialized || task->output->updates_relative)) {
        task->status = maage_chunk_environment_initialize(task->output, task->blocks, task->tick_low, task->tick_high);
        if (maage_status_failed(task->status)) strcpy(task->error, "Initial block update initialization failed.");
        else
            task->initialized_environment = 1;
    }
    if ((task->kind == MAAGE_JOB_LOAD || task->kind == MAAGE_JOB_GENERATE) && !maage_status_failed(task->status) && task->output) {
        task->status = maage_chunk_random_ticks_initialize(task->output, task->blocks, &leaf_rechecks);
        if (leaf_rechecks) task->initialized_environment = 1;
        if (maage_status_failed(task->status)) strcpy(task->error, "Random-tick section initialization failed.");
        else {
            task->status = maage_chunk_support_initialize(task->output, task->blocks, task->tick_low, task->tick_high);
            if (maage_status_code(task->status) == MAAGE_STATUS_SUCCESS) task->initialized_environment = 1;
            if (maage_status_failed(task->status)) strcpy(task->error, "Support validation queue initialization failed.");
        }
    }
finished:
    if (!maage_status_is_success(task->status))
        MAAGE_STATUS_TRACE(trace, task->status, MAAGE_CONTEXT_WORLD,
                           task->kind == MAAGE_JOB_GENERATE ? MAAGE_OPERATION_GENERATE : (task->kind == MAAGE_JOB_LIGHT ? MAAGE_OPERATION_LIGHT : (task->kind == MAAGE_JOB_MESH ? MAAGE_OPERATION_MESH : (task->kind == MAAGE_JOB_SAVE ? MAAGE_OPERATION_SAVE : MAAGE_OPERATION_LOAD))));
    maage_job_set_result(token, task->status);
}
static void maage_world_task_release(maage_world *world, maage_world_task *task) {
    int input;
    for (input = 0; input < 9; ++input) {
        maage_world_release(task->inputs[input]);
        maage_chunk_destroy(task->lit[input]);
    }
    for (input = 0; input < 25; ++input)
        maage_world_release(task->region[input]);
    if (task->presentation && task->kind == MAAGE_JOB_LIGHT) world->visual_light_active = 0;
    maage_chunk_destroy(task->output);
    maage_world_mesh_destroy(task->mesh);
    if (task->kind == MAAGE_JOB_SAVE) --world->saves_active;
    world->free_tasks[world->free_count++] = (unsigned) (task - world->tasks);
    memset(task, 0, sizeof(*task));
    --world->active;
}
static int maage_world_inputs_current(const maage_world *world, const maage_world_slot *slot, const maage_world_task *task) {
    maage_world_slot *neighbors[8];
    int side;
    if (task->presentation && task->kind == MAAGE_JOB_LIGHT) {
        int x, z, index;
        maage_world_slot *other;
        for (z = -2; z <= 2; ++z)
            for (x = -2; x <= 2; ++x) {
                index = x + 2 + 5 * (z + 2);
                other = maage_world_find(world, slot->x + x, slot->z + z);
                if ((maage_world_complete(other) ? other->data_epoch : 0) != task->region_epochs[index]) return 0;
            }
        return 1;
    }
    if (slot->data_epoch != task->input_epochs[0]) return 0;
    maage_world_neighbors(world, slot->x, slot->z, neighbors);
    for (side = 0; side < (task->kind == MAAGE_JOB_MESH ? 8 : 4); ++side)
        if ((neighbors[side] ? neighbors[side]->data_epoch : 0) != task->input_epochs[side + 1]) return 0;
    return 1;
}
static maage_status maage_world_publish(maage_world *world, maage_world_slot *slot, maage_world_task *task) {
    maage_chunk_snapshot *snapshot;
    maage_status result = maage_world_snapshot(task->output, &snapshot);
    if (maage_status_failed(result)) {
        maage_world_failure_status(world, slot, result, "Chunk snapshot publication allocation failed.");
        return result;
    }
    maage_world_release(slot->data);
    slot->data = snapshot;
    task->output = NULL;
    slot->dirty = !world->config.presentation_only;
    maage_world_invalidate(world, slot, 0);
    slot->data_epoch = slot->revision;
    maage_world_update_membership(world, slot);
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_UPDATE, 0);
}
static maage_status maage_world_collect(maage_world *world) {
    maage_job_completion completion;
    maage_world_task *task;
    maage_world_slot *slot;
    int stale, obsolete, owns;
    maage_status result, disposition, outcome = maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_POLL, 0);
    unsigned long failures = world->stats.failure_reports;
    while (1) {
        result = maage_jobs_poll(world->jobs, &completion);
        if (maage_status_failed(result)) {
            maage_world_failure_status(world, NULL, result, "World completion poll failed.");
            return result;
        }
        if (maage_status_code(result) != MAAGE_STATUS_SUCCESS) break;
        outcome = result;
        task = (maage_world_task *) completion.context;
        task->status = completion.operation_result;
        disposition = completion.result;
        slot = maage_world_find(world, task->x, task->z);
        stale = !slot || slot->incarnation != task->incarnation;
        obsolete = slot && maage_world_distance(world, slot) > maage_world_resident_radius(world) + 2;
        owns = !stale && (task->kind == MAAGE_JOB_SAVE ? slot->save == task : slot->work == task);
        if (owns) {
            if (task->kind == MAAGE_JOB_SAVE) slot->save = NULL;
            else
                slot->work = NULL;
        }
        if (owns && (completion.cancelled || obsolete)) {
            if (task->kind == MAAGE_JOB_LIGHT) slot->light_dirty = 1;
            if (task->kind == MAAGE_JOB_MESH) slot->mesh_dirty = 1;
        }
        if (completion.cancelled) {
            ++world->stats.cancelled_jobs;
        } else if (stale || (obsolete && task->kind != MAAGE_JOB_SAVE)) {
            ++world->stats.stale_jobs;
            disposition = maage_status_make(MAAGE_STATUS_INTERRUPTED, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_JOB, MAAGE_DETAIL_STALE);
        } else if ((task->kind == MAAGE_JOB_LIGHT || task->kind == MAAGE_JOB_MESH) &&
                   (!maage_world_inputs_current(world, slot, task) ||
                    (task->kind == MAAGE_JOB_LIGHT && slot->light_epoch != task->light_epoch) ||
                    (task->kind == MAAGE_JOB_MESH && (slot->revision != task->revision || (!world->config.presentation_only && slot->light_dirty))))) {
            ++world->stats.stale_jobs;
            disposition = maage_status_make(MAAGE_STATUS_INTERRUPTED, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_JOB, MAAGE_DETAIL_STALE);
            if (task->kind == MAAGE_JOB_LIGHT) slot->light_dirty = 1;
            else
                slot->mesh_dirty = 1;
        } else if (task->kind == MAAGE_JOB_LOAD && maage_status_code(task->status) == MAAGE_STATUS_NOT_FOUND &&
                   maage_status_context(task->status) == MAAGE_CONTEXT_WORLD_IO &&
                   maage_status_operation(task->status) == MAAGE_OPERATION_LOAD) {
            slot->stage = MAAGE_JOB_GENERATE;
        } else if (maage_status_failed(task->status)) {
            int optional = task->kind == MAAGE_JOB_MESH && (maage_status_code(task->status) == MAAGE_STATUS_ALLOCATION_FAILED || maage_status_code(task->status) == MAAGE_STATUS_BUFFER_OVERFLOW);
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
                slot->mesh_dirty = 1;
                slot->mesh_failed_revision = task->revision;
            } else {
                world->failed = 1;
                if (task->kind != MAAGE_JOB_SAVE) slot->failed = 1;
            }
        } else if (task->kind == MAAGE_JOB_SAVE) {
            /* A newer light/block snapshot remains dirty while its old coherent
             * revision finishes saving. Never overlap writes to one file. */
            if (slot->data == task->inputs[0]) slot->dirty = 0;
        } else if (task->kind == MAAGE_JOB_LOAD || task->kind == MAAGE_JOB_GENERATE) {
            if (!maage_status_failed(maage_world_publish(world, slot, task))) {
                slot->block_revision = slot->revision;
                slot->stage = 0;
                slot->dirty = task->kind == MAAGE_JOB_GENERATE || task->initialized_environment;
                slot->light_dirty = 1;
                slot->light_epoch = slot->revision;
                if (task->kind == MAAGE_JOB_LOAD) ++world->stats.loaded_from_disk;
                else {
                    ++world->stats.generated;
                    if (slot->data->chunk->generation_stages & (1U << MAAGE_GENERATION_STAGE_VEGETATION)) ++world->stats.decorated_columns;
                    world->stats.structure_fragments += slot->data->chunk->generation_features[MAAGE_GENERATION_STAGE_STRUCTURES];
                }
                maage_world_invalidate_neighbors(world, slot->x, slot->z, 15U);
            }
        } else if (task->presentation && task->kind == MAAGE_JOB_LIGHT) {
            int x, z, index;
            maage_world_slot *other;
            maage_chunk_snapshot *snapshot;
            for (z = -1; z <= 1; ++z)
                for (x = -1; x <= 1; ++x) {
                    int changed;
                    index = x + 1 + 3 * (z + 1);
                    if (!task->lit[index]) continue;
                    other = maage_world_find(world, task->x + x, task->z + z);
                    changed = maage_world_light_boundaries(other->data->chunk, task->lit[index]) != 0;
                    if (!changed) {
                        int sy;
                        for (sy = 0; sy < 16; ++sy) {
                            const maage_section *before = other->data->chunk->sections[sy];
                            const maage_section *after = task->lit[index]->sections[sy];
                            if ((before == NULL) != (after == NULL) || (before && memcmp(before->light, after->light, 4096))) {
                                changed = 1;
                                break;
                            }
                        }
                    }
                    result = maage_world_snapshot(task->lit[index], &snapshot);
                    if (maage_status_failed(result)) {
                        maage_world_failure_status(world, other, result, "Visual light publication failed.");
                        break;
                    }
                    task->lit[index] = NULL;
                    maage_world_release(other->data);
                    other->data = snapshot;
                    other->light_ready = 1;
                    if (other == slot) {
                        other->light_dirty = 0;
                        memset(other->light_cells, 0, sizeof(other->light_cells));
                    }
                    if (changed || other == slot) {
                        maage_world_invalidate(world, other, 0);
                        other->data_epoch = other->revision;
                        maage_world_invalidate_neighbors(world, other->x, other->z, 15U);
                    }
                }
        } else if (task->kind == MAAGE_JOB_LIGHT) {
            if (maage_status_code(task->status) == MAAGE_STATUS_NOOP) slot->light_ready = 1;
            else if (slot->data != task->inputs[0] && maage_status_failed(maage_chunk_replace_environment(task->output, slot->data->chunk))) maage_world_failure(world, slot, "Lighting result auxiliary snapshot allocation failed.");
            else if (!maage_status_failed(maage_world_publish(world, slot, task))) {
                slot->light_ready = 1;
                maage_world_invalidate_neighbors(world, slot->x, slot->z, task->boundaries);
            }
        } else if (task->kind == MAAGE_JOB_MESH) {
            maage_world_mesh_destroy(slot->pending_mesh);
            slot->pending_mesh = task->mesh;
            task->mesh = NULL;
            slot->mesh_completed_at = completion.completed_at;
            slot->mesh_revision = task->revision;
            slot->mesh_state_revision = task->light_epoch;
            slot->mesh_predicted = task->predicted;
            slot->mesh_presentation_generation = task->presentation_generation;
            ++world->stats.meshes;
            if (!slot->mesh_linked) {
                slot->mesh_linked = 1;
                slot->mesh_next = NULL;
                if (world->mesh_tail) world->mesh_tail->mesh_next = slot;
                else
                    world->mesh_head = slot;
                world->mesh_tail = slot;
            }
        }
        if (world->diagnostics) maage_log_info("maage.world.latency", "owner=%s kind=%d job=%lu column=%d,%d revision=%lu queue_ms=%.3f run_ms=%.3f cancelled=%d stale=%d", world->config.presentation_only ? "client" : "server", task->kind, task->id, task->x, task->z, task->revision, completion.queue_seconds * 1000, completion.run_seconds * 1000, completion.cancelled, maage_status_detail(disposition) == MAAGE_DETAIL_STALE);
        ++world->stats.completed_jobs;
        world->stats.last_job_result = disposition;
        world->stats.last_job_operation_result = completion.operation_result;
        world->last_job_trace = completion.trace;
        if (disposition != completion.operation_result) MAAGE_STATUS_TRACE(&world->last_job_trace, disposition, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_POLL);
        maage_world_task_release(world, task);
    }
    if (world->failed) return world->stats.last_result;
    if (world->stats.failure_reports != failures) return maage_status_make(MAAGE_STATUS_PARTIAL, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_POLL, 0);
    return outcome;
}
static maage_status maage_world_submit(maage_world *world, maage_world_slot *slot, int kind, unsigned priority) {
    maage_world_task *task = NULL;
    maage_world_slot *neighbors[8];
    unsigned index;
    int side;
    maage_environment_state environment;
    maage_status_trace parent;
    maage_status result;
    if (!world->free_count) {
        ++world->stats.queue_saturation_events;
        return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_SUBMIT, 0);
    }
    index = world->free_tasks[--world->free_count];
    task = &world->tasks[index];
    memset(task, 0, sizeof(*task));
    task->used = 1;
    task->kind = kind;
    task->x = slot->x;
    task->z = slot->z;
    task->incarnation = slot->incarnation;
    task->revision = slot->revision;
    task->light_epoch = slot->light_epoch;
    task->blocks = world->config.registry;
    task->biomes = world->config.biomes;
    task->seed = world->config.seed;
    task->store = world->config.store;
    task->generation_policy = world->config.generation_policy;
    task->leaves_mode = world->leaves_mode;
    task->presentation = world->config.presentation_only;
    task->presentation_generation = world->prediction_generation;
    task->predicted = world->prediction_count != 0;
    if (kind == MAAGE_JOB_MESH) task->light_epoch = slot->block_revision;
    if (task->presentation && kind == MAAGE_JOB_LIGHT) {
        int x, z, i;
        maage_world_slot *other;
        for (z = -2; z <= 2; ++z)
            for (x = -2; x <= 2; ++x) {
                i = x + 2 + 5 * (z + 2);
                other = maage_world_find(world, slot->x + x, slot->z + z);
                if (!maage_world_complete(other)) continue;
                task->region[i] = other->data;
                ++other->data->references;
                task->region_epochs[i] = other->data_epoch;
                if (x >= -1 && x <= 1 && z >= -1 && z <= 1) memcpy(task->light_cells[x + 1 + 3 * (z + 1)], other->light_cells, 8192);
            }
    }
    maage_environment_snapshot(world->environment, &environment);
    task->tick_low = environment.ticks_low;
    task->tick_high = environment.ticks_high;
    if (kind >= MAAGE_JOB_LIGHT) {
        task->inputs[0] = slot->data;
        ++slot->data->references;
        task->input_epochs[0] = slot->data_epoch;
        if (kind != MAAGE_JOB_SAVE) {
            maage_world_neighbors(world, slot->x, slot->z, neighbors);
            for (side = 0; side < (kind == MAAGE_JOB_MESH ? 8 : 4); ++side)
                if (neighbors[side]) {
                    task->inputs[side + 1] = neighbors[side]->data;
                    ++task->inputs[side + 1]->references;
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
    else
        slot->work = task;
    if (kind == MAAGE_JOB_LIGHT) {
        slot->light_dirty = 0;
        if (task->presentation) world->visual_light_active = 1;
    }
    if (kind == MAAGE_JOB_MESH) slot->mesh_dirty = 0;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_SUBMIT, 0);
}
static maage_status maage_world_cancel(maage_world *world, maage_world_task *task) {
    maage_status result;
    if (!task || task->cancel_requested) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_CANCEL, 0);
    result = maage_jobs_cancel(world->jobs, task->id);
    if (!maage_status_failed(result)) task->cancel_requested = 1;
    return MAAGE_STATUS_PROPAGATE(&world->trace, result, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_CANCEL);
}
static void maage_world_retire(maage_world *world) {
    maage_world_slot *slot, *next;
    int radius = maage_world_resident_radius(world) + 2;
    for (slot = world->chunks; slot; slot = next) {
        int x, z;
        next = slot->next;
        if (maage_world_distance(world, slot) <= radius) continue;
        maage_world_cancel(world, slot->work);
        /* A cancelled worker borrows snapshots, not the membership slot.
         * Remove clean columns immediately; old results fail incarnation
         * validation when their completion eventually reaches the owner. */
        if ((slot->data && slot->dirty) || slot->save) continue;
        x = slot->x;
        z = slot->z;
        maage_world_remove(world, slot);
        maage_world_invalidate_neighbors(world, x, z, 15U);
    }
}
static int maage_world_make_room(maage_world *world) {
    maage_world_slot *slot, *victim = NULL;
    int distance, farthest = maage_world_resident_radius(world);
    int x, z;
    if (world->count < world->cache_limit) return 1;
    for (slot = world->chunks; slot; slot = slot->next) {
        if (slot->dirty || slot->save) continue;
        distance = maage_world_distance(world, slot);
        if (distance > farthest) {
            farthest = distance;
            victim = slot;
        }
    }
    if (!victim) {
        ++world->stats.cache_pressure_events;
        return 0;
    }
    maage_world_cancel(world, victim->work);
    x = victim->x;
    z = victim->z;
    maage_world_remove(world, victim);
    maage_world_invalidate_neighbors(world, x, z, 15U);
    return 1;
}
/* Walk concentric square rings incrementally. A single pump examines at
 * most a workload-derived budget, never rescanning the entire grid per job.
 * On movement the cursor restarts at the nearest column. */
static void maage_world_scan_next(maage_world *world, int *dx, int *dz) {
    int ring = world->scan_ring;
    unsigned step, span;
    if (!ring) {
        *dx = *dz = 0;
        world->scan_ring = 1;
        world->scan_step = 0;
        return;
    }
    step = world->scan_step++;
    span = (unsigned) ring * 2U + 1U;
    if (step < span) {
        *dx = (int) step - ring;
        *dz = -ring;
    } else if (step < span + (unsigned) ring * 2U) {
        *dx = ring;
        *dz = (int) (step - span) - ring + 1;
    } else if (step < span + (unsigned) ring * 4U) {
        *dx = ring - 1 - (int) (step - span - (unsigned) ring * 2U);
        *dz = ring;
    } else {
        *dx = -ring;
        *dz = ring - 1 - (int) (step - span - (unsigned) ring * 4U);
    }
    if (world->scan_step == (unsigned) ring * 8U) {
        ++world->scan_ring;
        world->scan_step = 0;
    }
}
static maage_status maage_world_schedule_saves(maage_world *world, unsigned reserve) {
    maage_world_slot *slot, *first, *next;
    maage_status result = maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_SUBMIT, 0);
    if (world->config.presentation_only || world->failed || !world->chunks || world->active >= world->task_limit || world->saves_active >= reserve) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_SUBMIT, 0);
    first = world->save_cursor ? world->save_cursor : world->chunks;
    slot = first;
    do {
        next = slot->next ? slot->next : world->chunks;
        if (slot->dirty && slot->data && !slot->save && !slot->failed) {
            result = maage_world_submit(world, slot, MAAGE_JOB_SAVE, 0);
            if (maage_status_failed(result)) {
                maage_world_failure_status(world, slot, result, "World save submission failed.");
                return result;
            }
            if (maage_status_code(result) != MAAGE_STATUS_SUCCESS) break;
        }
        slot = next;
    } while (slot != first && world->active < world->task_limit && world->saves_active < reserve);
    /* One pass and a persistent cursor: dirty columns eventually receive a
     * writer even during continuous nearby streaming and insertions. */
    world->save_cursor = slot;
    return result;
}
static maage_status maage_world_schedule(maage_world *world) {
    maage_world_slot *slot;
    int radius = maage_world_resident_radius(world) + 1;
    int dx, dz, x, z, kind, ring, no_room = 0;
    size_t span = (size_t) radius * 2U + 1U;
    unsigned limit, reserve, work_limit, budget, examined;
    unsigned priority;
    double forward_x, forward_z;
    maage_status result;
    if (world->failed) return world->stats.last_result;
    reserve = world->task_limit > 1 ? world->task_limit / 8U + (world->task_limit % 8U != 0) : 0;
    work_limit = world->task_limit - reserve;
    if (world->active >= world->task_limit || world->active - world->saves_active >= work_limit) ++world->stats.queue_saturation_events;
    /* A single context cannot reserve a separate save slot. Alternate which
     * class is admitted first when constrained to that explicit capacity. */
    if (world->task_limit == 1 && !world->active) {
        world->save_turn = !world->save_turn;
        if (world->save_turn) {
            result = maage_world_schedule_saves(world, 1);
            if (maage_status_failed(result)) return result;
        }
    }
    limit = (unsigned) (span * span); /* validated at creation */
    budget = world->task_limit > UINT_MAX / 16U ? UINT_MAX : world->task_limit * 16U;
    if (budget < 64U) budget = 64U;
    if (budget > limit) budget = limit;
    if (world->config.presentation_only && budget > 64U) budget = 64U;
    forward_x = sin(world->yaw);
    forward_z = -cos(world->yaw);
    for (examined = 0; examined < budget && world->active - world->saves_active < work_limit && world->active < world->task_limit; ++examined) {
        if (world->scan_ring > radius) {
            world->scan_ring = 0;
            world->scan_step = 0;
        }
        maage_world_scan_next(world, &dx, &dz);
        x = world->center_x + dx;
        z = world->center_z + dz;
        if (abs(x) > MAAGE_WORLD_LIMIT / MAAGE_CHUNK_EDGE - 1 || abs(z) > MAAGE_WORLD_LIMIT / MAAGE_CHUNK_EDGE - 1) continue;
        slot = maage_world_find(world, x, z);
        if (slot && (slot->failed || slot->work)) continue;
        kind = 0;
        if (!slot) {
            if (world->config.presentation_only) continue;
            /* Cache pressure must not stop work on columns already resident. */
            if (no_room) continue;
            if (!maage_world_make_room(world)) {
                no_room = 1;
                continue;
            }
            kind = MAAGE_JOB_LOAD;
        } else if (!slot->data) kind = slot->stage;
        else if (world->config.presentation_only && world->config.build_meshes && !slot->mesh_revision && slot->mesh_dirty && abs(dx) <= world->config.render_distance && abs(dz) <= world->config.render_distance) kind = MAAGE_JOB_MESH;
        else if (slot->light_dirty && (!world->config.presentation_only || !world->visual_light_active)) kind = MAAGE_JOB_LIGHT;
        else if (world->config.build_meshes && (world->config.presentation_only || (slot->light_ready && !slot->light_dirty)) && slot->mesh_dirty && slot->mesh_failed_revision != slot->revision &&
                 abs(dx) <= world->config.render_distance &&
                 abs(dz) <= world->config.render_distance)
            kind = MAAGE_JOB_MESH;
        if (!kind) continue;
        if (!slot) slot = maage_world_insert(world, x, z);
        if (!slot) break;
        ring = abs(dx) > abs(dz) ? abs(dx) : abs(dz);
        priority = (unsigned) ring * 16U;
        if (dx * forward_x + dz * forward_z < 0) priority += 3U;
        if (kind == MAAGE_JOB_LOAD || kind == MAAGE_JOB_GENERATE) priority += 4U;
        else if (kind == MAAGE_JOB_MESH) ++priority;
        result = maage_world_submit(world, slot, kind, priority);
        if (maage_status_failed(result)) {
            maage_world_failure_status(world, slot, result, "World task submission failed.");
            return result;
        }
        if (maage_status_code(result) != MAAGE_STATUS_SUCCESS) break;
    }
    /* Saves are accepted independently of the near-first work quota. A
     * reserved share prevents an endless stream of arrivals from starving
     * durable dirty data or filling all contexts with low-value I/O. */
    result = maage_world_schedule_saves(world, reserve ? reserve : 1U);
    return world->failed ? world->stats.last_result : result;
}
maage_status maage_world_create(const maage_world_config *config, maage_world **output, maage_status_trace *trace) {
    maage_world *world;
    maage_jobs_stats stats;
    unsigned workers, index, cache_limit, job_limit;
    size_t span, halo, footprint, extra, automatic_jobs;
    int axis, resident_radius;
    maage_random_tick_state initial_random;
    maage_generation_policy policy;
    maage_status result;
    if (output) *output = NULL;
    if (!config || !output) return MAAGE_STATUS_TRACE(trace, maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_CREATE, config ? 1 : 0), MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_CREATE);
    if (config->initial_random_ticks && !maage_random_tick_state_valid(config->initial_random_ticks)) return MAAGE_STATUS_TRACE(trace, maage_status_make(MAAGE_STATUS_MALFORMED_INPUT, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_CREATE, 0), MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_CREATE);
    if (config->initial_random_ticks) initial_random = *config->initial_random_ticks;
    else {
        result = maage_random_tick_default(config->seed, &initial_random);
        if (maage_status_failed(result)) return MAAGE_STATUS_TRACE(trace, result, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_CREATE);
    }
    policy = config->generation_policy;
    if (!policy.sea_level && !policy.spawn_search_radius && !policy.spawn_search_step && maage_status_failed(maage_generation_policy_default(&policy))) return MAAGE_STATUS_TRACE(trace, maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_CREATE, 0), MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_CREATE);
    if (!maage_generation_policy_valid(&policy)) return MAAGE_STATUS_TRACE(trace, maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_CREATE, 0), MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_CREATE);
    if (!maage_block_registry_is_finalized(config->registry) ||
        !maage_item_registry_is_finalized(config->items) || !config->biomes ||
        !config->biomes->finalized || config->render_distance < 1 ||
        config->render_distance > MAAGE_WORLD_MAX_RADIUS || config->simulation_distance < 0 ||
        config->simulation_distance > MAAGE_WORLD_MAX_RADIUS) return MAAGE_STATUS_TRACE(trace, maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_CREATE, 0), MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_CREATE);
    if (!maage_world_random_speed_valid(config->simulation_distance, config->random_tick_speed)) goto overflow;
    resident_radius = config->render_distance > config->simulation_distance ? config->render_distance : config->simulation_distance;
    span = (size_t) resident_radius * 2U + 3U;
    if (span > (size_t) UINT_MAX / span) goto overflow;
    footprint = span * span;
    if (config->cache_capacity) cache_limit = config->cache_capacity;
    else {
        halo = span + 2U;
        if (halo > (size_t) UINT_MAX / halo) goto overflow;
        extra = halo * halo;
        if (footprint > (size_t) UINT_MAX - extra) goto overflow;
        cache_limit = (unsigned) (footprint + extra);
    }
    workers = maage_jobs_resolve_workers(config->worker_count);
    if ((size_t) workers > (size_t) - 1 / 4U || (size_t) workers * 4U > (size_t) - 1 - span * 2U) goto overflow;
    automatic_jobs = (size_t) workers * 4U + span * 2U;
    if (automatic_jobs > cache_limit) automatic_jobs = cache_limit;
    job_limit = config->job_capacity ? config->job_capacity : (unsigned) automatic_jobs;
    if (!job_limit || (size_t) job_limit > (size_t) - 1 / sizeof(maage_world_task) ||
        (size_t) job_limit > (size_t) - 1 / sizeof(maage_job_observation) ||
        (size_t) job_limit > (size_t) - 1 / sizeof(unsigned)) goto overflow;
    world = (maage_world *) calloc(1, sizeof(*world));
    if (!world) goto allocation_failed;
    result = maage_entities_create(&world->entities);
    if (maage_status_failed(result)) {
        maage_entities_destroy(world->entities);
        free(world);
        return result;
    }
    world->diagnostics = getenv("MAAGE_WORLD_DIAGNOSTICS") && !strcmp(getenv("MAAGE_WORLD_DIAGNOSTICS"), "1");
    world->config = *config;
    world->config.generation_policy = policy;
    world->leaves_mode = MAAGE_LEAVES_FANCY;
    world->cache_limit = cache_limit;
    world->task_limit = job_limit;
    memcpy(world->spawn, config->position, sizeof(world->spawn));
    if (config->new_world && maage_status_failed(maage_generation_spawn(config->seed, config->biomes, &policy, world->spawn))) {
        maage_entities_destroy(world->entities);
        free(world);
        goto invalid_spawn;
    }
    for (axis = 0; axis < 3; ++axis)
        if (!(world->spawn[axis] > -MAAGE_WORLD_LIMIT && world->spawn[axis] < MAAGE_WORLD_LIMIT)) {
            maage_entities_destroy(world->entities);
            free(world);
            goto invalid_spawn;
        }
    if (!(world->spawn[1] >= 0 && world->spawn[1] < MAAGE_WORLD_LIMIT)) {
        maage_entities_destroy(world->entities);
        free(world);
        goto invalid_spawn;
    }
    world->center_x = maage_world_chunk_coordinate((int) floor(world->spawn[0]));
    world->center_z = maage_world_chunk_coordinate((int) floor(world->spawn[2]));
    world->tasks = (maage_world_task *) calloc(job_limit, sizeof(*world->tasks));
    world->observations = (maage_job_observation *) malloc((size_t) job_limit * sizeof(*world->observations));
    world->free_tasks = (unsigned *) malloc((size_t) job_limit * sizeof(*world->free_tasks));
    if (!world->tasks || !world->observations || !world->free_tasks) {
        free(world->free_tasks);
        free(world->observations);
        free(world->tasks);
        maage_entities_destroy(world->entities);
        free(world);
        goto allocation_failed;
    }
    for (index = 0; index < job_limit; ++index)
        world->free_tasks[index] = index;
    world->free_count = job_limit;
    result = maage_jobs_create(workers, job_limit, &world->jobs, trace);
    if (maage_status_failed(result)) {
        free(world->free_tasks);
        free(world->observations);
        free(world->tasks);
        maage_entities_destroy(world->entities);
        free(world);
        return MAAGE_STATUS_TRACE(trace, result, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_CREATE);
    }
    result = maage_environment_create(config->seed, config->initial_environment, &world->environment);
    if (maage_status_failed(result)) {
        maage_jobs_destroy(world->jobs, NULL);
        free(world->free_tasks);
        free(world->observations);
        free(world->tasks);
        maage_entities_destroy(world->entities);
        free(world);
        return MAAGE_STATUS_TRACE(trace, result, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_CREATE);
    }
    world->random_ticks.state = initial_random;
    world->config.initial_random_ticks = NULL;
    world->config.initial_environment = NULL;
    world->rate_time = maage_time_monotonic_seconds();
    maage_jobs_snapshot(world->jobs, &stats);
    maage_log_info("maage.world.jobs", "Logical CPUs=%u%s; workers=%u; job capacity=%u; cache capacity=%u; immutable commits.", stats.logical_processors, stats.logical_processors ? "" : " (unknown)", stats.workers, job_limit, cache_limit);
    result = config->presentation_only ? maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_CREATE, 0) : maage_world_schedule(world);
    *output = world;
    if (maage_status_failed(result)) {
        if (trace) *trace = world->trace;
        return result;
    }
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_CREATE, 0);
overflow:
    return MAAGE_STATUS_TRACE(trace, maage_status_make(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_CREATE, 0), MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_CREATE);
allocation_failed:
    return MAAGE_STATUS_TRACE(trace, maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_CREATE, 0), MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_CREATE);
invalid_spawn:
    return MAAGE_STATUS_TRACE(trace, maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_CREATE, 0), MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_CREATE);
}
maage_status maage_world_set_leaves_mode(maage_world *world, maage_leaves_mode mode) {
    maage_world_slot *slot;
    if (!world || world->stopped || world->failed || mode < MAAGE_LEAVES_FAST || mode > MAAGE_LEAVES_FANCY) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_SET, 0);
    if (world->leaves_mode == mode) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_SET, 0);
    world->leaves_mode = mode;
    for (slot = world->chunks; slot; slot = slot->next) {
        if (!maage_world_complete(slot)) continue;
        maage_world_invalidate(world, slot, 0);
        /* Pending mesh jobs hold the old mode by value. Cancel without waiting;
         * stale completions cannot publish across the new mesh revision. */
        if (slot->work && slot->work->kind == MAAGE_JOB_MESH) maage_world_cancel(world, slot->work);
    }
    world->scan_ring = 0;
    world->scan_step = 0;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_SET, 0);
}
maage_status maage_world_stream(maage_world *world, double x, double z, double yaw, maage_status_trace *trace) {
    maage_world_slot *slot;
    int cx, cz, old_x, old_z, radius;
    unsigned long failures;
    if (!world || world->stopped || !(x > -MAAGE_WORLD_LIMIT && x < MAAGE_WORLD_LIMIT && z > -MAAGE_WORLD_LIMIT && z < MAAGE_WORLD_LIMIT)) return MAAGE_STATUS_TRACE(trace, maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_UPDATE, 0), MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_UPDATE);
    failures = world->stats.failure_reports;
    cx = maage_world_chunk_coordinate((int) floor(x));
    cz = maage_world_chunk_coordinate((int) floor(z));
    old_x = world->center_x;
    old_z = world->center_z;
    radius = world->config.render_distance;
    world->center_x = cx;
    world->center_z = cz;
    world->yaw = yaw;
    maage_world_collect(world);
    if (cx != old_x || cz != old_z) {
        world->scan_ring = 0;
        world->scan_step = 0;
    }
    if (cx != old_x || cz != old_z) {
        for (slot = world->chunks; slot; slot = slot->next) {
            if (!world->config.presentation_only && maage_world_distance(world, slot) > radius + 1) {
                maage_world_release(slot->sent);
                slot->sent = NULL;
                slot->sent_revision = 0;
            }
            if (maage_world_distance(world, slot) <= radius && (abs(slot->x - old_x) > radius || abs(slot->z - old_z) > radius)) maage_world_invalidate(world, slot, 0);
        }
    }
    if (!world->config.presentation_only || cx != old_x || cz != old_z) maage_world_retire(world);
    maage_world_schedule(world);
    if (world->diagnostics) {
        double now = maage_time_monotonic_seconds();
        if (now >= world->diagnostic_at + 1.0) {
            unsigned blocks = 0, initial = 0, light = 0, mesh = 0, upload = 0;
            for (slot = world->chunks; slot; slot = slot->next) {
                if (!maage_world_complete(slot)) ++blocks;
                else {
                    if (!slot->light_ready) ++initial;
                    if (slot->light_dirty || (slot->work && slot->work->kind == MAAGE_JOB_LIGHT)) ++light;
                    if (slot->mesh_dirty || (slot->work && slot->work->kind == MAAGE_JOB_MESH)) ++mesh;
                    if (slot->pending_mesh) ++upload;
                }
            }
            maage_log_info("maage.world.latency", "owner=%s center=%d,%d resident=%u awaiting_blocks=%u initializing_light=%u awaiting_light=%u awaiting_mesh=%u awaiting_upload=%u accepted=%u stale=%lu cancelled=%lu", world->config.presentation_only ? "client" : "server", cx, cz, world->count, blocks, initial, light, mesh, upload, world->active, world->stats.stale_jobs, world->stats.cancelled_jobs);
            world->diagnostic_at = now;
        }
    }
    if (world->failed || world->stats.failure_reports != failures) {
        if (trace) *trace = world->trace;
        return world->failed ? world->stats.last_result : maage_status_make(MAAGE_STATUS_PARTIAL, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_UPDATE, 0);
    }
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_UPDATE, 0);
}
int maage_world_failed(const maage_world *world) {
    return world && world->failed;
}
maage_status maage_world_status(const maage_world *world) {
    return world ? world->stats.last_result : maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_INITIALIZE, 0);
}
const maage_status_trace *maage_world_trace(const maage_world *world) {
    return world ? &world->trace : NULL;
}
const maage_status_trace *maage_world_last_job_trace(const maage_world *world) {
    return world ? &world->last_job_trace : NULL;
}
static maage_status maage_world_random_ticks(maage_world *world) {
    maage_world_slot *slot;
    const maage_chunk *chunk;
    const maage_block_state_definition *state;
    const maage_block_definition *block;
    maage_environment_state environment;
    maage_status result, outcome = maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_TICK, 0);
    unsigned int attempt, speed = world->config.random_tick_speed;
    unsigned long sample;
    int dx, dz, section, x, y, z, radius = world->config.simulation_distance;
    double sunlight;
    world->stats.random_tick_attempts = world->stats.random_tick_callbacks = 0;
    if (!speed) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_TICK, 0);
    maage_environment_snapshot(world->environment, &environment);
    /* Gameplay daylight uses a 0.5 phase offset and subtracts 0..11 from
     * stored sky light, independently of rendering's ambient floor. */
    sunlight = cos(maage_environment_celestial_angle(&environment) * 6.283185307179586) * 2.0 + 0.5;
    if (sunlight < 0) sunlight = 0;
    if (sunlight > 1) sunlight = 1;
    sunlight *= (1.0 - environment.rain_intensity * 5.0 / 16.0) * (1.0 - environment.thunder_intensity * environment.rain_intensity * 5.0 / 16.0);
    world->random_ticks.sky_subtracted = (unsigned int) ((1.0 - sunlight) * 11.0);
    /* Stable coordinate order avoids cache/hash/worker-completion ordering.
     * Only resident active columns participate; sampling never requests I/O. */
    for (dz = -radius; dz <= radius; ++dz)
        for (dx = -radius; dx <= radius; ++dx) {
            slot = maage_world_find(world, world->center_x + dx, world->center_z + dz);
            if (!maage_world_complete(slot)) continue;
            for (section = 0; section < 16; ++section) {
                chunk = slot->edit ? slot->edit : slot->data->chunk;
                if (!chunk->sections[section] || !chunk->random_tick_counts[section]) continue;
                for (attempt = 0; attempt < speed; ++attempt) {
                    ++world->stats.random_tick_attempts;
                    world->random_ticks.state.selection = (world->random_ticks.state.selection * 3UL + 1013904223UL) & 0xffffffffUL;
                    sample = world->random_ticks.state.selection >> 2;
                    x = slot->x * 16 + (int) (sample & 15UL);
                    z = slot->z * 16 + (int) ((sample >> 8) & 15UL);
                    y = section * 16 + (int) ((sample >> 16) & 15UL);
                    state = maage_block_registry_state(world->config.registry, maage_world_get(world, x, y, z));
                    block = state ? maage_block_registry_block(world->config.registry, state->block_id) : NULL;
                    if (!block) return maage_status_make(MAAGE_STATUS_INVARIANT_VIOLATION, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_TICK, 0);
                    if (!state->random_ticks || !block->random_tick) continue;
                    ++world->stats.random_tick_callbacks;
                    result = block->random_tick(world, x, y, z, state, &world->random_ticks);
                    if (maage_status_failed(result)) return MAAGE_STATUS_PROPAGATE(&world->trace, result, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_TICK);
                    if (maage_status_is_warning(result) && !maage_status_is_warning(outcome)) outcome = result;
                }
            }
        }
    return outcome;
}
maage_status maage_world_tick(maage_world *world) {
    maage_status result, random_result;
    if (!world) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_TICK, 0);
    if (world->failed) return world->stats.last_result;
    if (world->stopped || world->config.presentation_only) return maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_TICK, 0);
    ++world->edit_depth;
    result = maage_environment_tick(world->environment, world, 128U);
    if (!maage_status_failed(result)) {
        random_result = maage_world_random_ticks(world);
        if (maage_status_failed(random_result) || maage_status_is_warning(random_result)) result = random_result;
    }
    if (!--world->edit_depth) maage_world_commit_edits(world);
    return world->failed ? world->stats.last_result : result;
}
void maage_world_environment(const maage_world *world, maage_environment_state *state) {
    if (world && state) maage_environment_snapshot(world->environment, state);
}
maage_status maage_world_random_tick_snapshot(const maage_world *world, maage_random_tick_state *state) {
    if (!world || !state) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_SNAPSHOT, 0);
    *state = world->random_ticks.state;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_SNAPSHOT, 0);
}
void maage_world_get_stats(maage_world *world, maage_world_stats *stats) {
    maage_world_slot *slot;
    const maage_world_task *task;
    maage_jobs_stats jobs;
    unsigned index, count;
    unsigned *queued, *running;
    int section, radius, lower_x, upper_x, lower_z, upper_z, bound;
    size_t pending;
    double now;
    if (!world || !stats) return;
    *stats = world->stats;
    stats->failed = world->failed;
    stats->resident_columns = world->count;
    stats->cache_capacity = world->cache_limit;
    stats->job_capacity = world->task_limit;
    radius = world->config.render_distance + 1;
    bound = MAAGE_WORLD_LIMIT / MAAGE_CHUNK_EDGE - 1;
    lower_x = world->center_x - radius;
    upper_x = world->center_x + radius;
    lower_z = world->center_z - radius;
    upper_z = world->center_z + radius;
    if (lower_x < -bound) lower_x = -bound;
    if (lower_z < -bound) lower_z = -bound;
    if (upper_x > bound) upper_x = bound;
    if (upper_z > bound) upper_z = bound;
    pending = (size_t) (upper_x - lower_x + 1) * (size_t) (upper_z - lower_z + 1);
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
            if (slot->x >= lower_x && slot->x <= upper_x && slot->z >= lower_z && slot->z <= upper_z) --pending;
        }
        if (maage_world_complete(slot) && !slot->light_dirty && (!world->config.build_meshes || !slot->mesh_dirty) && !slot->work) ++stats->ready;
    }
    stats->pending = (unsigned) pending;
    count = 0;
    maage_jobs_observe(world->jobs, &jobs, world->observations, world->task_limit, &count);
    for (index = 0; index < count; ++index) {
        /* Only immutable owner-assigned kind is read, never worker outputs. */
        task = (const maage_world_task *) world->observations[index].context;
        if (task->kind == MAAGE_JOB_GENERATE) {
            ++stats->generation_jobs;
            queued = &stats->generation_queued;
            running = &stats->generation_running;
            if (world->observations[index].stage > 0 && world->observations[index].stage <= 8) ++stats->generation_stage_jobs[world->observations[index].stage - 1];
        } else if (task->kind == MAAGE_JOB_MESH) {
            ++stats->meshing_jobs;
            queued = &stats->meshing_queued;
            running = &stats->meshing_running;
        } else if (task->kind == MAAGE_JOB_LIGHT) {
            ++stats->lighting_jobs;
            queued = &stats->lighting_queued;
            running = &stats->lighting_running;
        } else {
            ++stats->io_jobs;
            queued = &stats->io_queued;
            running = &stats->io_running;
        }
        if (world->observations[index].state == MAAGE_JOB_STATE_QUEUED) ++*queued;
        else if (world->observations[index].state == MAAGE_JOB_STATE_RUNNING) ++*running;
    }
    stats->logical_processors = jobs.logical_processors;
    stats->worker_count = jobs.workers;
    stats->queued_jobs = jobs.queued;
    stats->running_jobs = jobs.running;
    stats->completed_uncommitted_jobs = jobs.ready;
    stats->scheduling_latency_ms = jobs.mean_queue_seconds * 1000.0;
    now = maage_time_monotonic_seconds();
    if (now >= world->rate_time + 0.5) {
        world->generation_rate = world->stats.generated >= world->rate_generated ? (double) (world->stats.generated - world->rate_generated) / (now - world->rate_time) : 0.0;
        world->rate_generated = world->stats.generated;
        world->rate_time = now;
    }
    stats->generated_columns_per_second = world->generation_rate;
}
maage_status maage_world_present_light(const maage_world *world, int x, int y, int z, unsigned char *light) {
    static const int offsets[6][3] = {{-1, 0, 0}, {1, 0, 0}, {0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}};
    maage_world_slot *slot, *adjacent;
    unsigned int cell, side, sky, emission, value;
    unsigned char sample;
    int nx, ny, nz, pending;
    if (!world || !world->config.presentation_only || !light || x <= -MAAGE_WORLD_LIMIT || x >= MAAGE_WORLD_LIMIT || z <= -MAAGE_WORLD_LIMIT || z >= MAAGE_WORLD_LIMIT) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_QUERY, 0);
    if (y >= 256 || y < 0) {
        *light = y >= 256 ? 240 : 0;
        return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_QUERY, 0);
    }
    slot = maage_world_find(world, maage_world_chunk_coordinate(x), maage_world_chunk_coordinate(z));
    if (!maage_world_complete(slot)) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_QUERY, 0);
    *light = maage_chunk_presentation_light(slot->data->chunk, world->config.registry, maage_world_local_coordinate(x), y, maage_world_local_coordinate(z));
    pending = !slot->light_ready || slot->light_dirty || (slot->work && slot->work->kind == MAAGE_JOB_LIGHT);
    cell = (unsigned int) ((y * 16 + maage_world_local_coordinate(z)) * 16 + maage_world_local_coordinate(x));
    if (pending && (slot->light_cells[cell / 8] & (1U << (cell % 8)))) {
        sky = *light >> 4;
        emission = *light & 15;
        for (side = 0; side < 6; ++side) {
            nx = x + offsets[side][0];
            ny = y + offsets[side][1];
            nz = z + offsets[side][2];
            if (ny < 0 || ny >= 256) continue;
            adjacent = maage_world_find(world, maage_world_chunk_coordinate(nx), maage_world_chunk_coordinate(nz));
            if (!maage_world_complete(adjacent)) continue;
            sample = maage_chunk_presentation_light(adjacent->data->chunk, world->config.registry, maage_world_local_coordinate(nx), ny, maage_world_local_coordinate(nz));
            value = sample >> 4;
            if (value && value - 1 > sky) sky = value - 1;
            value = sample & 15;
            if (value && value - 1 > emission) emission = value - 1;
        }
        *light = (unsigned char) ((sky << 4) | emission);
    }
    return maage_status_make(pending ? MAAGE_STATUS_PARTIAL : MAAGE_STATUS_SUCCESS, pending ? MAAGE_CONTEXT_WORLD : 0, MAAGE_OPERATION_QUERY, 0);
}
maage_status maage_world_sample_light(const maage_world *world, int x, int y, int z, unsigned char *light) {
    maage_world_slot *slot;
    if (light) *light = 0;
    if (!world || !light) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_QUERY, 0);
    if (x <= -MAAGE_WORLD_LIMIT || x >= MAAGE_WORLD_LIMIT || z <= -MAAGE_WORLD_LIMIT || z >= MAAGE_WORLD_LIMIT) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_QUERY, 0);
    slot = maage_world_find(world, maage_world_chunk_coordinate(x), maage_world_chunk_coordinate(z));
    if (!maage_world_complete(slot) || !slot->light_ready || slot->light_dirty || slot->edit_light || (slot->work && slot->work->kind == MAAGE_JOB_LIGHT)) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_QUERY, 0);
    if (y >= MAAGE_WORLD_HEIGHT) y = MAAGE_WORLD_HEIGHT - 1;
    *light = maage_chunk_light(slot->data->chunk, maage_world_local_coordinate(x), y, maage_world_local_coordinate(z));
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_QUERY, 0);
}
int maage_world_observe(const maage_world *world, int x, int y, int z, maage_world_observation *observation) {
    maage_world_slot *slot;
    const maage_chunk *chunk;
    const maage_biome_definition *biome;
    int local_x, local_z, section;
    if (!observation) return 0;
    memset(observation, 0, sizeof(*observation));
    if (!world || y < 0 || y >= MAAGE_WORLD_HEIGHT || x <= -MAAGE_WORLD_LIMIT || x >= MAAGE_WORLD_LIMIT || z <= -MAAGE_WORLD_LIMIT || z >= MAAGE_WORLD_LIMIT) return 0;
    slot = maage_world_find(world, maage_world_chunk_coordinate(x), maage_world_chunk_coordinate(z));
    if (!maage_world_complete(slot)) return 0;
    chunk = slot->data->chunk;
    local_x = maage_world_local_coordinate(x);
    local_z = maage_world_local_coordinate(z);
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
    observation->light_available = slot->light_ready && !slot->light_dirty && !(slot->work && slot->work->kind == MAAGE_JOB_LIGHT);
    if (observation->light_available) observation->light = maage_chunk_light(chunk, local_x, y, local_z);
    return 1;
}
unsigned long maage_world_column_revision(const maage_world *world, int x, int z) {
    maage_world_slot *slot = world ? maage_world_find(world, x, z) : NULL;
    return maage_world_complete(slot) ? slot->block_revision : 0;
}
maage_status maage_world_sample_camera(const maage_world *world, int x, int y, int z, maage_world_camera_sample *sample) {
    unsigned long revisions[4];
    int dx, dy, dz, i, changed;
    if (!world || !sample || x < -MAAGE_WORLD_LIMIT + 5 || x > MAAGE_WORLD_LIMIT - 5 ||
        z < -MAAGE_WORLD_LIMIT + 5 || z > MAAGE_WORLD_LIMIT - 5 ||
        y < -MAAGE_WORLD_LIMIT + 5 || y > MAAGE_WORLD_LIMIT - 5)
        return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_SNAPSHOT, 0);
    changed = !sample->valid || sample->x != x || sample->y != y || sample->z != z;
    for (i = 0; i < 4; ++i) {
        revisions[i] = maage_world_column_revision(world,
                                                   maage_world_chunk_coordinate(x + (i % 2 ? 5 : -5)),
                                                   maage_world_chunk_coordinate(z + (i / 2 ? 5 : -5)));
        if (revisions[i] != sample->revisions[i]) changed = 1;
    }
    if (!changed) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_SNAPSHOT, 0);
    memset(sample, 0, sizeof(*sample));
    sample->valid = 1;
    sample->x = x;
    sample->y = y;
    sample->z = z;
    memcpy(sample->revisions, revisions, sizeof(revisions));
    for (dy = -5; dy <= 5; ++dy)
        for (dz = -5; dz <= 5; ++dz)
            for (dx = -5; dx <= 5; ++dx) {
                i = dx + 5 + 11 * (dz + 5) + 121 * (dy + 5);
                if (y + dy < 0) sample->states[i] = MAAGE_STATE_BEDROCK;
                else if (y + dy < MAAGE_WORLD_HEIGHT) sample->states[i] = maage_world_get(world, x + dx, y + dy, z + dz);
            }
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_SNAPSHOT, 0);
}

int maage_world_mesh_current(const maage_world *world, int x, int z, unsigned long revision) {
    maage_world_slot *slot;
    if (!world) return 0;
    slot = maage_world_find(world, x, z);
    return maage_world_complete(slot) && slot->revision == revision && (world->config.presentation_only || !slot->light_dirty) && !slot->mesh_dirty && maage_world_distance(world, slot) <= world->config.render_distance;
}
maage_status maage_world_take_mesh(maage_world *world, maage_world_mesh_update *update) {
    maage_world_slot *slot, *chosen = NULL;
    if (update) memset(update, 0, sizeof(*update));
    if (!world || !update) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_MESH, 0);
    while (world->mesh_head) {
        slot = world->mesh_head;
        world->mesh_head = slot->mesh_next;
        if (!world->mesh_head) world->mesh_tail = NULL;
        slot->mesh_linked = 0;
        slot->mesh_next = NULL;
        if (slot->pending_mesh && maage_world_mesh_current(world, slot->x, slot->z, slot->mesh_revision)) {
            chosen = slot;
            break;
        }
        maage_world_mesh_destroy(slot->pending_mesh);
        slot->pending_mesh = NULL;
    }
    if (chosen) {
        update->x = chosen->x;
        update->z = chosen->z;
        update->revision = chosen->mesh_revision;
        update->state_revision = chosen->mesh_state_revision;
        update->presentation_generation = chosen->mesh_presentation_generation;
        update->completed_at = chosen->mesh_completed_at;
        update->predicted = chosen->mesh_predicted;
        update->mesh = chosen->pending_mesh;
        chosen->pending_mesh = NULL;
    }
    return maage_status_make(chosen ? MAAGE_STATUS_SUCCESS : MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_MESH, 0);
}
int maage_world_chunk_loaded(const maage_world *world, int x, int z) {
    return world && maage_world_complete(maage_world_find(world, x, z));
}
maage_block_state_id maage_world_get(const maage_world *world, int x, int y, int z) {
    maage_world_slot *slot;
    if (!world || y < 0 || y >= MAAGE_WORLD_HEIGHT || x <= -MAAGE_WORLD_LIMIT || x >= MAAGE_WORLD_LIMIT || z <= -MAAGE_WORLD_LIMIT || z >= MAAGE_WORLD_LIMIT) return MAAGE_STATE_AIR;
    slot = maage_world_find(world, maage_world_chunk_coordinate(x), maage_world_chunk_coordinate(z));
    return maage_world_complete(slot) ? maage_chunk_get(slot->edit ? slot->edit : slot->data->chunk, maage_world_local_coordinate(x), y, maage_world_local_coordinate(z))
                                      : MAAGE_STATE_AIR;
}
int maage_world_solid(const maage_world *world, int x, int y, int z) {
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
int maage_world_collision_shape(const maage_world *world, int x, int y, int z, maage_block_shape *shape) {
    const maage_block_state_definition *state;
    const maage_block_definition *block;
    int axis;
    if (!shape) return 0;
    for (axis = 0; axis < 3; ++axis) {
        shape->minimum[axis] = 0.0;
        shape->maximum[axis] = 1.0;
    }
    if (!world || y < 0 || x <= -MAAGE_WORLD_LIMIT || x >= MAAGE_WORLD_LIMIT || z <= -MAAGE_WORLD_LIMIT || z >= MAAGE_WORLD_LIMIT) return 1;
    if (!maage_world_chunk_loaded(world, maage_world_chunk_coordinate(x), maage_world_chunk_coordinate(z))) return 1;
    if (y >= MAAGE_WORLD_HEIGHT) return 0;
    state = maage_block_registry_state(world->config.registry, maage_world_get(world, x, y, z));
    block = state ? maage_block_registry_block(world->config.registry, state->block_id) : NULL;
    if (!block) return 1;
    if (!block->solid) return 0;
    *shape = block->collision_shape;
    return 1;
}
maage_status maage_world_set(maage_world *world, int x, int y, int z, maage_block_state_id expected, maage_block_state_id replacement) {
    maage_world_slot *slot;
    maage_chunk *copy;
    int local_x, local_z, top, leaf_flags_only;
    const maage_block_definition *before, *after;
    const maage_block_state_definition *old_state, *new_state;
    maage_status result;
    if (!world) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_MUTATE, 0);
    if (world->config.presentation_only || world->stopped || world->failed || y < 0 || y >= MAAGE_WORLD_HEIGHT ||
        x <= -MAAGE_WORLD_LIMIT || x >= MAAGE_WORLD_LIMIT ||
        z <= -MAAGE_WORLD_LIMIT || z >= MAAGE_WORLD_LIMIT ||
        !maage_block_registry_state(world->config.registry, replacement) ||
        !maage_block_registry_state(world->config.registry, expected)) {
        ++world->stats.rejected_mutations;
        return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_MUTATE, 0);
    }
    slot = maage_world_find(world, maage_world_chunk_coordinate(x), maage_world_chunk_coordinate(z));
    if (!maage_world_complete(slot)) {
        ++world->stats.rejected_mutations;
        return maage_status_make(MAAGE_STATUS_NOT_FOUND, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_MUTATE, 0);
    }
    if (world->clock > ~0UL - 5UL) {
        maage_world_failure_status(world, slot, maage_status_make(MAAGE_STATUS_INVARIANT_VIOLATION, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_INITIALIZE, 0), "World revision identity exhausted.");
        ++world->stats.rejected_mutations;
        return maage_world_status(world);
    }
    local_x = maage_world_local_coordinate(x);
    local_z = maage_world_local_coordinate(z);
    if (maage_chunk_get(slot->edit ? slot->edit : slot->data->chunk, local_x, y, local_z) != expected) {
        ++world->stats.rejected_mutations;
        return maage_status_make(MAAGE_STATUS_INTERRUPTED, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_MUTATE, MAAGE_DETAIL_STALE);
    }
    if (expected == replacement) return maage_status_make(MAAGE_STATUS_NOOP, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_MUTATE, 0);
    old_state = maage_block_registry_state(world->config.registry, expected);
    new_state = maage_block_registry_state(world->config.registry, replacement);
    before = maage_block_registry_block(world->config.registry, old_state->block_id);
    after = maage_block_registry_block(world->config.registry, new_state->block_id);
    if (!before || !after || (old_state->random_ticks && !(slot->edit ? slot->edit : slot->data->chunk)->random_tick_counts[y / 16])) {
        result = maage_status_make(MAAGE_STATUS_INVARIANT_VIOLATION, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_MUTATE, 0);
        maage_world_failure_status(world, slot, result, "Invalid random-tick section membership.");
        return result;
    }
    leaf_flags_only = old_state->block_id == MAAGE_BLOCK_LEAVES && new_state->block_id == MAAGE_BLOCK_LEAVES && (old_state->metadata & MAAGE_LEAF_SPECIES_MASK) == (new_state->metadata & MAAGE_LEAF_SPECIES_MASK);
    /* Queue allocations precede the state mutation and any reserved drop.
     * Existing fluid/support entries are deduplicated without allocation. */
    ++world->edit_depth;
    if (!leaf_flags_only) {
        result = maage_support_changed(world, x, y, z, replacement);
        if (!maage_status_failed(result)) result = maage_environment_changed(world->environment, world, x, y, z);
        if (maage_status_failed(result)) {
            if (!--world->edit_depth) maage_world_commit_edits(world);
            ++world->stats.rejected_mutations;
            return result;
        }
    }
    result = maage_world_edit(world, slot, &copy);
    if (!maage_status_failed(result)) result = maage_chunk_set(copy, local_x, y, local_z, replacement);
    if (maage_status_failed(result)) {
        maage_world_failure_status(world, slot, result, "World block mutation allocation failed.");
        ++world->stats.rejected_mutations;
        if (!--world->edit_depth) maage_world_commit_edits(world);
        return result;
    }
    slot->edit_states = 1;
    if (old_state->random_ticks) --copy->random_tick_counts[y / 16];
    if (new_state->random_ticks) ++copy->random_tick_counts[y / 16];
    if (before->light_opacity != after->light_opacity || before->emitted_light != after->emitted_light) slot->edit_light = 1;
    if (maage_block_registry_state(world->config.registry, expected)->block_id != maage_block_registry_state(world->config.registry, replacement)->block_id) maage_chunk_block_entity_remove(copy, (unsigned int) ((y * 16 + local_z) * 16 + local_x));
    if (!leaf_flags_only) {
        top = MAAGE_WORLD_HEIGHT;
        while (top > 0 && maage_chunk_get(copy, local_x, top - 1, local_z) == MAAGE_STATE_AIR)
            --top;
        copy->heightmap[local_z * MAAGE_CHUNK_EDGE + local_x] = (unsigned short) top;
        slot->edit_blocks = 1;
    }
    if (!leaf_flags_only) {
        result = maage_environment_changed(world->environment, world, x, y, z);
        if (!maage_status_failed(result)) result = maage_leaves_changed(world, x, y, z, old_state, new_state);
    }
    if (!--world->edit_depth) maage_world_commit_edits(world);
    ++world->stats.mutations;
    return maage_status_failed(result) ? result : maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_MUTATE, 0);
}
static maage_world_slot *maage_world_entity_slot(const maage_world *world, int x, int y, int z) {
    maage_world_slot *slot;
    if (!world || y < 0 || y >= MAAGE_WORLD_HEIGHT || x <= -MAAGE_WORLD_LIMIT || x >= MAAGE_WORLD_LIMIT || z <= -MAAGE_WORLD_LIMIT || z >= MAAGE_WORLD_LIMIT) return NULL;
    slot = maage_world_find(world, maage_world_chunk_coordinate(x), maage_world_chunk_coordinate(z));
    return maage_world_complete(slot) ? slot : NULL;
}
maage_status maage_world_block_entity_set(maage_world *world, int x, int y, int z, unsigned long type, unsigned long version, const void *data, size_t length) {
    maage_world_slot *slot = maage_world_entity_slot(world, x, y, z);
    maage_chunk *chunk;
    unsigned cell;
    maage_status result;
    if (!slot || world->stopped || world->failed || !type || !version || (length && !data) || maage_world_get(world, x, y, z) == MAAGE_STATE_AIR) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_SET, 0);
    result = maage_world_edit(world, slot, &chunk);
    if (maage_status_failed(result)) return result;
    cell = (unsigned) ((y * 16 + maage_world_local_coordinate(z)) * 16 + maage_world_local_coordinate(x));
    result = maage_chunk_block_entity_set(chunk, cell, type, version, data, length);
    if (maage_status_failed(result)) {
        if (!world->edit_depth) maage_world_commit_edits(world);
        return MAAGE_STATUS_PROPAGATE(&world->trace, result, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_SET);
    }
    if (!world->edit_depth) maage_world_commit_edits(world);
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_SET, 0);
}
maage_status maage_world_block_entity_get(const maage_world *world, int x, int y, int z, unsigned long *type, unsigned long *version, void *data, size_t capacity, size_t *length) {
    maage_world_slot *slot = maage_world_entity_slot(world, x, y, z);
    const maage_chunk *chunk;
    const maage_block_entity *entity;
    size_t index;
    unsigned cell;
    if (length) *length = 0;
    if (!slot || !type || !version || !length) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_READ, 0);
    chunk = slot->edit ? slot->edit : slot->data->chunk;
    cell = (unsigned) ((y * 16 + maage_world_local_coordinate(z)) * 16 + maage_world_local_coordinate(x));
    for (index = 0; index < chunk->entity_count; ++index) {
        entity = &chunk->entities[index];
        if (entity->cell != cell) continue;
        *length = entity->length;
        *type = entity->type;
        *version = entity->version;
        if (capacity < entity->length || (entity->length && !data)) return maage_status_make(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_READ, 0);
        if (entity->length) memcpy(data, entity->data, entity->length);
        return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_READ, 0);
    }
    return maage_status_make(MAAGE_STATUS_NOT_FOUND, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_READ, 0);
}
maage_status maage_world_block_entity_remove(maage_world *world, int x, int y, int z) {
    maage_world_slot *slot = maage_world_entity_slot(world, x, y, z);
    maage_chunk *chunk;
    unsigned cell;
    maage_status removed;
    if (!slot || world->stopped || world->failed) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_REMOVE, 0);
    removed = maage_world_edit(world, slot, &chunk);
    if (maage_status_failed(removed)) return removed;
    cell = (unsigned) ((y * 16 + maage_world_local_coordinate(z)) * 16 + maage_world_local_coordinate(x));
    removed = maage_chunk_block_entity_remove(chunk, cell);
    if (!world->edit_depth) maage_world_commit_edits(world);
    return removed;
}
maage_status maage_world_schedule_update(maage_world *world, int x, int y, int z, unsigned int kind, unsigned long due_low, unsigned long due_high) {
    maage_world_slot *slot;
    maage_chunk *chunk;
    maage_scheduled_update update;
    maage_status result;
    if (!world || world->stopped || world->failed || (kind != MAAGE_BLOCK_UPDATE_FLUID && kind != MAAGE_BLOCK_UPDATE_SUPPORT) || due_low > 4294967295UL || due_high > 4294967295UL ||
        y < 0 || y >= MAAGE_WORLD_HEIGHT || x <= -MAAGE_WORLD_LIMIT || x >= MAAGE_WORLD_LIMIT ||
        z <= -MAAGE_WORLD_LIMIT || z >= MAAGE_WORLD_LIMIT) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_SUBMIT, 0);
    slot = maage_world_find(world, maage_world_chunk_coordinate(x), maage_world_chunk_coordinate(z));
    if (!maage_world_complete(slot)) return maage_status_make(MAAGE_STATUS_NOT_FOUND, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_SUBMIT, 0);
    result = maage_world_edit(world, slot, &chunk);
    if (maage_status_failed(result)) return result;
    update.kind = kind;
    update.cell = (unsigned int) ((y * 16 + maage_world_local_coordinate(z)) * 16 + maage_world_local_coordinate(x));
    update.due_low = due_low;
    update.due_high = due_high;
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
maage_status maage_world_take_update(maage_world *world, unsigned long now_low, unsigned long now_high, int *column_x, int *column_z, maage_scheduled_update *update) {
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
        if (!current->update_count) {
            maage_world_update_membership(world, slot);
            continue;
        }
        if (current->updates[0].due_high > now_high || (current->updates[0].due_high == now_high && current->updates[0].due_low > now_low)) continue;
        result = maage_world_edit(world, slot, &chunk);
        if (maage_status_failed(result)) return result;
        result = maage_chunk_take_update(chunk, now_low, now_high, update);
        if (maage_status_code(result) != MAAGE_STATUS_SUCCESS) {
            if (!maage_status_failed(result)) result = maage_status_make(MAAGE_STATUS_INVARIANT_VIOLATION, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_POLL, 0);
            maage_world_failure_status(world, slot, result, "Due block update could not be consumed.");
            return result;
        }
        *column_x = slot->x;
        *column_z = slot->z;
        ++world->stats.block_updates_processed;
        maage_world_update_membership(world, slot);
        if (!world->edit_depth) maage_world_commit_edits(world);
        return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_POLL, 0);
    }
    return maage_status_make(MAAGE_STATUS_NOOP, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_POLL, 0);
}
const maage_block_registry *maage_world_registry(const maage_world *world) {
    return world ? world->config.registry : NULL;
}
const maage_item_registry *maage_world_item_registry(const maage_world *world) {
    return world ? world->config.items : NULL;
}
void maage_world_spawn(const maage_world *world, double feet[3]) {
    if (world && feet) memcpy(feet, world->spawn, sizeof(world->spawn));
}
maage_status maage_world_flush(maage_world *world, maage_status_trace *trace) {
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
    for (slot = world->chunks; slot; slot = slot->next)
        slot->stage = 0;
    for (;;) {
        for (slot = world->chunks; slot; slot = slot->next)
            if (slot->dirty && slot->data && !slot->save && slot->stage != MAAGE_JOB_SAVE) {
                result = maage_world_submit(world, slot, MAAGE_JOB_SAVE, 0);
                if (maage_status_code(result) != MAAGE_STATUS_SUCCESS) {
                    if (maage_status_failed(result) || !world->active) {
                        if (!maage_status_failed(result)) result = maage_status_make(MAAGE_STATUS_INVARIANT_VIOLATION, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_SUBMIT, 0);
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
maage_status maage_world_destroy(maage_world *world) {
    maage_world_slot *slot, *next;
    maage_status result;
    if (!world) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_DESTROY, 0);
    result = maage_world_flush(world, NULL);
    if (!world->stopped) return result;
    result = maage_jobs_destroy(world->jobs, NULL);
    if (maage_status_failed(result)) return result;
    for (slot = world->chunks; slot; slot = next) {
        next = slot->next;
        maage_world_release(slot->data);
        maage_chunk_destroy(slot->edit);
        maage_world_release(slot->sent);
        maage_world_release(slot->authority);
        maage_world_mesh_destroy(slot->pending_mesh);
        free(slot);
    }
    free(world->buckets);
    free(world->free_tasks);
    maage_environment_destroy(world->environment);
    maage_entities_destroy(world->entities);
    free(world->observations);
    free(world->tasks);
    free(world);
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_DESTROY, 0);
}

maage_entities *maage_world_entities(const maage_world *world) {
    return world ? world->entities : NULL;
}

maage_status maage_column_update_destroy(maage_column_update *update) {
    if (!update) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_DESTROY, 0);
    free(update->cells);
    free(update->biomes);
    free(update);
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_DESTROY, 0);
}
maage_status maage_world_next_column(maage_world *world, maage_column_update **output) {
    maage_world_slot *slot, *chosen = NULL;
    maage_column_update *update;
    const maage_chunk *chunk, *before;
    size_t count = 0, index = 0;
    int x, y, z, sy, best = INT_MAX, distance;
    maage_block_state_id state;
    if (output) *output = NULL;
    if (!world || !output || world->config.presentation_only) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_SNAPSHOT, 0);
    for (slot = world->chunks; slot; slot = slot->next) {
        distance = maage_world_distance(world, slot);
        if (maage_world_complete(slot) && distance <= world->config.render_distance + 1 && slot->block_revision != slot->sent_revision && distance < best) {
            chosen = slot;
            best = distance;
        }
    }
    if (!chosen) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_SNAPSHOT, 0);
    chunk = chosen->data->chunk;
    before = chosen->sent ? chosen->sent->chunk : NULL;
    for (sy = 0; sy < 16; ++sy) {
        if (chunk->sections[sy] && before && before->sections[sy] &&
            !memcmp(chunk->sections[sy]->states, before->sections[sy]->states, sizeof(chunk->sections[sy]->states))) continue;
        if (!chunk->sections[sy] && (!before || !before->sections[sy])) continue;
        for (y = sy * 16; y < sy * 16 + 16; ++y)
            for (z = 0; z < 16; ++z)
                for (x = 0; x < 16; ++x) {
                    state = maage_chunk_get(chunk, x, y, z);
                    if (state != (before ? maage_chunk_get(before, x, y, z) : MAAGE_STATE_AIR)) ++count;
                }
    }
    update = (maage_column_update *) calloc(1, sizeof(*update));
    if (!update) return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_SNAPSHOT, 0);
    if (count) update->cells = (maage_column_cell *) malloc(count * sizeof(*update->cells));
    if (!before) update->biomes = (maage_biome_id *) malloc(sizeof(chunk->biomes));
    if ((count && !update->cells) || (!before && !update->biomes)) {
        maage_column_update_destroy(update);
        return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_SNAPSHOT, 0);
    }
    update->x = chosen->x;
    update->z = chosen->z;
    update->revision = chosen->block_revision;
    update->base_revision = chosen->sent_revision;
    update->count = count;
    if (update->biomes) memcpy(update->biomes, chunk->biomes, sizeof(chunk->biomes));
    for (sy = 0; sy < 16; ++sy) {
        if (chunk->sections[sy] && before && before->sections[sy] && !memcmp(chunk->sections[sy]->states, before->sections[sy]->states, sizeof(chunk->sections[sy]->states))) continue;
        if (!chunk->sections[sy] && (!before || !before->sections[sy])) continue;
        for (y = sy * 16; y < sy * 16 + 16; ++y)
            for (z = 0; z < 16; ++z)
                for (x = 0; x < 16; ++x) {
                    state = maage_chunk_get(chunk, x, y, z);
                    if (state != (before ? maage_chunk_get(before, x, y, z) : MAAGE_STATE_AIR)) {
                        update->cells[index].cell = (unsigned int) ((y * 16 + z) * 16 + x);
                        update->cells[index++].state = state;
                    }
                }
    }
    *output = update;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_SNAPSHOT, 0);
}
maage_status maage_world_ack_column(maage_world *world, int x, int z, unsigned long revision) {
    maage_world_slot *slot = world ? maage_world_find(world, x, z) : NULL;
    if (!slot || slot->block_revision != revision) return maage_status_make(MAAGE_STATUS_INTERRUPTED, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_SNAPSHOT, MAAGE_DETAIL_STALE);
    maage_world_release(slot->sent);
    slot->sent = slot->data;
    ++slot->sent->references;
    slot->sent_revision = revision;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_SNAPSHOT, 0);
}
static void maage_world_visual_changed(maage_world *world, maage_world_slot *slot) {
    maage_world_invalidate(world, slot, slot->light_changed);
    slot->light_changed = 0;
    slot->data_epoch = slot->revision;
    maage_world_invalidate_neighbors(world, slot->x, slot->z, 15U);
    maage_world_cancel(world, slot->work);
    world->scan_ring = 0;
    world->scan_step = 0;
}
static maage_status maage_world_visual_cell(const maage_world *world, maage_world_slot *slot, maage_chunk *chunk, unsigned int cell, maage_block_state_id state) {
    maage_status result;
    const maage_block_state_definition *before, *after;
    const maage_block_definition *old_block, *new_block;
    int x = cell % 16, z = (cell / 16) % 16, y = cell / 256, top;
    if (maage_chunk_get(chunk, x, y, z) == state) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_MUTATE, 0);
    before = maage_block_registry_state(world->config.registry, maage_chunk_get(chunk, x, y, z));
    after = maage_block_registry_state(world->config.registry, state);
    old_block = before ? maage_block_registry_block(world->config.registry, before->block_id) : NULL;
    new_block = after ? maage_block_registry_block(world->config.registry, after->block_id) : NULL;
    if (!old_block || !new_block) return maage_status_make(MAAGE_STATUS_INVARIANT_VIOLATION, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_MUTATE, 0);
    result = maage_chunk_set(chunk, x, y, z, state);
    if (maage_status_failed(result)) return result;
    if (old_block->light_opacity != new_block->light_opacity || old_block->emitted_light != new_block->emitted_light) {
        slot->light_cells[cell / 8] |= (unsigned char) (1U << (cell % 8));
        slot->light_changed = 1;
    }
    top = 256;
    while (top > 0 && maage_chunk_get(chunk, x, top - 1, z) == MAAGE_STATE_AIR)
        --top;
    chunk->heightmap[z * 16 + x] = (unsigned short) top;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_MUTATE, 0);
}
maage_status maage_world_apply_column(maage_world *world, const maage_column_update *update) {
    maage_world_slot *slot;
    maage_chunk *authority = NULL, *visual = NULL;
    maage_chunk_snapshot *snapshot = NULL, *auth_snapshot = NULL;
    maage_status result;
    size_t i;
    unsigned int cell;
    if (!world || !world->config.presentation_only || !update || !update->revision ||
        update->count > 65536UL || (update->count && !update->cells) ||
        (!update->base_revision && !update->biomes) || update->x < -MAAGE_WORLD_MAX_RADIUS || update->x > MAAGE_WORLD_MAX_RADIUS ||
        update->z < -MAAGE_WORLD_MAX_RADIUS || update->z > MAAGE_WORLD_MAX_RADIUS) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_SNAPSHOT, 0);
    if (update->base_revision >= update->revision) return maage_status_make(MAAGE_STATUS_MALFORMED_INPUT, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_SNAPSHOT, 0);
    if (update->biomes)
        for (i = 0; i < 256; ++i)
            if (!maage_biome_registry_get(world->config.biomes, update->biomes[i])) return maage_status_make(MAAGE_STATUS_MALFORMED_INPUT, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_SNAPSHOT, 0);
    slot = maage_world_find(world, update->x, update->z);
    if (slot && slot->block_revision >= update->revision) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_SNAPSHOT, 0);
    if (update->base_revision && (!slot || slot->block_revision != update->base_revision)) return maage_status_make(MAAGE_STATUS_MALFORMED_INPUT, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_SNAPSHOT, MAAGE_DETAIL_STALE);
    if (!slot) {
        if (!maage_world_make_room(world)) return maage_status_make(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_SNAPSHOT, 0);
        slot = maage_world_insert(world, update->x, update->z);
        if (!slot) return maage_world_status(world);
    }
    result = update->base_revision ? maage_chunk_clone(slot->authority->chunk, &authority) : maage_chunk_create(update->x, update->z, &authority);
    if (maage_status_failed(result)) goto done;
    if (update->biomes) memcpy(authority->biomes, update->biomes, sizeof(authority->biomes));
    for (i = 0; i < update->count; ++i) {
        cell = update->cells[i].cell;
        if (cell > 65535U || (i && cell <= update->cells[i - 1].cell) || !maage_block_registry_state(world->config.registry, update->cells[i].state)) {
            result = maage_status_make(MAAGE_STATUS_MALFORMED_INPUT, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_SNAPSHOT, 0);
            goto done;
        }
        result = maage_chunk_set(authority, cell % 16, cell / 256, (cell / 16) % 16, update->cells[i].state);
        if (maage_status_failed(result)) goto done;
    }
    /* Keep locally calculated light and its direct-sky cache across deltas. */
    result = slot->data && update->base_revision ? maage_chunk_clone(slot->data->chunk, &visual) : maage_chunk_clone(authority, &visual);
    if (maage_status_failed(result)) goto done;
    if (!update->base_revision) {
        int sy, x, z, top;
        for (sy = 0; sy < 16; ++sy)
            if (visual->sections[sy]) memset(visual->sections[sy]->light, 0, 4096);
        slot->light_changed = 1;
        memset(slot->light_cells, 255, sizeof(slot->light_cells));
        for (z = 0; z < 16; ++z)
            for (x = 0; x < 16; ++x) {
                top = 256;
                while (top > 0 && maage_chunk_get(visual, x, top - 1, z) == MAAGE_STATE_AIR)
                    --top;
                visual->heightmap[z * 16 + x] = (unsigned short) top;
            }
    }
    if (update->biomes) memcpy(visual->biomes, update->biomes, sizeof(visual->biomes));
    for (i = 0; i < update->count; ++i) {
        result = maage_world_visual_cell(world, slot, visual, update->cells[i].cell, update->cells[i].state);
        if (maage_status_failed(result)) goto done;
    }
    for (i = 0; i < world->prediction_count; ++i)
        if (maage_world_chunk_coordinate(world->predictions[i].x) == slot->x && maage_world_chunk_coordinate(world->predictions[i].z) == slot->z) {
            int x = maage_world_local_coordinate(world->predictions[i].x), z = maage_world_local_coordinate(world->predictions[i].z), y = world->predictions[i].y;
            if (maage_chunk_get(authority, x, y, z) == world->prediction_states[i]) {
                result = maage_world_visual_cell(world, slot, visual, (unsigned int) ((y * 16 + z) * 16 + x), MAAGE_STATE_AIR);
                if (maage_status_failed(result)) goto done;
            }
        }
    result = maage_world_snapshot(authority, &auth_snapshot);
    if (maage_status_failed(result)) goto done;
    authority = NULL;
    result = maage_world_snapshot(visual, &snapshot);
    if (maage_status_failed(result)) goto done;
    visual = NULL;
    maage_world_release(slot->authority);
    slot->authority = auth_snapshot;
    auth_snapshot = NULL;
    maage_world_release(slot->data);
    slot->data = snapshot;
    snapshot = NULL;
    slot->block_revision = update->revision;
    slot->dirty = 0;
    slot->stage = 0;
    maage_world_visual_changed(world, slot);
    result = maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_SNAPSHOT, 0);
done:
    maage_world_release(snapshot);
    maage_world_release(auth_snapshot);
    maage_chunk_destroy(authority);
    maage_chunk_destroy(visual);
    return result;
}
maage_status maage_world_present_predictions(maage_world *world, const maage_block_position *positions, const maage_block_state_id *expected, size_t count, unsigned long generation) {
    maage_world_slot *slot;
    maage_chunk *copy;
    maage_chunk_snapshot *snapshot;
    maage_status result;
    size_t i, j, total;
    maage_block_position all[64];
    if (!world || !world->config.presentation_only || count > 32 || (count && (!positions || !expected))) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_PRESENT, 0);
    for (i = 0; i < count; ++i)
        if (positions[i].y < 0 || positions[i].y >= 256 ||
            positions[i].x <= -MAAGE_WORLD_LIMIT || positions[i].x >= MAAGE_WORLD_LIMIT ||
            positions[i].z <= -MAAGE_WORLD_LIMIT || positions[i].z >= MAAGE_WORLD_LIMIT ||
            !maage_block_registry_state(world->config.registry, expected[i])) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_PRESENT, 0);
    if (world->prediction_generation == generation) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_PRESENT, 0);
    total = world->prediction_count + count;
    memcpy(all, world->predictions, world->prediction_count * sizeof(*all));
    if (count) memcpy(all + world->prediction_count, positions, count * sizeof(*all));
    for (i = 0; i < total; ++i) {
        maage_block_state_id desired;
        unsigned int cell;
        for (j = 0; j < i; ++j)
            if (all[j].x == all[i].x && all[j].y == all[i].y && all[j].z == all[i].z) break;
        if (j < i) continue;
        slot = maage_world_find(world, maage_world_chunk_coordinate(all[i].x), maage_world_chunk_coordinate(all[i].z));
        if (!slot || !slot->authority) continue;
        cell = (unsigned int) ((all[i].y * 16 + maage_world_local_coordinate(all[i].z)) * 16 + maage_world_local_coordinate(all[i].x));
        desired = maage_chunk_get(slot->authority->chunk, cell % 16, cell / 256, (cell / 16) % 16);
        for (j = 0; j < count; ++j)
            if (positions[j].x == all[i].x && positions[j].y == all[i].y && positions[j].z == all[i].z && expected[j] == desired) {
                desired = MAAGE_STATE_AIR;
                break;
            }
        if (maage_chunk_get(slot->data->chunk, cell % 16, cell / 256, (cell / 16) % 16) == desired) continue;
        result = maage_chunk_clone(slot->data->chunk, &copy);
        if (maage_status_failed(result)) return result;
        result = maage_world_visual_cell(world, slot, copy, cell, desired);
        if (maage_status_failed(result)) {
            maage_chunk_destroy(copy);
            return result;
        }
        result = maage_world_snapshot(copy, &snapshot);
        if (maage_status_failed(result)) {
            maage_chunk_destroy(copy);
            return result;
        }
        maage_world_release(slot->data);
        slot->data = snapshot;
        maage_world_visual_changed(world, slot);
    }
    world->prediction_count = count;
    world->prediction_generation = generation;
    if (count) {
        memcpy(world->predictions, positions, count * sizeof(*positions));
        memcpy(world->prediction_states, expected, count * sizeof(*expected));
    }
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_PRESENT, 0);
}

unsigned long maage_world_presentation_revision(const maage_world *world) {
    return world ? world->clock : 0;
}
int maage_world_present_inputs(const maage_world *world, int x, int z, const maage_chunk **chunk, const maage_chunk *neighbors[8], const maage_block_registry **blocks, const maage_biome_registry **biomes) {
    maage_world_slot *slot, *adjacent[8];
    int side;
    if (!world || !world->config.presentation_only) return 0;
    slot = maage_world_find(world, maage_world_chunk_coordinate(x), maage_world_chunk_coordinate(z));
    if (!maage_world_complete(slot)) return 0;
    *chunk = slot->data->chunk;
    *blocks = world->config.registry;
    *biomes = world->config.biomes;
    maage_world_neighbors(world, slot->x, slot->z, adjacent);
    for (side = 0; side < 8; ++side)
        neighbors[side] = adjacent[side] ? adjacent[side]->data->chunk : NULL;
    return 1;
}
