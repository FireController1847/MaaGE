#include "maage/entities.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

struct maage_entities {
    maage_entity *data;
    size_t count, capacity, *heads, *next, buckets, merge_phase;
    unsigned long next_id, random;
};
static maage_status entity_status(unsigned char code, unsigned char operation) {
    return maage_status_make(code, MAAGE_CONTEXT_WORLD, operation, 0);
}
maage_status maage_entities_create(maage_entities **output) {
    if (!output) return entity_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_CREATE);
    *output = (maage_entities *) calloc(1, sizeof(**output));
    if (!*output) return entity_status(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_CREATE);
    (*output)->next_id = 1;
    (*output)->random = 0x78412aUL;
    return entity_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_CREATE);
}
maage_status maage_entities_destroy(maage_entities *entities) {
    if (!entities) return entity_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_DESTROY);
    free(entities->data);
    free(entities->heads);
    free(entities->next);
    free(entities);
    return entity_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_DESTROY);
}
maage_status maage_entities_reserve(maage_entities *entities, size_t additional) {
    maage_entity *data;
    size_t capacity, buckets, *heads, *next;
    if (!entities) return entity_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_ACQUIRE);
    if (additional > 4294967295UL - entities->next_id || additional > (size_t) - 1 - entities->count) return entity_status(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_OPERATION_ACQUIRE);
    if (entities->count + additional <= entities->capacity) return entity_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_ACQUIRE);
    capacity = entities->capacity ? entities->capacity : 64;
    while (capacity < entities->count + additional) {
        if (capacity > (size_t) - 1 / 2) return entity_status(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_OPERATION_ACQUIRE);
        capacity *= 2;
    }
    if (capacity > (size_t) - 1 / sizeof(*data) || capacity > ((size_t) - 1 / sizeof(*heads) - 1) / 2) return entity_status(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_OPERATION_ACQUIRE);
    buckets = capacity * 2 + 1;
    data = (maage_entity *) malloc(capacity * sizeof(*data));
    heads = (size_t *) malloc(buckets * sizeof(*heads));
    next = (size_t *) malloc(capacity * sizeof(*next));
    if (!data || !heads || !next) {
        free(data);
        free(heads);
        free(next);
        return entity_status(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_ACQUIRE);
    }
    if (entities->count) memcpy(data, entities->data, entities->count * sizeof(*data));
    free(entities->data);
    free(entities->heads);
    free(entities->next);
    entities->data = data;
    entities->heads = heads;
    entities->next = next;
    entities->capacity = capacity;
    entities->buckets = buckets;
    return entity_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_ACQUIRE);
}
int maage_entity_valid(const maage_entity *entity, const maage_item_registry *items) {
    unsigned int axis;
    if (!entity || !entity->id || entity->id >= 4294967295UL || entity->type != MAAGE_ENTITY_ITEM ||
        entity->age >= 6000 || entity->pickup_delay > 32767 ||
        !entity->payload.item.count || !maage_item_stack_valid(items, &entity->payload.item) ||
        (entity->grounded != 0 && entity->grounded != 1)) return 0;
    for (axis = 0; axis < 3; ++axis)
        if (!(entity->position[axis] > -MAAGE_WORLD_LIMIT && entity->position[axis] < MAAGE_WORLD_LIMIT) ||
            !(entity->previous[axis] > -MAAGE_WORLD_LIMIT && entity->previous[axis] < MAAGE_WORLD_LIMIT) ||
            !(entity->velocity[axis] >= -4 && entity->velocity[axis] <= 4)) return 0;
    return entity->position[1] >= -64 && entity->position[1] <= MAAGE_WORLD_HEIGHT + 64;
}
maage_status maage_entities_spawn(maage_entities *entities, const maage_item_registry *items, const maage_item_stack *stack, const double position[3], const double velocity[3], unsigned int delay) {
    maage_entity entity;
    if (!entities || !position || !velocity || !stack) return entity_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_CREATE);
    memset(&entity, 0, sizeof(entity));
    entity.id = entities->next_id;
    entity.type = MAAGE_ENTITY_ITEM;
    entity.payload.item = *stack;
    entity.pickup_delay = delay;
    memcpy(entity.position, position, sizeof(entity.position));
    memcpy(entity.previous, position, sizeof(entity.previous));
    memcpy(entity.velocity, velocity, sizeof(entity.velocity));
    if (!maage_entity_valid(&entity, items)) return entity_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_CREATE);
    if (entities->count == entities->capacity) return entity_status(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_OPERATION_CREATE);
    entities->data[entities->count++] = entity;
    ++entities->next_id;
    return entity_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_CREATE);
}
static size_t entity_bucket(const maage_entities *entities, int x, int y, int z) {
    unsigned long hash = (unsigned long) x * 0x8da6b343UL ^ (unsigned long) y * 0x9e3779b9UL ^ (unsigned long) z * 0xd8163841UL;
    return (size_t) (hash ^ (hash >> 16)) % entities->buckets;
}
static int entity_loaded(const maage_world *world, const maage_entity *entity) {
    int x, z;
    /* Freeze before a boundary until every column intersecting the AABB loads. */
    for (z = -1; z <= 1; z += 2)
        for (x = -1; x <= 1; x += 2)
            if (!maage_world_chunk_loaded(world, maage_world_chunk_coordinate((int) floor(entity->position[0] + x * 0.126)), maage_world_chunk_coordinate((int) floor(entity->position[2] + z * 0.126)))) return 0;
    return 1;
}
static void entity_move(maage_entity *entity, const maage_world *world) {
    const maage_block_state_definition *state;
    double vertical, drag, minimum[3], maximum[3], current[3];
    unsigned int axis;
    for (axis = 0; axis < 3; ++axis) {
        minimum[axis] = entity->position[axis] - (axis == 1 ? 0 : 0.125);
        maximum[axis] = minimum[axis] + 0.25;
    }
    /* Item water motion retains ordinary gravity/drag and has no buoyancy.
     * Observe the whole small body, including a current across a cell edge. */
    if (maage_world_fluid_volume(world, minimum, maximum, MAAGE_FLUID_WATER, current))
        for (axis = 0; axis < 3; ++axis)
            entity->velocity[axis] += current[axis] * MAAGE_PLAYER_CURRENT_IMPULSE;
    entity->velocity[1] -= 0.04;
    vertical = entity->velocity[1];
    entity->grounded = maage_world_sweep_body(world, entity->position, entity->velocity, 0.25, 0.25, 1, vertical) && vertical < 0;
    maage_world_sweep_body(world, entity->position, entity->velocity, 0.25, 0.25, 0, entity->velocity[0]);
    maage_world_sweep_body(world, entity->position, entity->velocity, 0.25, 0.25, 2, entity->velocity[2]);
    drag = entity->grounded ? 0.588 : 0.98;
    if (entity->grounded) {
        state = maage_block_registry_state(maage_world_registry(world), maage_world_get(world, (int) floor(entity->position[0]), (int) floor(entity->position[1] - 0.01), (int) floor(entity->position[2])));
        if (state && state->block_id == MAAGE_BLOCK_ICE) drag = 0.9604;
    }
    /* A clipped vertical sweep has already removed impact velocity. Preserve
     * that rest state rather than reflecting the pre-collision request. */
    entity->velocity[1] *= 0.98;
    entity->velocity[0] *= drag;
    entity->velocity[2] *= drag;
    if (fabs(entity->velocity[0]) < 0.0001) entity->velocity[0] = 0;
    if (fabs(entity->velocity[2]) < 0.0001) entity->velocity[2] = 0;
}
maage_status maage_entities_tick(maage_entities *entities, const maage_world *world, const maage_item_registry *items, const maage_player *player, maage_inventory *inventory, int collect, unsigned int *collected, maage_entity_pickup *pickups, size_t capacity, size_t *written) {
    size_t i, j, step, bucket, output = 0;
    int x, y, z, dx, dy, dz;
    unsigned int remaining, examined;
    maage_entity *entity, *other;
    const maage_item_definition *item;
    maage_status result;
    if (!entities || !world || !items || !player || !inventory || !collected || !written || (capacity && !pickups)) return entity_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_TICK);
    *collected = 0;
    *written = 0;
    if (!entities->count) return entity_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_TICK);
    for (i = 0; i < entities->buckets; ++i)
        entities->heads[i] = (size_t) - 1;
    entities->merge_phase %= entities->count;
    for (step = 0; step < entities->count; ++step) {
        /* Rotate bucket insertion priority so a dense pile's bounded candidate
         * window cannot permanently starve older compatible stacks. */
        i = step >= entities->count - entities->merge_phase ? step - (entities->count - entities->merge_phase) : step + entities->merge_phase;
        entity = &entities->data[i];
        memcpy(entity->previous, entity->position, sizeof(entity->previous));
        if (!entity_loaded(world, entity)) continue;
        if (entity->pickup_delay) --entity->pickup_delay;
        ++entity->age;
        entity_move(entity, world);
        if (entity->age >= 6000 || entity->position[1] < -64 || fabs(entity->position[0]) >= MAAGE_WORLD_LIMIT - 4 || fabs(entity->position[2]) >= MAAGE_WORLD_LIMIT - 4) {
            entity->payload.item.count = 0;
            continue;
        }
        bucket = entity_bucket(entities, (int) floor(entity->position[0]), (int) floor(entity->position[1]), (int) floor(entity->position[2]));
        entities->next[i] = entities->heads[bucket];
        entities->heads[bucket] = i;
    }
    ++entities->merge_phase;
    /* A reusable spatial hash and a fixed candidate budget keep dense piles
     * linear in entity count, rather than quadratic pairwise work. */
    for (i = 0; i < entities->count; ++i) {
        entity = &entities->data[i];
        if (!entity->payload.item.count || !entity_loaded(world, entity)) continue;
        item = maage_item_registry_get(items, entity->payload.item.item_id);
        if (!item) return entity_status(MAAGE_STATUS_INVARIANT_VIOLATION, MAAGE_OPERATION_LOOKUP);
        x = (int) floor(entity->position[0]);
        y = (int) floor(entity->position[1]);
        z = (int) floor(entity->position[2]);
        examined = 0;
        if (entity->age % 5 == 0)
            for (dz = -1; dz <= 1; ++dz)
                for (dy = -1; dy <= 1; ++dy)
                    for (dx = -1; dx <= 1; ++dx) {
                        bucket = entity_bucket(entities, x + dx, y + dy, z + dz);
                        for (j = entities->heads[bucket]; j != (size_t) - 1 && examined < 128; j = entities->next[j], ++examined) {
                            other = &entities->data[j];
                            if (j <= i || !other->payload.item.count ||
                                other->payload.item.item_id != entity->payload.item.item_id ||
                                other->payload.item.metadata != entity->payload.item.metadata ||
                                other->payload.item.damage != entity->payload.item.damage ||
                                other->payload.item.count + entity->payload.item.count > item->max_stack ||
                                fabs(other->position[0] - entity->position[0]) > 0.75 ||
                                fabs(other->position[1] - entity->position[1]) > 0.25 ||
                                fabs(other->position[2] - entity->position[2]) > 0.75) continue;
                            entity->payload.item.count += other->payload.item.count;
                            if (other->age < entity->age) entity->age = other->age;
                            if (other->pickup_delay > entity->pickup_delay) entity->pickup_delay = other->pickup_delay;
                            other->payload.item.count = 0;
                        }
                    }
        /* EntityPlayer expands its body by (1, .5, 1), then intersects the
         * item's .25-wide AABB. No pre-collection magnetic force exists. */
        if (collect && *written < capacity && !entity->pickup_delay &&
            fabs(player->position[0] - entity->position[0]) < MAAGE_PLAYER_WIDTH * 0.5 + 1.125 &&
            fabs(player->position[2] - entity->position[2]) < MAAGE_PLAYER_WIDTH * 0.5 + 1.125 &&
            entity->position[1] + 0.25 > player->position[1] - 0.5 && entity->position[1] < player->position[1] + MAAGE_PLAYER_HEIGHT + 0.5) {
            result = maage_inventory_add(inventory, items, &entity->payload.item, &remaining);
            if (maage_status_failed(result)) return result;
            if (remaining != entity->payload.item.count) {
                maage_entity_pickup *pickup = &pickups[(*written)++];
                pickup->entity = entity->id;
                pickup->stack = entity->payload.item;
                pickup->stack.count -= remaining;
                memcpy(pickup->position, entity->position, sizeof(pickup->position));
                pickup->age = entity->age;
                *collected += pickup->stack.count;
                entity->payload.item.count = remaining;
            }
        }
    }
    /* Preserve increasing identity order for binary search in presentation. */
    for (i = 0; i < entities->count; ++i)
        if (entities->data[i].payload.item.count)
            entities->data[output++] = entities->data[i];
    entities->count = output;
    return entity_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_TICK);
}
size_t maage_entities_count(const maage_entities *entities) {
    return entities ? entities->count : 0;
}
const maage_entity *maage_entities_at(const maage_entities *entities, size_t index) {
    return entities && index < entities->count ? &entities->data[index] : NULL;
}
maage_status maage_entity_save_clear(maage_entity_save *save) {
    if (!save) return entity_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_DESTROY);
    free(save->entities);
    memset(save, 0, sizeof(*save));
    return entity_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_DESTROY);
}
maage_status maage_entities_snapshot(const maage_entities *entities, maage_entity_save *save) {
    if (!entities || !save) return entity_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_SNAPSHOT);
    memset(save, 0, sizeof(*save));
    if (entities->count) {
        save->entities = (maage_entity *) malloc(entities->count * sizeof(*save->entities));
        if (!save->entities) return entity_status(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_SNAPSHOT);
        memcpy(save->entities, entities->data, entities->count * sizeof(*save->entities));
    }
    save->count = entities->count;
    save->next_id = entities->next_id;
    save->random = entities->random;
    return entity_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_SNAPSHOT);
}
maage_status maage_entities_restore(maage_entities *entities, const maage_item_registry *items, const maage_entity_save *save) {
    size_t i;
    maage_status result;
    unsigned long previous = 0;
    if (!entities || !save || !save->next_id || save->next_id > 4294967295UL || (save->count && !save->entities)) return entity_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_LOAD);
    for (i = 0; i < save->count; ++i) {
        if (!maage_entity_valid(&save->entities[i], items) || save->entities[i].id <= previous || save->entities[i].id >= save->next_id) return entity_status(MAAGE_STATUS_MALFORMED_INPUT, MAAGE_OPERATION_LOAD);
        previous = save->entities[i].id;
    }
    result = maage_entities_reserve(entities, save->count > entities->count ? save->count - entities->count : 0);
    if (maage_status_failed(result)) return result;
    if (save->count) memcpy(entities->data, save->entities, save->count * sizeof(*entities->data));
    entities->count = save->count;
    entities->next_id = save->next_id;
    entities->random = save->random;
    return entity_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_LOAD);
}

unsigned long maage_entities_random(const maage_entities *entities) {
    return entities ? entities->random : 0;
}
maage_status maage_entities_set_random(maage_entities *entities, unsigned long random) {
    if (!entities) return entity_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_SET);
    entities->random = random & 4294967295UL;
    return entity_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_SET);
}
