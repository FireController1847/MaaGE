#include "world_internal.h"
#include "maage/world_store.h"
#include "maage/thread.h"
#include "maage/log.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define MAAGE_CHUNK_BUCKETS 2048
#define MAAGE_JOB_LOAD 1
#define MAAGE_JOB_LIGHT 2
#define MAAGE_JOB_MESH 3
#define MAAGE_JOB_EVICT 4

typedef struct maage_chunk_bucket { maage_chunk *chunk; int occupied; } maage_chunk_bucket;
struct maage_world {
    maage_world_config config;
    maage_mutex *mutex;
    maage_condition *condition;
    maage_thread *worker;
    maage_chunk_bucket buckets[MAAGE_CHUNK_BUCKETS];
    maage_chunk *chunks;
    unsigned int count;
    int center_x, center_z, stop, failed;
    double yaw, spawn[3];
    maage_world_stats stats;
};

static size_t maage_world_bucket(int x, int z)
{
    unsigned long hash = (unsigned long)x * 0x8da6b343UL ^ (unsigned long)z * 0xd8163841UL;
    hash ^= hash >> 16;
    return (size_t)(hash & (MAAGE_CHUNK_BUCKETS - 1));
}
/* Integer-keyed open addressing: expected O(1), not a worst-case guarantee.
 * Called under mutex, or on the sole worker while membership is unchanged. */
static maage_chunk *maage_world_find(const maage_world *world, int x, int z)
{
    size_t bucket = maage_world_bucket(x, z), probes;
    const maage_chunk_bucket *entry;
    for (probes = 0; probes < MAAGE_CHUNK_BUCKETS; ++probes) {
        entry = &world->buckets[bucket];
        if (!entry->occupied) return NULL;
        if (entry->chunk && entry->chunk->x == x && entry->chunk->z == z) return entry->chunk;
        bucket = (bucket + 1) & (MAAGE_CHUNK_BUCKETS - 1);
    }
    return NULL;
}
static int maage_world_insert(maage_world *world, maage_chunk *chunk)
{
    size_t bucket = maage_world_bucket(chunk->x, chunk->z), probes, vacant = MAAGE_CHUNK_BUCKETS;
    maage_chunk_bucket *entry;
    if (world->count >= MAAGE_WORLD_MAX_CHUNKS) return 0;
    for (probes = 0; probes < MAAGE_CHUNK_BUCKETS; ++probes) {
        entry = &world->buckets[bucket];
        if (!entry->chunk && vacant == MAAGE_CHUNK_BUCKETS) vacant = bucket;
        if (!entry->occupied) break;
        bucket = (bucket + 1) & (MAAGE_CHUNK_BUCKETS - 1);
    }
    if (vacant == MAAGE_CHUNK_BUCKETS) return 0;
    world->buckets[vacant].chunk = chunk; world->buckets[vacant].occupied = 1;
    chunk->next = world->chunks; world->chunks = chunk; ++world->count;
    return 1;
}
static void maage_world_remove(maage_world *world, maage_chunk *chunk)
{
    size_t bucket = maage_world_bucket(chunk->x, chunk->z), probes, destination;
    maage_chunk *displaced;
    maage_chunk **link = &world->chunks;
    for (probes = 0; probes < MAAGE_CHUNK_BUCKETS; ++probes) {
        if (world->buckets[bucket].chunk == chunk) {
            world->buckets[bucket].chunk = NULL; world->buckets[bucket].occupied = 0; break;
        }
        bucket = (bucket + 1) & (MAAGE_CHUNK_BUCKETS - 1);
    }
    /* Reinsert this probe cluster rather than accumulating tombstones during
     * long journeys. Missing lookups keep an empty terminator at low load. */
    bucket = (bucket + 1) & (MAAGE_CHUNK_BUCKETS - 1);
    while (world->buckets[bucket].occupied) {
        displaced = world->buckets[bucket].chunk;
        world->buckets[bucket].chunk = NULL; world->buckets[bucket].occupied = 0;
        destination = maage_world_bucket(displaced->x, displaced->z);
        while (world->buckets[destination].occupied) destination = (destination + 1) & (MAAGE_CHUNK_BUCKETS - 1);
        world->buckets[destination].chunk = displaced; world->buckets[destination].occupied = 1;
        bucket = (bucket + 1) & (MAAGE_CHUNK_BUCKETS - 1);
    }
    while (*link && *link != chunk) link = &(*link)->next;
    if (*link) *link = chunk->next;
    --world->count;
}
static int maage_chunk_complete(const maage_chunk *chunk)
{
    return chunk && chunk->state >= MAAGE_CHUNK_GENERATED && !chunk->failed;
}
static int maage_world_distance(const maage_world *world, const maage_chunk *chunk)
{
    int x = abs(chunk->x - world->center_x), z = abs(chunk->z - world->center_z);
    return x > z ? x : z;
}
static void maage_world_neighbors(const maage_world *world, const maage_chunk *chunk, const maage_chunk *neighbors[4])
{
    static const int offsets[4][2] = {{-1,0},{1,0},{0,-1},{0,1}};
    int side;
    for (side = 0; side < 4; ++side) {
        neighbors[side] = maage_world_find(world, chunk->x + offsets[side][0], chunk->z + offsets[side][1]);
        if (!maage_chunk_complete(neighbors[side])) neighbors[side] = NULL;
    }
}
static void maage_world_invalidate_neighbors(maage_world *world, maage_chunk *chunk)
{
    const maage_chunk *neighbors[4];
    maage_chunk *neighbor;
    int side;
    maage_world_neighbors(world, chunk, neighbors);
    for (side = 0; side < 4; ++side) {
        neighbor = (maage_chunk *)neighbors[side];
        if (neighbor) { neighbor->mesh_dirty = 1; neighbor->light_dirty = 1; }
    }
}
static void maage_world_failure(maage_world *world, maage_chunk *chunk, const char *error)
{
    maage_mutex_lock(world->mutex);
    world->failed = 1;
    if (chunk) { chunk->failed = 1; chunk->state = MAAGE_CHUNK_FAILED; }
    maage_mutex_unlock(world->mutex);
    maage_log_error("maage.world", "%s", error);
}
static int maage_world_save(maage_world *world, maage_chunk *chunk)
{
    char error[256];
    if (!chunk->dirty || chunk->failed) return 1;
    if (world->config.store && !maage_world_store_save_chunk(world->config.store, chunk,
        world->config.registry, world->config.biomes, error)) {
        maage_world_failure(world, NULL, error); return 0;
    }
    maage_mutex_lock(world->mutex); chunk->dirty = 0; maage_mutex_unlock(world->mutex);
    return 1;
}
/* Bounded desired grid doubles as the request queue: one producer/worker, no
 * duplicate requests, at most 19x19 desired columns including a lighting halo.
 * Score near/view-facing work first. Scan only on the worker, not every frame. */
static maage_chunk *maage_world_next(maage_world *world, int *job)
{
    maage_chunk *chunk, *chosen = NULL;
    int radius = world->config.render_distance + 1;
    int dx, dz, x, z, candidate, best = 2147483647, score;
    double forward_x = sin(world->yaw), forward_z = -cos(world->yaw);
    for (chunk = world->chunks; chunk; chunk = chunk->next) {
        if (maage_world_distance(world, chunk) > radius + 1) { *job = MAAGE_JOB_EVICT; return chunk; }
    }
    *job = 0; x = z = 0;
    for (dz = -radius; dz <= radius; ++dz) for (dx = -radius; dx <= radius; ++dx) {
        if (abs(world->center_x + dx) > MAAGE_WORLD_LIMIT / 16 - 1 ||
            abs(world->center_z + dz) > MAAGE_WORLD_LIMIT / 16 - 1) continue;
        chunk = maage_world_find(world, world->center_x + dx, world->center_z + dz);
        candidate = 0;
        score = (dx * dx + dz * dz) * 8;
        if (dx * forward_x + dz * forward_z < 0) score += 3;
        if (!chunk) { candidate = MAAGE_JOB_LOAD; score += 4; }
        else if (maage_chunk_complete(chunk)) {
            if (chunk->light_dirty) candidate = MAAGE_JOB_LIGHT;
            else if (chunk->mesh_dirty && abs(dx) <= world->config.render_distance && abs(dz) <= world->config.render_distance) {
                candidate = MAAGE_JOB_MESH; score += 1;
            }
        }
        if (candidate && score < best) { best = score; chosen = chunk; *job = candidate; x = dx; z = dz; }
    }
    if (*job == MAAGE_JOB_LOAD) {
        chosen = maage_chunk_create(world->center_x + x, world->center_z + z);
        if (!chosen || !maage_world_insert(world, chosen)) {
            maage_chunk_destroy(chosen); world->failed = 1; *job = 0; return NULL;
        }
        chosen->state = MAAGE_CHUNK_LOADING;
    }
    return chosen;
}
static void maage_world_load(maage_world *world, maage_chunk *slot)
{
    maage_chunk *loaded = NULL;
    char error[256];
    int result = 0;
    if (world->config.store) result = maage_world_store_load_chunk(world->config.store, slot->x, slot->z,
        world->config.registry, world->config.biomes, &loaded, error);
    if (result < 0) { maage_world_failure(world, slot, error); return; }
    if (!result) {
        loaded = maage_chunk_create(slot->x, slot->z);
        maage_mutex_lock(world->mutex); slot->state = MAAGE_CHUNK_GENERATING; maage_mutex_unlock(world->mutex);
        if (!loaded || !maage_generate_chunk(loaded, world->config.seed, world->config.registry, world->config.biomes)) {
            maage_chunk_destroy(loaded); maage_world_failure(world, slot, "Chunk generation failed."); return;
        }
        loaded->dirty = 1;
    }
    /* Transfer complete arrays into the permanent table slot. No incomplete
     * generated arrays are ever visible to collision queries. */
    maage_mutex_lock(world->mutex);
    memcpy(slot->sections, loaded->sections, sizeof(slot->sections));
    memset(loaded->sections, 0, sizeof(loaded->sections));
    memcpy(slot->biomes, loaded->biomes, sizeof(slot->biomes));
    memcpy(slot->heightmap, loaded->heightmap, sizeof(slot->heightmap));
    slot->dirty = loaded->dirty; slot->light_dirty = 1; slot->mesh_dirty = 1;
    slot->state = MAAGE_CHUNK_GENERATED;
    if (result) ++world->stats.loaded_from_disk; else ++world->stats.generated;
    maage_world_invalidate_neighbors(world, slot);
    maage_mutex_unlock(world->mutex);
    maage_chunk_destroy(loaded);
}
static void maage_world_worker(void *context)
{
    maage_world *world = (maage_world *)context;
    maage_chunk *chunk;
    const maage_chunk *neighbors[4];
    maage_world_mesh *mesh, *previous;
    maage_section *added[16];
    int job, changed, section;
    for (;;) {
        maage_mutex_lock(world->mutex);
        if (world->stop) { maage_mutex_unlock(world->mutex); break; }
        chunk = world->failed ? NULL : maage_world_next(world, &job);
        if (!chunk) {
            maage_condition_wait(world->condition, world->mutex);
            maage_mutex_unlock(world->mutex); continue;
        }
        maage_world_neighbors(world, chunk, neighbors);
        if (job == MAAGE_JOB_LIGHT) { chunk->light_dirty = 0; chunk->state = MAAGE_CHUNK_LIGHTING; }
        if (job == MAAGE_JOB_MESH) chunk->mesh_dirty = 0;
        if (job == MAAGE_JOB_EVICT) chunk->state = MAAGE_CHUNK_SAVING;
        maage_mutex_unlock(world->mutex);
        if (job == MAAGE_JOB_LOAD) maage_world_load(world, chunk);
        else if (job == MAAGE_JOB_LIGHT) {
            changed = maage_light_chunk(chunk, world->config.registry, neighbors, added);
            if (changed < 0) {
                for (section = 0; section < 16; ++section) free(added[section]);
                maage_world_failure(world, chunk, "Chunk lighting allocation failed."); continue;
            }
            maage_mutex_lock(world->mutex);
            for (section = 0; section < 16; ++section)
                if (added[section]) chunk->sections[section] = added[section];
            chunk->state = MAAGE_CHUNK_GENERATED;
            if (changed) { chunk->dirty = 1; chunk->mesh_dirty = 1; maage_world_invalidate_neighbors(world, chunk); }
            maage_mutex_unlock(world->mutex);
        } else if (job == MAAGE_JOB_MESH) {
            mesh = maage_world_mesh_create_chunk(world->config.registry, world->config.biomes, chunk, neighbors);
            if (!mesh) { maage_world_failure(world, chunk, "Chunk CPU mesh allocation failed."); continue; }
            maage_mutex_lock(world->mutex);
            previous = chunk->pending_mesh; chunk->pending_mesh = mesh;
            ++chunk->revision; ++world->stats.meshes; chunk->state = MAAGE_CHUNK_MESH_PENDING;
            maage_mutex_unlock(world->mutex);
            maage_world_mesh_destroy(previous);
        } else if (job == MAAGE_JOB_EVICT) {
            if (!maage_world_save(world, chunk)) continue;
            maage_mutex_lock(world->mutex);
            maage_world_remove(world, chunk); chunk->state = MAAGE_CHUNK_UNLOADING;
            maage_world_invalidate_neighbors(world, chunk);
            maage_mutex_unlock(world->mutex);
            maage_chunk_destroy(chunk);
        }
    }
    for (chunk = world->chunks; chunk; chunk = chunk->next) maage_world_save(world, chunk);
}
maage_world *maage_world_create(const maage_world_config *config)
{
    maage_world *world;
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
    if (!(world->spawn[1] >= 0 && world->spawn[1] <= MAAGE_WORLD_HEIGHT + 16)) { free(world); return NULL; }
    world->center_x = maage_world_chunk_coordinate((int)floor(world->spawn[0]));
    world->center_z = maage_world_chunk_coordinate((int)floor(world->spawn[2]));
    world->mutex = maage_mutex_create(); world->condition = maage_condition_create();
    if (!world->mutex || !world->condition) { maage_world_destroy(world); return NULL; }
    world->worker = maage_thread_start(maage_world_worker, world);
    if (!world->worker) { maage_world_destroy(world); return NULL; }
    return world;
}
void maage_world_stream(maage_world *world, double x, double z, double yaw)
{
    maage_chunk *chunk;
    int cx, cz, old_x, old_z, radius;
    if (!world || !(x > -MAAGE_WORLD_LIMIT && x < MAAGE_WORLD_LIMIT && z > -MAAGE_WORLD_LIMIT && z < MAAGE_WORLD_LIMIT)) return;
    cx = maage_world_chunk_coordinate((int)floor(x)); cz = maage_world_chunk_coordinate((int)floor(z));
    maage_mutex_lock(world->mutex);
    old_x = world->center_x; old_z = world->center_z; radius = world->config.render_distance;
    if (cx != old_x || cz != old_z) {
        world->center_x = cx; world->center_z = cz;
        for (chunk = world->chunks; chunk; chunk = chunk->next)
            if (maage_world_distance(world, chunk) <= radius &&
                (abs(chunk->x - old_x) > radius || abs(chunk->z - old_z) > radius)) chunk->mesh_dirty = 1;
        maage_condition_signal(world->condition);
    }
    world->yaw = yaw;
    maage_mutex_unlock(world->mutex);
}
void maage_world_get_stats(maage_world *world, maage_world_stats *stats)
{
    maage_chunk *chunk;
    int x, z, radius;
    if (!world || !stats) return;
    maage_mutex_lock(world->mutex);
    *stats = world->stats; stats->loaded = stats->pending = stats->ready = 0; stats->failed = world->failed;
    for (chunk = world->chunks; chunk; chunk = chunk->next) {
        if (maage_chunk_complete(chunk)) ++stats->loaded;
        if (chunk->state == MAAGE_CHUNK_READY || chunk->state == MAAGE_CHUNK_MESH_PENDING) ++stats->ready;
    }
    radius = world->config.render_distance + 1;
    for (z = -radius; z <= radius; ++z) for (x = -radius; x <= radius; ++x) {
        if (abs(world->center_x + x) > MAAGE_WORLD_LIMIT / 16 - 1 ||
            abs(world->center_z + z) > MAAGE_WORLD_LIMIT / 16 - 1) continue;
        if (!maage_chunk_complete(maage_world_find(world, world->center_x + x, world->center_z + z))) ++stats->pending;
    }
    maage_mutex_unlock(world->mutex);
}
int maage_world_take_mesh(maage_world *world, maage_world_mesh_update *update)
{
    maage_chunk *chunk, *chosen = NULL;
    int best = 2147483647, distance;
    if (!world || !update) return 0;
    maage_mutex_lock(world->mutex);
    for (chunk = world->chunks; chunk; chunk = chunk->next) {
        distance = maage_world_distance(world, chunk);
        if (chunk->pending_mesh && distance <= world->config.render_distance && distance < best) { chosen = chunk; best = distance; }
    }
    if (chosen) {
        update->x = chosen->x; update->z = chosen->z; update->revision = chosen->revision;
        update->mesh = chosen->pending_mesh; chosen->pending_mesh = NULL;
        if (chosen->state == MAAGE_CHUNK_MESH_PENDING && !chosen->light_dirty && !chosen->mesh_dirty)
            chosen->state = MAAGE_CHUNK_READY;
    }
    maage_mutex_unlock(world->mutex);
    return chosen != NULL;
}
int maage_world_chunk_loaded(maage_world *world, int x, int z)
{
    int complete;
    if (!world) return 0;
    maage_mutex_lock(world->mutex); complete = maage_chunk_complete(maage_world_find(world, x, z)); maage_mutex_unlock(world->mutex);
    return complete;
}
maage_block_state_id maage_world_get(const maage_world *source, int x, int y, int z)
{
    maage_world *world = (maage_world *)source;
    maage_chunk *chunk;
    maage_block_state_id state = MAAGE_STATE_AIR;
    if (!world || y < 0 || y >= MAAGE_WORLD_HEIGHT || x <= -MAAGE_WORLD_LIMIT || x >= MAAGE_WORLD_LIMIT || z <= -MAAGE_WORLD_LIMIT || z >= MAAGE_WORLD_LIMIT) return state;
    maage_mutex_lock(world->mutex);
    chunk = maage_world_find(world, maage_world_chunk_coordinate(x), maage_world_chunk_coordinate(z));
    if (maage_chunk_complete(chunk)) state = maage_chunk_get(chunk, maage_world_local_coordinate(x), y, maage_world_local_coordinate(z));
    maage_mutex_unlock(world->mutex);
    return state;
}
int maage_world_solid(const maage_world *source, int x, int y, int z)
{
    maage_world *world = (maage_world *)source;
    maage_chunk *chunk;
    const maage_block_state_definition *state;
    const maage_block_definition *block;
    int solid = 1;
    if (!world || y < 0 || x <= -MAAGE_WORLD_LIMIT || x >= MAAGE_WORLD_LIMIT || z <= -MAAGE_WORLD_LIMIT || z >= MAAGE_WORLD_LIMIT) return 1;
    maage_mutex_lock(world->mutex);
    chunk = maage_world_find(world, maage_world_chunk_coordinate(x), maage_world_chunk_coordinate(z));
    if (maage_chunk_complete(chunk)) {
        state = maage_block_registry_state(world->config.registry, maage_chunk_get(chunk, maage_world_local_coordinate(x), y, maage_world_local_coordinate(z)));
        block = state ? maage_block_registry_block(world->config.registry, state->block_id) : NULL;
        solid = !block || block->solid;
    }
    maage_mutex_unlock(world->mutex);
    return solid;
}
const maage_block_registry *maage_world_registry(const maage_world *world) { return world ? world->config.registry : NULL; }
void maage_world_spawn(const maage_world *world, double feet[3])
{
    if (world && feet) memcpy(feet, world->spawn, sizeof(world->spawn));
}
int maage_world_flush(maage_world *world)
{
    if (!world) return 1;
    if (world->worker) {
        maage_mutex_lock(world->mutex); world->stop = 1; maage_condition_signal(world->condition); maage_mutex_unlock(world->mutex);
        maage_thread_join(world->worker); world->worker = NULL;
    }
    return !world->failed;
}
void maage_world_destroy(maage_world *world)
{
    maage_chunk *chunk, *next;
    if (!world) return;
    maage_world_flush(world);
    for (chunk = world->chunks; chunk; chunk = next) { next = chunk->next; maage_chunk_destroy(chunk); }
    maage_condition_destroy(world->condition); maage_mutex_destroy(world->mutex);
    free(world);
}
