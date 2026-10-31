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
const maage_item_stack *maage_entity_stack(const maage_entity *entity) {
    if (!entity) return NULL;
    return entity->type == MAAGE_ENTITY_ITEM ? &entity->payload.item : (entity->type == MAAGE_ENTITY_FALLING_BLOCK ? &entity->payload.falling.stack : NULL);
}
int maage_entity_valid(const maage_entity *entity, const maage_item_registry *items) {
    const maage_item_stack *stack = maage_entity_stack(entity);
    const maage_item_definition *item;
    maage_block_state_id state;
    unsigned int axis;
    if (!entity || !entity->id || entity->id >= 4294967295UL || !stack ||
        entity->age >= 6000 || entity->pickup_delay > 32767 ||
        !stack->count || !maage_item_stack_valid(items, stack) ||
        (entity->grounded != 0 && entity->grounded != 1)) return 0;
    if (entity->type == MAAGE_ENTITY_FALLING_BLOCK) {
        item = maage_item_registry_get(items, stack->item_id);
        if (stack->count != 1 || !item || !item->has_block || maage_status_failed(maage_item_placement_state(item, stack->metadata, MAAGE_FACE_POSITIVE_Y, 0, &state)) || state != entity->payload.falling.state) return 0;
        if (entity->payload.falling.source[1] < 0 || entity->payload.falling.source[1] >= MAAGE_WORLD_HEIGHT) return 0;
        for (axis = 0; axis < 3; ++axis)
            if (entity->payload.falling.source[axis] <= -MAAGE_WORLD_LIMIT || entity->payload.falling.source[axis] >= MAAGE_WORLD_LIMIT) return 0;
    }
    for (axis = 0; axis < 3; ++axis)
        if (!(entity->position[axis] > -MAAGE_WORLD_LIMIT && entity->position[axis] < MAAGE_WORLD_LIMIT) ||
            !(entity->previous[axis] > -MAAGE_WORLD_LIMIT && entity->previous[axis] < MAAGE_WORLD_LIMIT) ||
            !(entity->velocity[axis] >= -4 && entity->velocity[axis] <= 4)) return 0;
    return entity->position[1] >= -64 && entity->position[1] <= MAAGE_WORLD_HEIGHT + 64;
}

maage_status maage_entities_fall(maage_world *world, int x, int y, int z, maage_block_state_id state_id) {
    maage_entities *entities = maage_world_entities(world);
    const maage_item_registry *items = maage_world_item_registry(world);
    const maage_block_state_definition *state = maage_block_registry_state(maage_world_registry(world), state_id);
    const maage_block_definition *block = state ? maage_block_registry_block(maage_world_registry(world), state->block_id) : NULL;
    maage_entity entity;
    maage_status result;
    if (!entities || !block || !block->falls) return entity_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_CREATE);
    memset(&entity, 0, sizeof(entity));
    entity.id = entities->next_id;
    entity.type = MAAGE_ENTITY_FALLING_BLOCK;
    entity.position[0] = x + 0.5;
    entity.position[1] = y + 0.01;
    entity.position[2] = z + 0.5;
    memcpy(entity.previous, entity.position, sizeof(entity.previous));
    entity.payload.falling.state = state_id;
    entity.payload.falling.stack.item_id = block->drop_item_id;
    entity.payload.falling.stack.count = 1;
    entity.payload.falling.stack.metadata = state->metadata;
    entity.payload.falling.source[0] = x;
    entity.payload.falling.source[1] = y;
    entity.payload.falling.source[2] = z;
    entity.payload.falling.source_revision = maage_world_column_revision(world, maage_world_chunk_coordinate(x), maage_world_chunk_coordinate(z));
    if (!maage_entity_valid(&entity, items)) return entity_status(MAAGE_STATUS_INVARIANT_VIOLATION, MAAGE_OPERATION_CREATE);
    result = maage_entities_reserve(entities, 1);
    if (maage_status_failed(result)) return result;
    result = maage_world_set(world, x, y, z, state_id, MAAGE_STATE_AIR);
    if (maage_status_code(result) != MAAGE_STATUS_SUCCESS) return result;
    entities->data[entities->count++] = entity;
    ++entities->next_id;
    return entity_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_CREATE);
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
            if (!maage_world_chunk_loaded(world, maage_world_chunk_coordinate((int) floor(entity->position[0] + x * (entity->type == MAAGE_ENTITY_FALLING_BLOCK ? 0.491 : 0.126))), maage_world_chunk_coordinate((int) floor(entity->position[2] + z * (entity->type == MAAGE_ENTITY_FALLING_BLOCK ? 0.491 : 0.126))))) return 0;
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
static maage_status entity_fall_move(maage_entity *entity, maage_world *world) {
    const maage_block_state_definition *state, *under;
    const maage_block_definition *block;
    maage_item_stack drop;
    double vertical;
    int x, y, z;
    unsigned int axis;
    maage_status result;
    entity->velocity[1] -= 0.04;
    vertical = entity->velocity[1];
    entity->grounded = maage_world_sweep_body(world, entity->position, entity->velocity, 0.98, 0.98, 1, vertical) && vertical < 0;
    maage_world_sweep_body(world, entity->position, entity->velocity, 0.98, 0.98, 0, entity->velocity[0]);
    maage_world_sweep_body(world, entity->position, entity->velocity, 0.98, 0.98, 2, entity->velocity[2]);
    for (axis = 0; axis < 3; ++axis)
        entity->velocity[axis] *= 0.98;
    x = (int) floor(entity->position[0]);
    y = (int) floor(entity->position[1] + 0.01);
    z = (int) floor(entity->position[2]);
    if (!entity->grounded && entity->age <= 600 && !(entity->age > 100 && (y < 1 || y > 256))) return entity_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_TICK);
    if (entity->grounded && y >= 0 && y < MAAGE_WORLD_HEIGHT) {
        state = maage_block_registry_state(maage_world_registry(world), maage_world_get(world, x, y, z));
        under = maage_block_registry_state(maage_world_registry(world), maage_world_get(world, x, y - 1, z));
        block = state ? maage_block_registry_block(maage_world_registry(world), state->block_id) : NULL;
        if (!block || !under) return entity_status(MAAGE_STATUS_INVARIANT_VIOLATION, MAAGE_OPERATION_LOOKUP);
        if (block->replaceable && under->block_id != MAAGE_BLOCK_AIR && !under->fluid_kind) {
            result = maage_world_set(world, x, y, z, state->id, entity->payload.falling.state);
            if (maage_status_failed(result)) return result; /* Retain the entity on failed publication. */
            if (maage_status_code(result) == MAAGE_STATUS_SUCCESS) {
                entity->type = 0;
                return result;
            }
        }
    }
    /* Failed landing transfers ownership in place: no allocation, lost drop or
     * extra instance can result, and ordinary item collection begins next step. */
    drop = entity->payload.falling.stack;
    memset(&entity->payload, 0, sizeof(entity->payload));
    entity->payload.item = drop;
    entity->type = MAAGE_ENTITY_ITEM;
    entity->age = 0;
    entity->pickup_delay = 10;
    entity->velocity[1] = 0;
    return entity_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_TICK);
}

static void entity_compact(maage_entities *entities) {
    size_t i, output = 0;
    /* Preserve identity order, including after a partially successful step. */
    for (i = 0; i < entities->count; ++i)
        if (entities->data[i].type == MAAGE_ENTITY_FALLING_BLOCK || (entities->data[i].type == MAAGE_ENTITY_ITEM && entities->data[i].payload.item.count))
            entities->data[output++] = entities->data[i];
    entities->count = output;
}

maage_status maage_entities_tick(maage_entities *entities, maage_world *world, const maage_item_registry *items, const maage_player *player, maage_inventory *inventory, int collect, unsigned int *collected, maage_entity_pickup *pickups, size_t capacity, size_t *written) {
    size_t i, j, step, bucket;
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
        if (entity->type == MAAGE_ENTITY_FALLING_BLOCK) {
            result = entity_fall_move(entity, world);
            if (maage_status_failed(result)) {
                entity_compact(entities);
                return result;
            }
            continue;
        }
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
        if (entity->type != MAAGE_ENTITY_ITEM || !entity->payload.item.count || !entity_loaded(world, entity)) continue;
        item = maage_item_registry_get(items, entity->payload.item.item_id);
        if (!item) {
            entity_compact(entities);
            return entity_status(MAAGE_STATUS_INVARIANT_VIOLATION, MAAGE_OPERATION_LOOKUP);
        }
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
                            if (j <= i || other->type != MAAGE_ENTITY_ITEM || !other->payload.item.count ||
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
            if (maage_status_failed(result)) {
                entity_compact(entities);
                return result;
            }
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
    entity_compact(entities);
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
