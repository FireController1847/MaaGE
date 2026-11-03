#include "maage/entities.h"
#include "maage/gameplay.h"
#include "maage/environment.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

struct maage_entities {
    maage_entity *data;
    size_t count, capacity, *heads, *next, buckets, merge_phase;
    unsigned long next_id, random, behavior_random, spawn_random, simulation_tick;
    const maage_entity_type_registry *types;
    maage_entity_policy policy;
    size_t simulation_cursor;
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
    (*output)->behavior_random = 0x56c31fUL;
    (*output)->spawn_random = 0x891a2bUL;
    maage_entity_policy_init(&(*output)->policy);
    return entity_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_CREATE);
}
maage_status maage_entities_destroy(maage_entities *entities) {
    size_t i;
    if (!entities) return entity_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_DESTROY);
    for (i = 0; i < entities->count; ++i)
        free(entities->data[i].state);
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
    if (entities->count) {
        memcpy(data, entities->data, entities->count * sizeof(*data));
        memcpy(next, entities->next, entities->count * sizeof(*next));
    }
    free(entities->data);
    free(entities->heads);
    free(entities->next);
    entities->data = data;
    entities->heads = heads;
    entities->next = next;
    entities->capacity = capacity;
    entities->buckets = buckets;
    {
        size_t i, bucket;
        unsigned long hash;
        for (i = 0; i < buckets; ++i)
            heads[i] = (size_t) - 1;
        for (i = 0; i < entities->count; ++i) {
            if (data[i].type != MAAGE_ENTITY_LIVING) continue;
            hash = (unsigned long) ((int) floor(data[i].position[0])) * 0x8da6b343UL ^ (unsigned long) ((int) floor(data[i].position[1])) * 0x9e3779b9UL ^ (unsigned long) ((int) floor(data[i].position[2])) * 0xd8163841UL;
            bucket = (size_t) (hash ^ (hash >> 16)) % buckets;
            next[i] = heads[bucket];
            heads[bucket] = i;
        }
    }
    return entity_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_ACQUIRE);
}
const maage_item_stack *maage_entity_stack(const maage_entity *entity) {
    if (!entity) return NULL;
    return entity->type == MAAGE_ENTITY_ITEM ? &entity->payload.item : (entity->type == MAAGE_ENTITY_FALLING_BLOCK ? &entity->payload.falling.stack : NULL);
}
/* Item variants project a subset of a block state's properties. A unique match
 * retains exact falling geometry without allocating a per-entity property map. */
static maage_status falling_variant(const maage_block_registry *blocks, const maage_item_registry *items, const maage_block_state_definition *state, maage_item_id item_id, maage_item_variant_id *output) {
    const maage_item_definition *item = maage_item_registry_get(items, item_id);
    const maage_block_definition *block = state ? maage_block_registry_block(blocks, state->block_id) : NULL;
    const maage_traits_registry *traits = maage_item_registry_traits(items);
    const maage_item_variant_definition *row;
    const maage_property_set *properties, *constants;
    maage_property_value value;
    size_t variant, property, phase, matches = 0;
    int equal;
    if (!blocks || !state || !block || !block->falls || block->drop_item_id != item_id || !item || !item->has_block || item->block_id != state->block_id || !output) return entity_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_RESOLVE);
    constants = maage_property_set_get(traits, maage_traits_constants(traits, MAAGE_TRAIT_ITEM, item_id));
    if (!constants) return entity_status(MAAGE_STATUS_INVARIANT_VIOLATION, MAAGE_OPERATION_RESOLVE);
    for (variant = 0; variant < item->variant_count; ++variant) {
        row = maage_item_variant_get(items, item_id, (maage_item_variant_id) variant);
        properties = row ? maage_property_set_get(traits, row->properties) : NULL;
        if (!properties) return entity_status(MAAGE_STATUS_INVARIANT_VIOLATION, MAAGE_OPERATION_RESOLVE);
        equal = 1;
        for (phase = 0; phase < 2 && equal; ++phase) {
            if (phase) properties = constants;
            for (property = 0; property < properties->count; ++property)
                if (!maage_block_state_value(blocks, state, properties->values[property].property, &value) || value != properties->values[property].value) {
                    equal = 0;
                    break;
                }
        }
        if (equal) {
            *output = (maage_item_variant_id) variant;
            ++matches;
        }
    }
    return entity_status(matches == 1 ? MAAGE_STATUS_SUCCESS : (matches ? MAAGE_STATUS_INVALID_STATE : MAAGE_STATUS_NOT_FOUND), MAAGE_OPERATION_RESOLVE);
}
int maage_entity_valid(const maage_entity *entity, const maage_item_registry *items) {
    const maage_item_stack *stack = maage_entity_stack(entity);
    const maage_item_definition *item;
    maage_item_variant_id variant;
    const maage_block_registry *blocks;
    const maage_block_state_definition *state;
    unsigned int axis;
    if (!entity || !entity->id || entity->id >= 4294967295UL || entity->age > 4294967295UL || entity->pickup_delay > 32767 || (entity->grounded != 0 && entity->grounded != 1)) return 0;
    if (entity->type == MAAGE_ENTITY_ITEM || entity->type == MAAGE_ENTITY_FALLING_BLOCK) {
        if (entity->registered_type || entity->state || entity->state_size || entity->age >= 6000 || !stack || !stack->count || !maage_item_stack_valid(items, stack)) return 0;
    } else if (entity->type == MAAGE_ENTITY_LIVING) {
        if (!entity->registered_type || entity->registered_type >= MAAGE_CONTENT_ID_INVALID || entity->payload.living.air > 300 || entity->payload.living.damage_sequence > 4294967295UL || !(entity->payload.living.head_yaw >= -1e9 && entity->payload.living.head_yaw <= 1e9) || !(entity->payload.living.damage_yaw >= -1e9 && entity->payload.living.damage_yaw <= 1e9) || entity->payload.living.variant > 4294967295UL || entity->payload.living.flags > 4294967295UL || entity->payload.living.target > 4294967295UL || entity->payload.living.attacker > 4294967295UL || entity->payload.living.idle_ticks > 4294967295UL || entity->payload.living.effect_cursor > 4294967295UL || entity->payload.living.effect_limit > 4294967295UL || !maage_item_stack_valid(items, &entity->payload.living.pending_drop) || !(entity->payload.living.yaw >= -1e9 && entity->payload.living.yaw <= 1e9) || !(entity->payload.living.pitch >= -1.6 && entity->payload.living.pitch <= 1.6) || !(entity->payload.living.walk_distance >= 0 && entity->payload.living.walk_distance <= 1e15) || !(entity->payload.living.fall_distance >= 0 && entity->payload.living.fall_distance <= 1e9)) return 0;
        for (axis = 0; axis < 3; ++axis)
            if (!(entity->payload.living.goal[axis] > -MAAGE_WORLD_LIMIT && entity->payload.living.goal[axis] < MAAGE_WORLD_LIMIT)) return 0;
    } else if (entity->type == MAAGE_ENTITY_PROJECTILE) {
        if (!entity->registered_type || entity->registered_type >= MAAGE_CONTENT_ID_INVALID || entity->payload.projectile.owner > 4294967295UL || !entity->payload.projectile.damage || !maage_item_stack_valid(items, &entity->payload.projectile.recovery)) return 0;
    } else return 0;
    if (entity->type == MAAGE_ENTITY_FALLING_BLOCK) {
        item = maage_item_registry_get(items, stack->item_id);
        blocks = maage_item_registry_blocks(items);
        state = maage_block_registry_state(blocks, entity->payload.falling.state);
        if (stack->count != 1 || !item || maage_status_failed(falling_variant(blocks, items, state, stack->item_id, &variant)) || variant != stack->variant) return 0;
        if (entity->payload.falling.source[1] < 0 || entity->payload.falling.source[1] >= MAAGE_WORLD_HEIGHT) return 0;
        for (axis = 0; axis < 3; ++axis)
            if (entity->payload.falling.source[axis] <= -MAAGE_WORLD_LIMIT || entity->payload.falling.source[axis] >= MAAGE_WORLD_LIMIT) return 0;
    }
    for (axis = 0; axis < 3; ++axis)
        if (!(entity->position[axis] > -MAAGE_WORLD_LIMIT && entity->position[axis] < MAAGE_WORLD_LIMIT) || !(entity->previous[axis] > -MAAGE_WORLD_LIMIT && entity->previous[axis] < MAAGE_WORLD_LIMIT) || !(entity->velocity[axis] >= -4 && entity->velocity[axis] <= 4)) return 0;
    return entity->position[1] >= -64 && entity->position[1] <= MAAGE_WORLD_HEIGHT + 64;
}
int maage_entity_registered_valid(const maage_entity *entity, const maage_item_registry *items, const maage_entity_type_registry *types) {
    const maage_entity_type_definition *definition;
    if (!maage_entity_valid(entity, items)) return 0;
    if (!entity->registered_type) return 1;
    definition = maage_entity_type_get(types, entity->registered_type);
    if (!definition || definition->state_size != entity->state_size || (entity->state_size && !entity->state) || (!entity->state_size && entity->state)) return 0;
    if ((entity->type == MAAGE_ENTITY_PROJECTILE) != (definition->classification == MAAGE_ENTITY_CLASS_PROJECTILE)) return 0;
    if (entity->type == MAAGE_ENTITY_LIVING) {
        if (entity->payload.living.hurt_animation_ticks > 10 || entity->payload.living.death_ticks > 20 || (entity->payload.living.health && entity->payload.living.death_ticks) || entity->payload.living.health > definition->health || entity->payload.living.hurt_ticks > definition->invulnerability_ticks || entity->payload.living.attack_ticks > definition->attack_cooldown || entity->payload.living.fuse_ticks > definition->fuse_ticks) return 0;
        if (entity->payload.living.flags & MAAGE_ENTITY_STATE_DYING) {
            if (entity->payload.living.health || entity->payload.living.idle_ticks > definition->loot_count || ((entity->payload.living.effect_cursor != 0) != (entity->payload.living.pending_drop.count != 0))) return 0;
        } else if (entity->payload.living.pending_drop.count) return 0;
    }
    return !definition->valid || definition->valid(definition, entity);
}
void maage_entity_copy_view(const maage_entity *entity, maage_entity_view *view) {
    memset(view, 0, sizeof(*view));
    view->id = entity->id;
    view->type = entity->type;
    view->registered_type = entity->registered_type;
    memcpy(view->position, entity->position, sizeof(view->position));
    memcpy(view->previous, entity->previous, sizeof(view->previous));
    memcpy(view->velocity, entity->velocity, sizeof(view->velocity));
    view->age = entity->age;
    view->pickup_delay = entity->pickup_delay;
    view->grounded = entity->grounded;
    if (entity->type == MAAGE_ENTITY_ITEM) view->payload.item = entity->payload.item;
    else if (entity->type == MAAGE_ENTITY_FALLING_BLOCK) {
        view->payload.falling.stack = entity->payload.falling.stack;
        view->payload.falling.state = entity->payload.falling.state;
        memcpy(view->payload.falling.source, entity->payload.falling.source, sizeof(view->payload.falling.source));
        view->payload.falling.source_revision = entity->payload.falling.source_revision;
    } else if (entity->type == MAAGE_ENTITY_LIVING) {
        view->payload.living.yaw = entity->payload.living.yaw;
        view->payload.living.death_ticks = entity->payload.living.death_ticks;
        view->payload.living.hurt_animation_ticks = entity->payload.living.hurt_animation_ticks;
        view->payload.living.damage_sequence = entity->payload.living.damage_sequence;
        view->payload.living.damage_yaw = entity->payload.living.damage_yaw;
        view->payload.living.head_yaw = entity->payload.living.head_yaw;
        view->payload.living.pitch = entity->payload.living.pitch;
        view->payload.living.walk_distance = entity->payload.living.walk_distance;
        view->payload.living.variant = entity->payload.living.variant;
        view->payload.living.flags = entity->payload.living.flags;
        view->payload.living.health = entity->payload.living.health;
        view->payload.living.hurt_ticks = entity->payload.living.hurt_ticks;
        view->payload.living.attack_ticks = entity->payload.living.attack_ticks;
        view->payload.living.fuse_ticks = entity->payload.living.fuse_ticks;
    } else if (entity->type == MAAGE_ENTITY_PROJECTILE) view->payload.projectile.damage = entity->payload.projectile.damage;
}
int maage_entity_view_valid(const maage_entity_view *view, const maage_item_registry *items, const maage_entity_type_registry *types) {
    maage_entity entity;
    const maage_entity_type_definition *definition;
    if (!view) return 0;
    memset(&entity, 0, sizeof(entity));
    entity.id = view->id;
    entity.type = view->type;
    entity.registered_type = view->registered_type;
    memcpy(entity.position, view->position, sizeof(entity.position));
    memcpy(entity.previous, view->previous, sizeof(entity.previous));
    memcpy(entity.velocity, view->velocity, sizeof(entity.velocity));
    entity.age = view->age;
    entity.pickup_delay = view->pickup_delay;
    entity.grounded = view->grounded;
    if (view->type == MAAGE_ENTITY_ITEM) entity.payload.item = view->payload.item;
    else if (view->type == MAAGE_ENTITY_FALLING_BLOCK) {
        entity.payload.falling.stack = view->payload.falling.stack;
        entity.payload.falling.state = view->payload.falling.state;
        memcpy(entity.payload.falling.source, view->payload.falling.source, sizeof(entity.payload.falling.source));
        entity.payload.falling.source_revision = view->payload.falling.source_revision;
    } else if (view->type == MAAGE_ENTITY_LIVING) {
        entity.payload.living.yaw = view->payload.living.yaw;
        entity.payload.living.death_ticks = view->payload.living.death_ticks;
        entity.payload.living.hurt_animation_ticks = view->payload.living.hurt_animation_ticks;
        entity.payload.living.damage_sequence = view->payload.living.damage_sequence;
        entity.payload.living.damage_yaw = view->payload.living.damage_yaw;
        entity.payload.living.head_yaw = view->payload.living.head_yaw;
        entity.payload.living.pitch = view->payload.living.pitch;
        entity.payload.living.walk_distance = view->payload.living.walk_distance;
        entity.payload.living.variant = view->payload.living.variant;
        entity.payload.living.flags = view->payload.living.flags;
        entity.payload.living.health = view->payload.living.health;
        entity.payload.living.hurt_ticks = view->payload.living.hurt_ticks;
        entity.payload.living.attack_ticks = view->payload.living.attack_ticks;
        entity.payload.living.fuse_ticks = view->payload.living.fuse_ticks;
        entity.payload.living.air = 300;
    } else if (view->type == MAAGE_ENTITY_PROJECTILE) entity.payload.projectile.damage = view->payload.projectile.damage;
    if (!maage_entity_valid(&entity, items)) return 0;
    if (!entity.registered_type) return 1;
    definition = maage_entity_type_get(types, entity.registered_type);
    if (!definition || ((entity.type == MAAGE_ENTITY_PROJECTILE) != (definition->classification == MAAGE_ENTITY_CLASS_PROJECTILE))) return 0;
    if (entity.type != MAAGE_ENTITY_LIVING) return 1;
    return entity.payload.living.health <= definition->health && entity.payload.living.hurt_ticks <= definition->invulnerability_ticks && entity.payload.living.hurt_animation_ticks <= 10 && entity.payload.living.death_ticks <= 20 && entity.payload.living.attack_ticks <= definition->attack_cooldown && entity.payload.living.fuse_ticks <= definition->fuse_ticks && (!entity.payload.living.health || !entity.payload.living.death_ticks);
}

maage_status maage_entities_fall(maage_world *world, int x, int y, int z, maage_block_state_id state_id) {
    const maage_registry_bindings *ids = maage_block_registry_bindings(maage_world_registry(world));
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
    result = falling_variant(maage_world_registry(world), items, state, entity.payload.falling.stack.item_id, &entity.payload.falling.stack.variant);
    if (maage_status_failed(result)) return result;
    entity.payload.falling.source[0] = x;
    entity.payload.falling.source[1] = y;
    entity.payload.falling.source[2] = z;
    entity.payload.falling.source_revision = maage_world_column_revision(world, maage_world_chunk_coordinate(x), maage_world_chunk_coordinate(z));
    if (!maage_entity_valid(&entity, items)) return entity_status(MAAGE_STATUS_INVARIANT_VIOLATION, MAAGE_OPERATION_CREATE);
    result = maage_entities_reserve(entities, 1);
    if (maage_status_failed(result)) return result;
    result = maage_world_set(world, x, y, z, state_id, ids->state_air);
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
    const maage_registry_bindings *ids = maage_block_registry_bindings(maage_world_registry(world));
    const maage_block_state_definition *state;
    double vertical, drag, minimum[3], maximum[3], current[3];
    unsigned int axis;
    for (axis = 0; axis < 3; ++axis) {
        minimum[axis] = entity->position[axis] - (axis == 1 ? 0 : 0.125);
        maximum[axis] = minimum[axis] + 0.25;
    }
    /* Item water motion retains ordinary gravity/drag and has no buoyancy.
     * Observe the whole small body, including a current across a cell edge. */
    if (maage_world_fluid_volume_properties(world, minimum, maximum, MAAGE_FLUID_PROPERTY_SWIMMING, current))
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
        if (state) drag = maage_block_registry_block(maage_world_registry(world), state->block_id)->item_drag;
        if (drag == 0) drag = 0.588;
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
    const maage_registry_bindings *ids = maage_block_registry_bindings(maage_world_registry(world));
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
        if (block->replaceable && under->block_id != ids->block_air && !under->fluid_kind) {
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
        if (entities->data[i].type == MAAGE_ENTITY_FALLING_BLOCK || entities->data[i].type == MAAGE_ENTITY_LIVING || entities->data[i].type == MAAGE_ENTITY_PROJECTILE || (entities->data[i].type == MAAGE_ENTITY_ITEM && entities->data[i].payload.item.count))
            entities->data[output++] = entities->data[i];
        else free(entities->data[i].state);
    entities->count = output;
}

static void entity_reindex_living(maage_entities *entities) {
    size_t i, bucket;
    for (i = 0; i < entities->buckets; ++i)
        entities->heads[i] = (size_t) - 1;
    for (i = 0; i < entities->count; ++i) {
        if (entities->data[i].type != MAAGE_ENTITY_LIVING) continue;
        bucket = entity_bucket(entities, (int) floor(entities->data[i].position[0]), (int) floor(entities->data[i].position[1]), (int) floor(entities->data[i].position[2]));
        entities->next[i] = entities->heads[bucket];
        entities->heads[bucket] = i;
    }
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
        if (entity->type == MAAGE_ENTITY_LIVING) {
            bucket = entity_bucket(entities, (int) floor(entity->position[0]), (int) floor(entity->position[1]), (int) floor(entity->position[2]));
            entities->next[i] = entities->heads[bucket];
            entities->heads[bucket] = i;
            continue;
        }
        if (entity->type != MAAGE_ENTITY_ITEM && entity->type != MAAGE_ENTITY_FALLING_BLOCK) continue;
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
                                other->payload.item.variant != entity->payload.item.variant ||
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
    entity_reindex_living(entities);
    return entity_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_TICK);
}
size_t maage_entities_count(const maage_entities *entities) {
    return entities ? entities->count : 0;
}
const maage_entity *maage_entities_at(const maage_entities *entities, size_t index) {
    return entities && index < entities->count ? &entities->data[index] : NULL;
}
maage_status maage_entity_save_clear(maage_entity_save *save) {
    size_t i;
    if (!save) return entity_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_DESTROY);
    for (i = 0; i < save->count; ++i)
        free(save->entities[i].state);
    free(save->entities);
    memset(save, 0, sizeof(*save));
    return entity_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_DESTROY);
}
static maage_status entity_clone_array(const maage_entity *source, size_t count, maage_entity **output) {
    size_t i, j;
    maage_entity *copy;
    *output = NULL;
    if (!count) return entity_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_SNAPSHOT);
    if (count > (size_t) - 1 / sizeof(*copy)) return entity_status(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_OPERATION_SNAPSHOT);
    copy = (maage_entity *) calloc(count, sizeof(*copy));
    if (!copy) return entity_status(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_SNAPSHOT);
    for (i = 0; i < count; ++i) {
        copy[i] = source[i];
        copy[i].state = NULL;
        if (!source[i].state_size) continue;
        copy[i].state = malloc(source[i].state_size);
        if (!copy[i].state) {
            for (j = 0; j < i; ++j)
                free(copy[j].state);
            free(copy);
            return entity_status(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_SNAPSHOT);
        }
        memcpy(copy[i].state, source[i].state, source[i].state_size);
    }
    *output = copy;
    return entity_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_SNAPSHOT);
}
maage_status maage_entities_snapshot(const maage_entities *entities, maage_entity_save *save) {
    maage_status result;
    if (!entities || !save) return entity_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_SNAPSHOT);
    memset(save, 0, sizeof(*save));
    result = entity_clone_array(entities->data, entities->count, &save->entities);
    if (maage_status_failed(result)) return result;
    save->count = entities->count;
    save->next_id = entities->next_id;
    save->random = entities->random;
    save->behavior_random = entities->behavior_random;
    save->spawn_random = entities->spawn_random;
    save->simulation_tick = entities->simulation_tick;
    save->simulation_cursor = (unsigned long) entities->simulation_cursor;
    return entity_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_SNAPSHOT);
}
maage_status maage_entities_restore(maage_entities *entities, const maage_item_registry *items, const maage_entity_save *save) {
    size_t i;
    maage_status result;
    maage_entity *copy;
    unsigned long previous = 0;
    if (!entities || !save || !save->next_id || save->next_id > 4294967295UL || save->random > 4294967295UL || save->behavior_random > 4294967295UL || save->spawn_random > 4294967295UL || save->simulation_tick > 4294967295UL || (save->count && !save->entities)) return entity_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_LOAD);
    for (i = 0; i < save->count; ++i) {
        if (!maage_entity_registered_valid(&save->entities[i], items, entities->types) || save->entities[i].id <= previous || save->entities[i].id >= save->next_id) return entity_status(MAAGE_STATUS_MALFORMED_INPUT, MAAGE_OPERATION_LOAD);
        previous = save->entities[i].id;
    }
    result = entity_clone_array(save->entities, save->count, &copy);
    if (maage_status_failed(result)) return result;
    result = maage_entities_reserve(entities, save->count > entities->count ? save->count - entities->count : 0);
    if (maage_status_failed(result)) {
        for (i = 0; i < save->count; ++i)
            free(copy[i].state);
        free(copy);
        return result;
    }
    for (i = 0; i < entities->count; ++i)
        free(entities->data[i].state);
    if (save->count) memcpy(entities->data, copy, save->count * sizeof(*entities->data));
    free(copy);
    entities->count = save->count;
    entities->next_id = save->next_id;
    entities->random = save->random;
    entities->behavior_random = save->behavior_random;
    entities->spawn_random = save->spawn_random;
    entities->simulation_tick = save->simulation_tick;
    entities->simulation_cursor = (size_t) save->simulation_cursor;
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

maage_status maage_entity_policy_init(maage_entity_policy *policy) {
    if (!policy) return entity_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_INITIALIZE);
    memset(policy, 0, sizeof(*policy));
    policy->tick_budget = 1024;
    policy->spawn_attempts = 16;
    policy->passive_population = 15;
    policy->hostile_population = 70;
    policy->passive_interval = 400;
    policy->hostile_interval = 1;
    policy->minimum_player_distance = 24;
    policy->maximum_player_distance = 128;
    return entity_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_INITIALIZE);
}
maage_status maage_entities_set_policy(maage_entities *entities, const maage_entity_policy *policy) {
    if (!entities || !policy || !policy->tick_budget || policy->tick_budget > (size_t) - 1 / sizeof(maage_entity) || policy->spawn_attempts > 4294967295UL || policy->passive_interval > 4294967295UL || policy->hostile_interval > 4294967295UL || !(policy->minimum_player_distance >= 0 && policy->minimum_player_distance <= policy->maximum_player_distance) || !(policy->maximum_player_distance > 0 && policy->maximum_player_distance <= MAAGE_WORLD_LIMIT / 2)) return entity_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_SET);
    entities->policy = *policy;
    return entity_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_SET);
}
static unsigned long entity_seed_stream(maage_world_seed seed, unsigned long domain) {
    unsigned long value = ((seed.low ^ domain) * 1664525UL + seed.high + 1013904223UL) & 0xffffffffUL;
    value ^= value >> 16;
    value = (value * 2246822519UL) & 0xffffffffUL;
    value ^= value >> 13;
    return value & 0xffffffffUL;
}
maage_status maage_entities_seed(maage_entities *entities, maage_world_seed seed) {
    if (!entities || entities->count || entities->next_id != 1 || seed.low > 0xffffffffUL || seed.high > 0xffffffffUL) return entity_status(MAAGE_STATUS_INVALID_STATE, MAAGE_OPERATION_INITIALIZE);
    entities->random = entity_seed_stream(seed, 0x7c5a3491UL);
    entities->behavior_random = entity_seed_stream(seed, 0x36e18b27UL);
    entities->spawn_random = entity_seed_stream(seed, 0xa415cb73UL);
    return entity_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_INITIALIZE);
}
maage_status maage_entities_bind_types(maage_entities *entities, const maage_entity_type_registry *types) {
    if (!entities || !maage_entity_type_registry_is_finalized(types) || (entities->types && entities->types != types && entities->count)) return entity_status(MAAGE_STATUS_INVALID_STATE, MAAGE_OPERATION_INITIALIZE);
    entities->types = types;
    return entity_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_INITIALIZE);
}
static unsigned long entity_random_next(unsigned long *random, unsigned long bound) {
    *random = (*random * 1664525UL + 1013904223UL) & 4294967295UL;
    return bound ? *random % bound : 0;
}
static int entity_body_loaded(const maage_world *world, const double position[3], double width) {
    int x, z;
    for (z = -1; z <= 1; z += 2)
        for (x = -1; x <= 1; x += 2)
            if (!maage_world_chunk_loaded(world, maage_world_chunk_coordinate((int) floor(position[0] + x * width * 0.5)), maage_world_chunk_coordinate((int) floor(position[2] + z * width * 0.5)))) return 0;
    return 1;
}
static int entity_body_fits(const maage_world *world, const double position[3], double width, double height, int hazards) {
    double minimum[3], maximum[3];
    int x, y, z;
    maage_block_shape shape;
    const maage_block_state_definition *state;
    const maage_block_definition *block;
    if (position[1] < 0 || position[1] + height > MAAGE_WORLD_HEIGHT || !entity_body_loaded(world, position, width)) return 0;
    minimum[0] = position[0] - width * 0.5;
    minimum[1] = position[1];
    minimum[2] = position[2] - width * 0.5;
    maximum[0] = position[0] + width * 0.5;
    maximum[1] = position[1] + height;
    maximum[2] = position[2] + width * 0.5;
    for (z = (int) floor(minimum[2] + 0.000001); z <= (int) floor(maximum[2] - 0.000001); ++z)
        for (y = (int) floor(minimum[1] + 0.000001); y <= (int) floor(maximum[1] - 0.000001); ++y)
            for (x = (int) floor(minimum[0] + 0.000001); x <= (int) floor(maximum[0] - 0.000001); ++x) {
                if (hazards) {
                    state = maage_block_registry_state(maage_world_registry(world), maage_world_get(world, x, y, z));
                    block = state ? maage_block_registry_block(maage_world_registry(world), state->block_id) : NULL;
                    if (!block || block->contact_damage) return 0;
                }
                if (!maage_world_collision_shape(world, x, y, z, &shape)) continue;
                if (maximum[0] > x + shape.minimum[0] + 0.000001 && minimum[0] < x + shape.maximum[0] - 0.000001 && maximum[1] > y + shape.minimum[1] + 0.000001 && minimum[1] < y + shape.maximum[1] - 0.000001 && maximum[2] > z + shape.minimum[2] + 0.000001 && minimum[2] < z + shape.maximum[2] - 0.000001) return 0;
            }
    return 1;
}
static int entity_supported(const maage_world *world, const double position[3]) {
    maage_block_shape shape;
    int x = (int) floor(position[0]), y = (int) floor(position[1] - 0.05), z = (int) floor(position[2]);
    return maage_world_chunk_loaded(world, maage_world_chunk_coordinate(x), maage_world_chunk_coordinate(z)) && maage_world_collision_shape(world, x, y, z, &shape) && y + shape.maximum[1] >= position[1] - 0.6;
}
maage_status maage_entities_spawn_type(maage_entities *entities, maage_world *world, maage_entity_type_id type, const double position[3], unsigned long variant, int persistent, maage_entity_id *id) {
    const maage_entity_type_definition *definition = entities ? maage_entity_type_get(entities->types, type) : NULL;
    maage_entity entity;
    maage_status result;
    unsigned int axis;
    if (id) *id = 0;
    if (!entities || !world || !position || !definition || variant > 4294967295UL || (persistent != 0 && persistent != 1) || definition->classification == MAAGE_ENTITY_CLASS_PROJECTILE) return entity_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_CREATE);
    /* Reject nonfinite and unbounded coordinates before floor-to-int casts in
     * collision/loaded-column queries. */
    for (axis = 0; axis < 3; ++axis)
        if (!(position[axis] > -MAAGE_WORLD_LIMIT && position[axis] < MAAGE_WORLD_LIMIT)) return entity_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_CREATE);
    if (!entity_body_fits(world, position, definition->width, definition->height, 0)) return entity_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_CREATE);
    memset(&entity, 0, sizeof(entity));
    entity.id = entities->next_id;
    entity.type = MAAGE_ENTITY_LIVING;
    entity.registered_type = type;
    memcpy(entity.position, position, sizeof(entity.position));
    memcpy(entity.previous, position, sizeof(entity.previous));
    memcpy(entity.payload.living.goal, position, sizeof(entity.payload.living.goal));
    entity.payload.living.health = definition->health;
    entity.payload.living.air = 300;
    entity.payload.living.variant = variant;
    entity.payload.living.flags = persistent ? MAAGE_ENTITY_STATE_PERSISTENT : 0;
    entity.state_size = definition->state_size;
    if (entity.state_size) {
        entity.state = calloc(1, entity.state_size);
        if (!entity.state) return entity_status(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_CREATE);
    }
    result = definition->initialize ? definition->initialize(definition, &entity) : entity_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_CREATE);
    if (!maage_status_failed(result) && !maage_entity_registered_valid(&entity, maage_world_item_registry(world), entities->types)) result = entity_status(MAAGE_STATUS_INVARIANT_VIOLATION, MAAGE_OPERATION_CREATE);
    if (!maage_status_failed(result)) result = maage_entities_reserve(entities, 1);
    if (maage_status_failed(result)) {
        free(entity.state);
        return result;
    }
    entities->data[entities->count++] = entity;
    ++entities->next_id;
    if (id) *id = entity.id;
    return entity_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_CREATE);
}
static int entity_line_of_sight(const maage_world *world, const double from[3], const double to[3]) {
    double direction[3], distance;
    maage_world_target target;
    maage_status result;
    unsigned int axis;
    for (axis = 0; axis < 3; ++axis)
        direction[axis] = to[axis] - from[axis];
    distance = sqrt(direction[0] * direction[0] + direction[1] * direction[1] + direction[2] * direction[2]);
    if (distance < 0.001) return 1;
    result = maage_world_raycast(world, from, direction, distance, &target);
    return maage_status_code(result) == MAAGE_STATUS_NOOP || (maage_status_code(result) == MAAGE_STATUS_SUCCESS && target.distance >= distance - 0.001);
}
static void entity_sound(maage_entity_context *context, unsigned long sound, const double position[3], double intensity) {
    if (sound && context->emit_sound) context->emit_sound(context->sound_context, sound, position, intensity, 0.9 + entity_random_next(&context->entities->behavior_random, 201) / 1000.0);
}
static void entity_hurt_player(maage_entity_context *context, unsigned int damage, const double origin[3]) {
    maage_gameplay *game = context->gameplay;
    unsigned int applied = damage;
    int occurrence = 1;
    if (!game || !game->health || game->mode == MAAGE_GAME_MODE_CREATIVE || !damage) return;
    if (game->hurt_resistance > 10) {
        if (damage <= game->last_damage) return;
        applied = damage - game->last_damage;
        occurrence = 0;
    } else game->hurt_resistance = 20;
    game->last_damage = damage;
    game->health = applied >= game->health ? 0 : game->health - applied;
    game->damage_yaw = atan2(origin[0] - context->player->position[0], context->player->position[2] - origin[2]);
    if (occurrence) {
        if (++game->damage_sequence == 0) ++game->damage_sequence;
        game->damage_tick = game->ticks;
    }
    if (!game->health) {
        game->death_tick = game->ticks;
        memset(&game->food, 0, sizeof(game->food));
        game->air = 300;
    }
    if (occurrence || !game->health) {
        maage_sound_id sound;
        maage_status selected = maage_sound_select(maage_block_registry_sounds(maage_world_registry(context->world)), game->health ? MAAGE_SOUND_PLAYER_HURT : MAAGE_SOUND_PLAYER_DEATH, 0, &sound);
        if (!maage_status_failed(selected)) entity_sound(context, sound, context->player->position, 1);
    }
}
static void entity_hurt(maage_entity_context *context, maage_entity *entity, const maage_entity_type_definition *definition, unsigned int damage, maage_entity_id attacker, const double origin[3]) {
    unsigned int applied = damage;
    int occurrence = 1;
    double dx, dz, length;
    if (!damage || !entity->payload.living.health) return;
    if (entity->payload.living.hurt_ticks > definition->invulnerability_ticks / 2) {
        if (damage <= entity->payload.living.last_damage) return;
        applied = damage - entity->payload.living.last_damage;
        occurrence = 0;
    } else entity->payload.living.hurt_ticks = definition->invulnerability_ticks;
    entity->payload.living.last_damage = damage;
    entity->payload.living.health = applied >= entity->payload.living.health ? 0 : entity->payload.living.health - applied;
    entity->payload.living.attacker = attacker;
    entity->payload.living.panic_ticks = 100;
    if (occurrence) {
        entity->payload.living.damage_sequence = (entity->payload.living.damage_sequence + 1UL) & 0xffffffffUL;
        if (!entity->payload.living.damage_sequence) entity->payload.living.damage_sequence = 1;
        entity->payload.living.hurt_animation_ticks = 10;
        entity->payload.living.damage_yaw = 0;
        if (origin) {
            dx = entity->position[0] - origin[0];
            dz = entity->position[2] - origin[2];
            length = sqrt(dx * dx + dz * dz);
            if (length < 0.0001) {
                dx = sin(entity->payload.living.yaw);
                dz = -cos(entity->payload.living.yaw);
                length = 1;
            }
            entity->payload.living.damage_yaw = atan2(-dx, dz);
            entity->velocity[0] = entity->velocity[0] * 0.5 + dx / length * 0.4;
            entity->velocity[2] = entity->velocity[2] * 0.5 + dz / length * 0.4;
            entity->velocity[1] = entity->velocity[1] * 0.5 + 0.4;
            if (entity->velocity[1] > 0.4) entity->velocity[1] = 0.4;
        }
    }
    if (occurrence || !entity->payload.living.health) entity_sound(context, entity->payload.living.health ? definition->hurt_sound : definition->death_sound, entity->position, 1);
}
static maage_status entity_projectile_spawn(maage_entity_context *context, const maage_entity_type_definition *definition, maage_entity *source) {
    const maage_entity_type_definition *projectile = maage_entity_type_get(context->entities->types, definition->projectile_type);
    maage_entity shot;
    double direction[3], length;
    unsigned int axis;
    maage_status result;
    if (!projectile || projectile->classification != MAAGE_ENTITY_CLASS_PROJECTILE) return entity_status(MAAGE_STATUS_INVARIANT_VIOLATION, MAAGE_OPERATION_CREATE);
    memset(&shot, 0, sizeof(shot));
    shot.id = context->entities->next_id;
    shot.type = MAAGE_ENTITY_PROJECTILE;
    shot.registered_type = projectile->id;
    memcpy(shot.position, source->position, sizeof(shot.position));
    shot.position[1] += definition->eye_height;
    memcpy(shot.previous, shot.position, sizeof(shot.previous));
    for (axis = 0; axis < 3; ++axis)
        direction[axis] = context->player->position[axis] - shot.position[axis];
    direction[1] += MAAGE_PLAYER_HEIGHT * 0.55;
    length = sqrt(direction[0] * direction[0] + direction[1] * direction[1] + direction[2] * direction[2]);
    if (length < 0.001) return entity_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_CREATE);
    direction[1] += sqrt(direction[0] * direction[0] + direction[2] * direction[2]) * 0.1;
    length = sqrt(direction[0] * direction[0] + direction[1] * direction[1] + direction[2] * direction[2]);
    for (axis = 0; axis < 3; ++axis)
        shot.velocity[axis] = direction[axis] / length * definition->projectile_speed;
    shot.payload.projectile.owner = source->id;
    shot.payload.projectile.damage = (unsigned int) ceil(definition->attack_damage);
    shot.state_size = projectile->state_size;
    if (shot.state_size) {
        shot.state = calloc(1, shot.state_size);
        if (!shot.state) return entity_status(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_CREATE);
    }
    result = projectile->initialize ? projectile->initialize(projectile, &shot) : entity_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_CREATE);
    if (!maage_status_failed(result) && !maage_entity_registered_valid(&shot, context->items, context->entities->types)) result = entity_status(MAAGE_STATUS_INVARIANT_VIOLATION, MAAGE_OPERATION_CREATE);
    if (!maage_status_failed(result)) result = maage_entities_reserve(context->entities, 1);
    if (maage_status_failed(result)) {
        free(shot.state);
        return result;
    }
    context->entities->data[context->entities->count++] = shot;
    ++context->entities->next_id;
    return entity_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_CREATE);
}

static maage_status entity_explode(maage_entity_context *context, const maage_entity_type_definition *definition, maage_entity *entity) {
    const maage_registry_bindings *ids = maage_block_registry_bindings(maage_world_registry(context->world));
    const maage_block_state_definition *state;
    const maage_block_definition *block;
    maage_item_drops drops;
    double radius = definition->explosion_radius, dx, dy, dz, distance, position[3], velocity[3] = {0, 0.1, 0};
    unsigned long cursor, total, random;
    int edge, extent, x, y, z;
    size_t processed, i, low, high, middle;
    maage_status result;
    if (!(entity->payload.living.flags & MAAGE_ENTITY_STATE_EXPLODING)) {
        dx = context->player->position[0] - entity->position[0];
        dy = context->player->position[1] + MAAGE_PLAYER_HEIGHT * 0.5 - entity->position[1];
        dz = context->player->position[2] - entity->position[2];
        distance = sqrt(dx * dx + dy * dy + dz * dz);
        if (distance < radius * 2 && entity_line_of_sight(context->world, entity->position, context->player->position)) entity_hurt_player(context, (unsigned int) ceil((1 - distance / (radius * 2)) * (radius * 7 + 1)), entity->position);
        entity->payload.living.effect_cursor = 0;
        entity->payload.living.effect_limit = context->entities->next_id;
        entity_sound(context, definition->attack_sound, entity->position, 4);
        entity->payload.living.flags |= MAAGE_ENTITY_STATE_EXPLODING;
        entity->payload.living.idle_ticks = 0;
    }
    low = 0;
    high = context->entities->count;
    while (low < high) {
        middle = low + (high - low) / 2;
        if (context->entities->data[middle].id <= entity->payload.living.effect_cursor) low = middle + 1;
        else high = middle;
    }
    for (i = low, processed = 0; i < context->entities->count && processed < 64; ++i) {
        maage_entity *other = &context->entities->data[i];
        const maage_entity_type_definition *other_type;
        if (other->id <= entity->payload.living.effect_cursor) continue;
        if (other->id >= entity->payload.living.effect_limit) break;
        entity->payload.living.effect_cursor = other->id;
        ++processed;
        if (other->id == entity->id || other->type != MAAGE_ENTITY_LIVING) continue;
        dx = other->position[0] - entity->position[0];
        dy = other->position[1] - entity->position[1];
        dz = other->position[2] - entity->position[2];
        distance = sqrt(dx * dx + dy * dy + dz * dz);
        other_type = maage_entity_type_get(context->entities->types, other->registered_type);
        if (other_type && distance < radius * 2 && entity_line_of_sight(context->world, entity->position, other->position)) entity_hurt(context, other, other_type, (unsigned int) ceil((1 - distance / (radius * 2)) * (radius * 7 + 1)), entity->id, entity->position);
    }
    if (processed == 64) return entity_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_TICK);
    entity->payload.living.effect_cursor = entity->payload.living.effect_limit;
    extent = (int) ceil(radius);
    edge = extent * 2 + 1;
    total = (unsigned long) edge * (unsigned long) edge * (unsigned long) edge;
    /* Destructive publication is a retained owner transaction. Resume its
     * numeric cursor after capacity failure/save, and never load terrain. */
    for (processed = 0; processed < 64 && entity->payload.living.idle_ticks < total; ++processed) {
        cursor = entity->payload.living.idle_ticks;
        dx = (double) (cursor % (unsigned long) edge) - extent;
        dy = (double) ((cursor / (unsigned long) edge) % (unsigned long) edge) - extent;
        dz = (double) (cursor / ((unsigned long) edge * (unsigned long) edge)) - extent;
        distance = sqrt(dx * dx + dy * dy + dz * dz);
        x = (int) floor(entity->position[0]) + (int) dx;
        y = (int) floor(entity->position[1]) + (int) dy;
        z = (int) floor(entity->position[2]) + (int) dz;
        if (distance > radius || y < 0 || y >= MAAGE_WORLD_HEIGHT || !maage_world_chunk_loaded(context->world, maage_world_chunk_coordinate(x), maage_world_chunk_coordinate(z))) {
            ++entity->payload.living.idle_ticks;
            continue;
        }
        state = maage_block_registry_state(maage_world_registry(context->world), maage_world_get(context->world, x, y, z));
        block = state ? maage_block_registry_block(maage_world_registry(context->world), state->block_id) : NULL;
        if (!block) return entity_status(MAAGE_STATUS_INVARIANT_VIOLATION, MAAGE_OPERATION_LOOKUP);
        if (block->id == ids->block_air || block->hardness < 0 || block->hardness * 0.3 > radius - distance) {
            ++entity->payload.living.idle_ticks;
            continue;
        }
        random = context->entities->random;
        memset(&drops, 0, sizeof(drops));
        if (entity_random_next(&random, (unsigned long) ceil(radius)) == 0) {
            result = maage_item_generate_drops(maage_world_registry(context->world), context->items, state->id, NULL, &random, &drops);
            if (maage_status_failed(result)) return result;
        }
        result = maage_entities_reserve(context->entities, drops.count);
        if (maage_status_failed(result)) return result;
        result = maage_world_set(context->world, x, y, z, state->id, ids->state_air);
        if (maage_status_failed(result)) return result;
        if (maage_status_code(result) == MAAGE_STATUS_SUCCESS) {
            position[0] = x + 0.5;
            position[1] = y + 0.1;
            position[2] = z + 0.5;
            for (i = 0; i < drops.count; ++i) {
                result = maage_entities_spawn(context->entities, context->items, &drops.stacks[i], position, velocity, 10);
                if (maage_status_failed(result)) return result;
            }
            context->entities->random = random;
        }
        ++entity->payload.living.idle_ticks;
    }
    if (entity->payload.living.idle_ticks == total) entity->type = 0;
    return entity_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_TICK);
}
static void entity_steer(maage_entity_context *context, maage_entity *entity, const maage_entity_type_definition *definition, double dx, double dz, double speed) {
    double length = sqrt(dx * dx + dz * dz), best = -1e30, direction[2], trial[3], angle, score;
    int candidate, jump = 0, best_jump = 0, found = 0;
    if (length < 0.1 || speed <= 0) {
        entity->velocity[0] *= 0.6;
        entity->velocity[2] *= 0.6;
        return;
    }
    angle = atan2(dx, -dz);
    for (candidate = 0; candidate < 9; ++candidate) {
        double turn = candidate == 0 ? 0 : ((candidate + 1) / 2) * 0.7853981633974483 * (candidate % 2 ? 1 : -1);
        direction[0] = sin(angle + turn);
        direction[1] = -cos(angle + turn);
        trial[0] = entity->position[0] + direction[0] * (definition->width * 0.5 + 0.6);
        trial[1] = entity->position[1];
        trial[2] = entity->position[2] + direction[1] * (definition->width * 0.5 + 0.6);
        jump = 0;
        if (!entity_body_fits(context->world, trial, definition->width, definition->height, 1)) {
            trial[1] += 1;
            if (!entity->grounded || !entity_body_fits(context->world, trial, definition->width, definition->height, 1)) continue;
            jump = 1;
        }
        if (!entity_supported(context->world, trial) && !maage_world_fluid_at(context->world, trial)) continue;
        score = (direction[0] * dx + direction[1] * dz) / length - fabs(turn) * 0.04;
        if (score <= best) continue;
        best = score;
        found = 1;
        best_jump = jump;
        entity->payload.living.yaw = angle + turn;
    }
    if (!found) {
        entity->velocity[0] *= 0.5;
        entity->velocity[2] *= 0.5;
        return;
    }
    entity->velocity[0] += (sin(entity->payload.living.yaw) * speed - entity->velocity[0]) * 0.35;
    entity->velocity[2] += (-cos(entity->payload.living.yaw) * speed - entity->velocity[2]) * 0.35;
    if (best_jump && entity->grounded) entity->velocity[1] = definition->jump_velocity;
}
maage_status maage_entities_behavior_standard(const maage_entity_type_definition *definition, maage_entity *entity, maage_entity_context *context) {
    double dx, dy, dz, distance, speed, eye[3], target[3], angle;
    int pursuing, visible;
    maage_status result;
    if (!definition || !entity || !context || !context->player || !context->gameplay || entity->type != MAAGE_ENTITY_LIVING) return entity_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_TICK);
    if (entity->payload.living.flags & MAAGE_ENTITY_STATE_EXPLODING) return entity_explode(context, definition, entity);
    dx = context->player->position[0] - entity->position[0];
    dy = context->player->position[1] - entity->position[1];
    dz = context->player->position[2] - entity->position[2];
    distance = sqrt(dx * dx + dy * dy + dz * dz);
    pursuing = (definition->behaviors & MAAGE_ENTITY_BEHAVIOR_PURSUE) && context->gameplay->health && context->gameplay->mode != MAAGE_GAME_MODE_CREATIVE && distance <= definition->follow_range;
    memcpy(eye, entity->position, sizeof(eye));
    eye[1] += definition->eye_height;
    memcpy(target, context->player->position, sizeof(target));
    target[1] += MAAGE_PLAYER_EYE_HEIGHT;
    visible = pursuing && entity_line_of_sight(context->world, eye, target);
    entity->payload.living.target = pursuing ? MAAGE_ENTITY_PLAYER_ID : 0;
    if (pursuing) {
        memcpy(entity->payload.living.goal, context->player->position, sizeof(entity->payload.living.goal));
        entity->payload.living.idle_ticks = 0;
        entity->payload.living.pitch = atan2(eye[1] - target[1], sqrt(dx * dx + dz * dz));
    } else if (entity->payload.living.panic_ticks) {
        angle = atan2(-dx, dz);
        entity->payload.living.goal[0] = entity->position[0] + sin(angle) * 8;
        entity->payload.living.goal[1] = entity->position[1];
        entity->payload.living.goal[2] = entity->position[2] - cos(angle) * 8;
    } else if ((definition->behaviors & MAAGE_ENTITY_BEHAVIOR_WANDER) && entity_random_next(&context->entities->behavior_random, 40) == 0) {
        angle = entity_random_next(&context->entities->behavior_random, 6284) / 1000.0;
        entity->payload.living.goal[0] = entity->position[0] + sin(angle) * (2 + entity_random_next(&context->entities->behavior_random, 7));
        entity->payload.living.goal[1] = entity->position[1];
        entity->payload.living.goal[2] = entity->position[2] - cos(angle) * (2 + entity_random_next(&context->entities->behavior_random, 7));
        entity->payload.living.pitch = 0;
    }
    if ((definition->behaviors & MAAGE_ENTITY_BEHAVIOR_EXPLODE) && pursuing && visible && distance < (entity->payload.living.fuse_ticks ? 7 : definition->attack_range)) {
        if (++entity->payload.living.fuse_ticks >= definition->fuse_ticks) return entity_explode(context, definition, entity);
    } else if (entity->payload.living.fuse_ticks) --entity->payload.living.fuse_ticks;
    if (pursuing && visible && !entity->payload.living.attack_ticks && distance <= definition->attack_range && fabs(dy) < definition->height) {
        if (definition->behaviors & MAAGE_ENTITY_BEHAVIOR_MELEE) {
            entity_hurt_player(context, (unsigned int) ceil(definition->attack_damage), entity->position);
            entity->payload.living.attack_ticks = definition->attack_cooldown;
            entity_sound(context, definition->attack_sound, entity->position, 1);
        } else if (definition->behaviors & MAAGE_ENTITY_BEHAVIOR_RANGED) {
            result = entity_projectile_spawn(context, definition, entity);
            if (maage_status_failed(result)) return result;
            entity->payload.living.attack_ticks = definition->attack_cooldown;
            entity_sound(context, definition->attack_sound, entity->position, 1);
        }
    }
    speed = definition->speed * (entity->payload.living.panic_ticks ? 1.5 : 1);
    if (entity->payload.living.fuse_ticks || ((definition->behaviors & MAAGE_ENTITY_BEHAVIOR_RANGED) && pursuing && visible && distance <= definition->attack_range * 0.8)) speed = 0;
    entity_steer(context, entity, definition, entity->payload.living.goal[0] - entity->position[0], entity->payload.living.goal[2] - entity->position[2], speed);
    entity->payload.living.head_yaw = pursuing ? atan2(dx, -dz) : entity->payload.living.yaw;
    if (!pursuing && !entity->payload.living.panic_ticks && distance < 6 && entity_line_of_sight(context->world, eye, target)) {
        entity->payload.living.head_yaw = atan2(dx, -dz);
        entity->payload.living.pitch = atan2(eye[1] - target[1], sqrt(dx * dx + dz * dz));
    } else if (!pursuing) entity->payload.living.pitch = 0;
    return entity_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_TICK);
}

static void entity_soft_collisions(maage_entity_context *context, maage_entity *entity, const maage_entity_type_definition *definition) {
    maage_entities *entities = context->entities;
    const maage_entity_type_definition *other_type;
    const maage_entity *other;
    size_t index, bucket, examined = 0;
    int dx, dy, dz, x = (int) floor(entity->position[0]), y = (int) floor(entity->position[1]), z = (int) floor(entity->position[2]);
    double offset_x, offset_z, length, reach;
    if (!entities->buckets) return;
    for (dz = -1; dz <= 1 && examined < 64; ++dz)
        for (dy = -1; dy <= 1 && examined < 64; ++dy)
            for (dx = -1; dx <= 1 && examined < 64; ++dx) {
                bucket = entity_bucket(entities, x + dx, y + dy, z + dz);
                for (index = entities->heads[bucket]; index != (size_t) - 1 && examined < 64; index = entities->next[index], ++examined) {
                    other = &entities->data[index];
                    if (other->id == entity->id || other->type != MAAGE_ENTITY_LIVING || !other->payload.living.health) continue;
                    other_type = maage_entity_type_get(entities->types, other->registered_type);
                    if (!other_type || other->position[1] >= entity->position[1] + definition->height || other->position[1] + other_type->height <= entity->position[1]) continue;
                    offset_x = entity->position[0] - other->position[0];
                    offset_z = entity->position[2] - other->position[2];
                    reach = (definition->width + other_type->width) * 0.5;
                    length = sqrt(offset_x * offset_x + offset_z * offset_z);
                    if (length >= reach) continue;
                    if (length < 0.001) {
                        offset_x = entity->id < other->id ? 1 : -1;
                        offset_z = 0;
                        length = 1;
                    }
                    entity->velocity[0] += offset_x / length * 0.025;
                    entity->velocity[2] += offset_z / length * 0.025;
                }
            }
    offset_x = entity->position[0] - context->player->position[0];
    offset_z = entity->position[2] - context->player->position[2];
    length = sqrt(offset_x * offset_x + offset_z * offset_z);
    if (length > 0.001 && length < (definition->width + MAAGE_PLAYER_WIDTH) * 0.5 && entity->position[1] < context->player->position[1] + MAAGE_PLAYER_HEIGHT && entity->position[1] + definition->height > context->player->position[1]) {
        entity->velocity[0] += offset_x / length * 0.05;
        entity->velocity[2] += offset_z / length * 0.05;
    }
    if (entity->velocity[0] < -4) entity->velocity[0] = -4;
    if (entity->velocity[0] > 4) entity->velocity[0] = 4;
    if (entity->velocity[2] < -4) entity->velocity[2] = -4;
    if (entity->velocity[2] > 4) entity->velocity[2] = 4;
}
static void entity_living_move(maage_entity_context *context, maage_entity *entity, const maage_entity_type_definition *definition) {
    double low[3], high[3], current[3], vertical, old_y, dx, dz;
    int swimming, collided;
    unsigned int axis;
    for (axis = 0; axis < 3; ++axis) {
        low[axis] = entity->position[axis] - (axis == 1 ? 0 : definition->width * 0.5);
        high[axis] = low[axis] + (axis == 1 ? definition->height : definition->width);
    }
    swimming = maage_world_fluid_volume_properties(context->world, low, high, MAAGE_FLUID_PROPERTY_SWIMMING, current);
    if (swimming) {
        for (axis = 0; axis < 3; ++axis)
            entity->velocity[axis] += current[axis] * MAAGE_PLAYER_CURRENT_IMPULSE;
        entity->velocity[1] = entity->velocity[1] * 0.8 - definition->gravity * 0.25;
        if (definition->flags & MAAGE_ENTITY_CAN_SWIM) entity->velocity[1] += 0.04;
        entity->payload.living.fall_distance = 0;
    } else entity->velocity[1] -= definition->gravity;
    if ((definition->flags & MAAGE_ENTITY_SLOW_FALL) && entity->velocity[1] < 0) entity->velocity[1] *= 0.6;
    if (entity->velocity[1] < -definition->fall_speed_limit) entity->velocity[1] = -definition->fall_speed_limit;
    old_y = entity->position[1];
    dx = entity->position[0];
    dz = entity->position[2];
    vertical = entity->velocity[1];
    entity->grounded = maage_world_sweep_body(context->world, entity->position, entity->velocity, definition->width, definition->height, 1, vertical) && vertical < 0;
    collided = maage_world_sweep_body(context->world, entity->position, entity->velocity, definition->width, definition->height, 0, entity->velocity[0]);
    if (maage_world_sweep_body(context->world, entity->position, entity->velocity, definition->width, definition->height, 2, entity->velocity[2])) collided = 1;
    if (collided && (definition->flags & MAAGE_ENTITY_CLIMBS)) entity->velocity[1] = 0.2;
    entity->velocity[1] *= swimming ? 0.8 : 0.98;
    entity->velocity[0] *= swimming ? 0.8 : 0.91;
    entity->velocity[2] *= swimming ? 0.8 : 0.91;
    dx = entity->position[0] - dx;
    dz = entity->position[2] - dz;
    entity->payload.living.walk_distance += sqrt(dx * dx + dz * dz);
    if (entity->position[1] < old_y && !(definition->flags & MAAGE_ENTITY_SLOW_FALL)) entity->payload.living.fall_distance += old_y - entity->position[1];
    if (entity->grounded) {
        if (entity->payload.living.fall_distance > 3) entity_hurt(context, entity, definition, (unsigned int) ceil(entity->payload.living.fall_distance - 3), 0, NULL);
        entity->payload.living.fall_distance = 0;
    }
}
static void entity_living_environment(maage_entity_context *context, maage_entity *entity, const maage_entity_type_definition *definition) {
    maage_world_observation observation;
    maage_environment_state environment;
    const maage_block_state_definition *state;
    const maage_block_definition *block;
    const maage_fluid_definition *fluid;
    double eye[3], low[3], high[3];
    int x, y, z;
    unsigned int damage = 0, axis;
    if (entity->position[1] < -32) {
        entity_hurt(context, entity, definition, 4, 0, NULL);
        return;
    }
    memcpy(eye, entity->position, sizeof(eye));
    eye[1] += definition->eye_height;
    fluid = maage_fluid_get(maage_block_registry_fluids(maage_world_registry(context->world)), maage_world_fluid_at(context->world, eye));
    if (fluid && fluid->submerges) {
        if (entity->payload.living.air) --entity->payload.living.air;
        else if (entity->age % 20 == 0) entity_hurt(context, entity, definition, 2, 0, NULL);
    } else entity->payload.living.air = 300;
    for (axis = 0; axis < 3; ++axis) {
        low[axis] = entity->position[axis] - (axis == 1 ? 0 : definition->width * 0.5);
        high[axis] = low[axis] + (axis == 1 ? definition->height : definition->width);
    }
    for (z = (int) floor(low[2]); z <= (int) floor(high[2] - 0.000001); ++z)
        for (y = (int) floor(low[1]); y <= (int) floor(high[1] - 0.000001); ++y)
            for (x = (int) floor(low[0]); x <= (int) floor(high[0] - 0.000001); ++x) {
                state = maage_block_registry_state(maage_world_registry(context->world), maage_world_get(context->world, x, y, z));
                block = state ? maage_block_registry_block(maage_world_registry(context->world), state->block_id) : NULL;
                if (!block || !block->contact_damage) continue;
                if (high[0] > x + block->contact_shape.minimum[0] && low[0] < x + block->contact_shape.maximum[0] && high[1] > y + block->contact_shape.minimum[1] && low[1] < y + block->contact_shape.maximum[1] && high[2] > z + block->contact_shape.minimum[2] && low[2] < z + block->contact_shape.maximum[2] && block->contact_damage > damage) damage = block->contact_damage;
            }
    if (damage) entity_hurt(context, entity, definition, damage, 0, NULL);
    if (maage_world_fluid_volume_properties(context->world, low, high, MAAGE_FLUID_PROPERTY_SWIMMING, NULL)) entity->payload.living.fire_ticks = 0;
    if (entity->payload.living.fire_ticks) {
        --entity->payload.living.fire_ticks;
        entity->payload.living.flags |= MAAGE_ENTITY_STATE_BURNING;
        if (entity->age % 20 == 0) entity_hurt(context, entity, definition, 1, 0, NULL);
    } else entity->payload.living.flags &= ~MAAGE_ENTITY_STATE_BURNING;
    x = (int) floor(eye[0]);
    y = (int) floor(eye[1]);
    z = (int) floor(eye[2]);
    if ((definition->flags & MAAGE_ENTITY_BURNS_DAYLIGHT) && entity->age % 20 == 0 && maage_world_observe(context->world, x, y, z, &observation) && observation.sky_exposed) {
        maage_world_environment(context->world, &environment);
        if (!environment.raining && maage_environment_sunlight(&environment) > 0.7 && !fluid) entity->payload.living.fire_ticks = 160;
    }
}
static unsigned long entity_loot_flags(const maage_entity *entity) {
    unsigned long flags = 0;
    if (entity->payload.living.flags & MAAGE_ENTITY_STATE_BURNING) flags |= MAAGE_ENTITY_LOOT_BURNING;
    if (entity->payload.living.flags & MAAGE_ENTITY_STATE_SHEARED) flags |= MAAGE_ENTITY_LOOT_SHEARED;
    if (entity->payload.living.attacker == MAAGE_ENTITY_PLAYER_ID) flags |= MAAGE_ENTITY_LOOT_PLAYER;
    return flags;
}
static int entity_loot_eligible(const maage_entity_loot *loot, unsigned long flags) {
    return (loot->required_flags & flags) == loot->required_flags && !(loot->excluded_flags & flags);
}
static int entity_loot_group_selected(const maage_entity_type_definition *definition, const maage_entity *entity, size_t selected, unsigned long flags) {
    const maage_entity_loot *loot = &definition->loot[selected];
    unsigned long total = 0, roll, random;
    size_t i;
    if (!loot->group) return 1;
    for (i = 0; i < definition->loot_count; ++i)
        if (definition->loot[i].group == loot->group && entity_loot_eligible(&definition->loot[i], flags)) {
            if (definition->loot[i].weight > 4294967295UL - total) return 0;
            total += definition->loot[i].weight;
        }
    if (!total) return 0;
    random = entity->payload.living.target ^ (loot->group * 0x9e3779b9UL);
    roll = entity_random_next(&random, total);
    for (i = 0; i < definition->loot_count; ++i) {
        if (definition->loot[i].group != loot->group || !entity_loot_eligible(&definition->loot[i], flags)) continue;
        if (roll < definition->loot[i].weight) return i == selected;
        roll -= definition->loot[i].weight;
    }
    return 0;
}
static maage_status entity_living_death(maage_entity_context *context, maage_entity *entity, const maage_entity_type_definition *definition) {
    const maage_entity_loot *loot;
    const maage_item_definition *item;
    maage_item_stack stack;
    unsigned long random, flags = entity_loot_flags(entity);
    size_t processed = 0, emitted = 0;
    double velocity[3] = {0, 0.15, 0};
    maage_status result;
    if (!(entity->payload.living.flags & MAAGE_ENTITY_STATE_DYING)) {
        entity->payload.living.flags |= MAAGE_ENTITY_STATE_DYING;
        entity->payload.living.idle_ticks = 0;
        entity->payload.living.target = entity_random_next(&context->entities->random, 0xffffffffUL);
        entity->payload.living.effect_cursor = 0;
        memset(&entity->payload.living.pending_drop, 0, sizeof(entity->payload.living.pending_drop));
    }
    while (processed < 16 && emitted < 16 && entity->payload.living.idle_ticks < definition->loot_count) {
        if (!entity->payload.living.effect_cursor) {
            ++processed;
            loot = &definition->loot[entity->payload.living.idle_ticks];
            random = context->entities->random;
            if (!entity_loot_eligible(loot, flags) || !entity_loot_group_selected(definition, entity, (size_t) entity->payload.living.idle_ticks, flags) || entity_random_next(&random, loot->chance_denominator) >= loot->chance_numerator) {
                context->entities->random = random;
                ++entity->payload.living.idle_ticks;
                continue;
            }
            stack = loot->stack;
            stack.count = loot->min_count + (unsigned int) entity_random_next(&random, (unsigned long) loot->max_count - loot->min_count + 1);
            if (definition->loot_stack) {
                result = definition->loot_stack(definition, entity, &stack);
                if (maage_status_failed(result)) return result;
            }
            item = maage_item_registry_get(context->items, stack.item_id);
            if (!item || !item->max_stack || stack.variant >= item->variant_count || stack.damage > item->durability) return entity_status(MAAGE_STATUS_INVARIANT_VIOLATION, MAAGE_OPERATION_VALIDATE);
            entity->payload.living.effect_cursor = stack.count;
            entity->payload.living.pending_drop = stack;
            entity->payload.living.pending_drop.count = stack.count ? 1 : 0;
            if (!stack.count) memset(&entity->payload.living.pending_drop, 0, sizeof(entity->payload.living.pending_drop));
            context->entities->random = random;
            if (!stack.count) {
                ++entity->payload.living.idle_ticks;
                continue;
            }
        }
        item = maage_item_registry_get(context->items, entity->payload.living.pending_drop.item_id);
        if (!item) return entity_status(MAAGE_STATUS_INVARIANT_VIOLATION, MAAGE_OPERATION_LOOKUP);
        stack = entity->payload.living.pending_drop;
        stack.count = entity->payload.living.effect_cursor > item->max_stack ? item->max_stack : (unsigned int) entity->payload.living.effect_cursor;
        result = maage_entities_reserve(context->entities, 1);
        if (maage_status_failed(result)) return result;
        result = maage_entities_spawn(context->entities, context->items, &stack, entity->position, velocity, 10);
        if (maage_status_failed(result)) return result;
        entity->payload.living.effect_cursor -= stack.count;
        ++emitted;
        if (!entity->payload.living.effect_cursor) {
            memset(&entity->payload.living.pending_drop, 0, sizeof(entity->payload.living.pending_drop));
            ++entity->payload.living.idle_ticks;
        }
    }
    if (entity->payload.living.idle_ticks == definition->loot_count && entity->payload.living.death_ticks >= 20) {
        if (entity->payload.living.attacker == MAAGE_ENTITY_PLAYER_ID && definition->experience) {
            result = maage_gameplay_add_experience(context->gameplay, definition->experience);
            if (maage_status_failed(result)) return result;
        }
        entity->type = 0;
    }
    return entity_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_TICK);
}
maage_status maage_entities_shear(maage_entity_context *context, maage_entity *entity, const maage_item_stack *wool) {
    maage_status result;
    const maage_item_definition *tool;
    double velocity[3] = {0, 0.15, 0};
    if (!context || !entity || !wool || !context->held || entity->type != MAAGE_ENTITY_LIVING) return entity_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_MUTATE);
    tool = maage_item_registry_get(context->items, context->held->item_id);
    if (!tool || !context->held->count || !entity->payload.living.health || (entity->payload.living.flags & MAAGE_ENTITY_STATE_SHEARED)) return entity_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_MUTATE);
    if (!maage_item_stack_valid(context->items, wool) || !wool->count) return entity_status(MAAGE_STATUS_INVARIANT_VIOLATION, MAAGE_OPERATION_VALIDATE);
    result = maage_entities_reserve(context->entities, 1);
    if (maage_status_failed(result)) return result;
    result = maage_entities_spawn(context->entities, context->items, wool, entity->position, velocity, 10);
    if (maage_status_failed(result)) return result;
    entity->payload.living.flags |= MAAGE_ENTITY_STATE_SHEARED | MAAGE_ENTITY_STATE_PERSISTENT;
    if (!context->creative && tool->durability) {
        if (context->held->damage == tool->durability) memset(context->held, 0, sizeof(*context->held));
        else ++context->held->damage;
    }
    context->handled = 1;
    return entity_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_MUTATE);
}

int maage_entity_ray_box(const double origin[3], const double direction[3], const double position[3], double width, double height, double range, double *distance) {
    double minimum[3], maximum[3], low = 0, high = range, first, last, swap;
    unsigned int axis;
    for (axis = 0; axis < 3; ++axis) {
        minimum[axis] = position[axis] - (axis == 1 ? 0 : width * 0.5);
        maximum[axis] = minimum[axis] + (axis == 1 ? height : width);
        if (fabs(direction[axis]) < 1e-12) {
            if (origin[axis] < minimum[axis] || origin[axis] > maximum[axis]) return 0;
            continue;
        }
        first = (minimum[axis] - origin[axis]) / direction[axis];
        last = (maximum[axis] - origin[axis]) / direction[axis];
        if (first > last) {
            swap = first;
            first = last;
            last = swap;
        }
        if (first > low) low = first;
        if (last < high) high = last;
        if (high < low) return 0;
    }
    *distance = low;
    return low <= range;
}
static void entity_projectile_tick(maage_entity_context *context, maage_entity *entity, const maage_entity_type_definition *definition) {
    double origin[3], direction[3], distance, length;
    int collided;
    unsigned int axis;
    memcpy(origin, entity->position, sizeof(origin));
    entity->velocity[1] -= definition->gravity;
    if (entity->velocity[1] < -definition->fall_speed_limit) entity->velocity[1] = -definition->fall_speed_limit;
    collided = maage_world_sweep_body(context->world, entity->position, entity->velocity, definition->width, definition->height, 1, entity->velocity[1]);
    if (maage_world_sweep_body(context->world, entity->position, entity->velocity, definition->width, definition->height, 0, entity->velocity[0])) collided = 1;
    if (maage_world_sweep_body(context->world, entity->position, entity->velocity, definition->width, definition->height, 2, entity->velocity[2])) collided = 1;
    for (axis = 0; axis < 3; ++axis)
        direction[axis] = entity->position[axis] - origin[axis];
    length = sqrt(direction[0] * direction[0] + direction[1] * direction[1] + direction[2] * direction[2]);
    if (length > 0.00001) {
        for (axis = 0; axis < 3; ++axis)
            direction[axis] /= length;
        if (context->gameplay->health && maage_entity_ray_box(origin, direction, context->player->position, MAAGE_PLAYER_WIDTH + definition->width, MAAGE_PLAYER_HEIGHT, length, &distance)) {
            entity_hurt_player(context, entity->payload.projectile.damage, origin);
            entity->type = 0;
            return;
        }
    }
    for (axis = 0; axis < 3; ++axis)
        entity->velocity[axis] *= 0.99;
    if (collided && entity->payload.projectile.recovery.count) {
        maage_item_stack recovery = entity->payload.projectile.recovery;
        memset(&entity->payload, 0, sizeof(entity->payload));
        entity->payload.item = recovery;
        entity->type = MAAGE_ENTITY_ITEM;
        entity->registered_type = 0;
        entity->age = 0;
        entity->pickup_delay = 10;
        free(entity->state);
        entity->state = NULL;
        entity->state_size = 0;
    } else if (collided || entity->age >= 1200 || entity->position[1] < -32) entity->type = 0;
}
static maage_status entity_spawn_rule_valid(maage_entities *entities, maage_world *world, const maage_entity_type_definition *definition, const maage_entity_spawn_rule *rule, const maage_player *player, const double position[3], const maage_environment_state *environment, int *accepted) {
    maage_world_observation observation;
    const maage_block_state_definition *surface;
    const maage_block_definition *block;
    double dx = position[0] - player->position[0], dy = position[1] - player->position[1], dz = position[2] - player->position[2], distance = sqrt(dx * dx + dy * dy + dz * dz);
    unsigned int sky, light, subtract;
    size_t i, same = 0, population = 0;
    int valid;
    maage_status result;
    *accepted = 0;
    if (!maage_world_simulation_contains(world, position[0], position[2]) || !rule->population || position[1] < rule->min_y || position[1] > rule->max_y || distance < entities->policy.minimum_player_distance || distance > entities->policy.maximum_player_distance || distance < rule->min_player_distance || (rule->max_player_distance > 0 && distance > rule->max_player_distance)) return entity_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_VALIDATE);
    if (rule->time_start != rule->time_end) {
        if (rule->time_start < rule->time_end) {
            if (environment->day_tick < rule->time_start || environment->day_tick > rule->time_end) return entity_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_VALIDATE);
        } else if (environment->day_tick < rule->time_start && environment->day_tick > rule->time_end) return entity_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_VALIDATE);
    }
    if (!maage_world_observe(world, (int) floor(position[0]), (int) floor(position[1]), (int) floor(position[2]), &observation) || !observation.light_available || !maage_entity_spawn_biome_matches(rule, observation.biome_id) || (rule->require_sky && !observation.sky_exposed)) return entity_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_VALIDATE);
    sky = observation.light >> 4;
    subtract = maage_environment_sky_subtracted(environment);
    sky = sky > subtract ? sky - subtract : 0;
    light = observation.light & 15;
    if (sky > light) light = sky;
    if (light < rule->min_light || light > rule->max_light) return entity_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_VALIDATE);
    if ((rule->fluid == 0 && maage_world_fluid_at(world, position)) || (rule->fluid == MAAGE_CONTENT_ID_INVALID && !maage_world_fluid_at(world, position)) || (rule->fluid && rule->fluid != MAAGE_CONTENT_ID_INVALID && maage_world_fluid_at(world, position) != rule->fluid)) return entity_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_VALIDATE);
    surface = maage_block_registry_state(maage_world_registry(world), maage_world_get(world, (int) floor(position[0]), (int) floor(position[1] - 0.05), (int) floor(position[2])));
    block = surface ? maage_block_registry_block(maage_world_registry(world), surface->block_id) : NULL;
    if (!block || (!rule->fluid && !block->solid) || (rule->surface != MAAGE_CONTENT_ID_INVALID && block->id != rule->surface)) return entity_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_VALIDATE);
    if (!entity_body_fits(world, position, definition->width, definition->height, 1)) return entity_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_VALIDATE);
    for (i = 0; i < entities->count; ++i) {
        const maage_entity *other = &entities->data[i];
        const maage_entity_type_definition *other_type;
        if (other->type != MAAGE_ENTITY_LIVING) continue;
        other_type = maage_entity_type_get(entities->types, other->registered_type);
        if (!other_type || !maage_world_simulation_contains(world, other->position[0], other->position[2]) || !entity_body_loaded(world, other->position, other_type->width)) continue;
        if (other_type->classification == definition->classification) ++population;
        if (other->registered_type == definition->id) ++same;
        if (fabs(other->position[0] - position[0]) < (other_type->width + definition->width) * 0.5 && fabs(other->position[2] - position[2]) < (other_type->width + definition->width) * 0.5 && other->position[1] < position[1] + definition->height && other->position[1] + other_type->height > position[1]) return entity_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_VALIDATE);
    }
    if (same >= rule->population || population >= (definition->classification == MAAGE_ENTITY_CLASS_PASSIVE ? entities->policy.passive_population : entities->policy.hostile_population)) return entity_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_VALIDATE);
    if (rule->validate) {
        valid = 0;
        result = rule->validate(definition, world, position, &valid);
        if (maage_status_failed(result)) return result;
        if (maage_status_code(result) != MAAGE_STATUS_SUCCESS || !valid) return entity_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_VALIDATE);
    }
    *accepted = 1;
    return entity_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_VALIDATE);
}
static maage_status entity_natural_spawn(maage_entities *entities, maage_world *world, const maage_player *player) {
    maage_environment_state environment;
    maage_world_observation observation;
    const maage_entity_type_definition *definition;
    const maage_entity_spawn_rule *rule;
    unsigned int classification, group;
    size_t attempt, member;
    double angle, radius, position[3], origin[3];
    maage_status result;
    int accepted;
    int passive = entities->policy.passive_interval && entities->simulation_tick % entities->policy.passive_interval == 0;
    int hostile = entities->policy.hostile_interval && entities->simulation_tick % entities->policy.hostile_interval == 0;
    if ((!passive && !hostile) || !entities->policy.spawn_attempts) return entity_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_CREATE);
    maage_world_environment(world, &environment);
    for (attempt = 0; attempt < entities->policy.spawn_attempts; ++attempt) {
        classification = passive && (!hostile || attempt % 2 == 0) ? MAAGE_ENTITY_CLASS_PASSIVE : MAAGE_ENTITY_CLASS_HOSTILE;
        angle = entity_random_next(&entities->spawn_random, 6284) / 1000.0;
        radius = entities->policy.minimum_player_distance + entity_random_next(&entities->spawn_random, 65536) / 65535.0 * (entities->policy.maximum_player_distance - entities->policy.minimum_player_distance);
        origin[0] = floor(player->position[0] + sin(angle) * radius) + 0.5;
        origin[1] = player->position[1];
        origin[2] = floor(player->position[2] - cos(angle) * radius) + 0.5;
        if (!maage_world_observe(world, (int) floor(origin[0]), (int) floor(origin[1]), (int) floor(origin[2]), &observation)) continue;
        result = maage_entity_spawn_select(entities->types, classification, observation.biome_id, entity_random_next(&entities->spawn_random, 0xffffffffUL), &definition, &rule);
        if (maage_status_failed(result)) return result;
        if (maage_status_code(result) == MAAGE_STATUS_NOOP) continue;
        origin[1] = observation.precipitation_height;
        if (classification == MAAGE_ENTITY_CLASS_HOSTILE && entity_random_next(&entities->spawn_random, 2)) {
            int y = (int) entity_random_next(&entities->spawn_random, (unsigned long) (observation.precipitation_height > 1 ? observation.precipitation_height : 1));
            int scan;
            for (scan = 0; scan < 16 && y > 0; ++scan, --y)
                if (!maage_world_solid(world, (int) floor(origin[0]), y, (int) floor(origin[2])) && maage_world_solid(world, (int) floor(origin[0]), y - 1, (int) floor(origin[2]))) break;
            origin[1] = y;
        }
        group = rule->group_min + (unsigned int) entity_random_next(&entities->spawn_random, (unsigned long) rule->group_max - rule->group_min + 1);
        /* Every proposed group member consumes the same finite attempt budget;
         * large module-defined groups cannot multiply work inside one step. */
        for (member = 0; member < group && attempt < entities->policy.spawn_attempts; ++member) {
            memcpy(position, origin, sizeof(position));
            if (member) {
                if (attempt + 1 >= entities->policy.spawn_attempts) break;
                ++attempt;
                position[0] += (long) entity_random_next(&entities->spawn_random, 9) - 4;
                position[2] += (long) entity_random_next(&entities->spawn_random, 9) - 4;
                if (!maage_world_observe(world, (int) floor(position[0]), (int) floor(position[1]), (int) floor(position[2]), &observation)) continue;
                position[1] = observation.precipitation_height;
            }
            result = entity_spawn_rule_valid(entities, world, definition, rule, player, position, &environment, &accepted);
            if (maage_status_failed(result)) return result;
            if (!accepted) continue;
            result = maage_entities_spawn_type(entities, world, definition->id, position, 0, 0, NULL);
            if (maage_status_failed(result)) return result;
        }
    }
    return entity_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_CREATE);
}
maage_status maage_entities_living_tick(maage_entities *entities, maage_world *world, const maage_item_registry *items, maage_player *player, maage_gameplay *gameplay, void (*emit_sound)(void *, unsigned long, const double[3], double, double), void *sound_context, maage_status_trace *trace) {
    maage_entity_context context;
    maage_entity entity;
    const maage_entity_type_definition *definition;
    size_t examined, index, initial_count;
    maage_status result;
    double dx, dz, distance;
    if (!entities || !world || !items || !player || !gameplay || !maage_entity_type_registry_is_finalized(entities->types)) return entity_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_TICK);
    memset(&context, 0, sizeof(context));
    context.entities = entities;
    context.world = world;
    context.items = items;
    context.player = player;
    context.gameplay = gameplay;
    context.trace = trace;
    context.emit_sound = emit_sound;
    context.sound_context = sound_context;
    entities->simulation_tick = (entities->simulation_tick + 1) & 4294967295UL;
    initial_count = entities->count;
    for (examined = 0; examined < initial_count && examined < entities->policy.tick_budget; ++examined) {
        index = entities->simulation_cursor % initial_count;
        entities->simulation_cursor = (index + 1) % initial_count;
        entity = entities->data[index];
        if (entity.type != MAAGE_ENTITY_LIVING && entity.type != MAAGE_ENTITY_PROJECTILE) continue;
        definition = maage_entity_type_get(entities->types, entity.registered_type);
        if (!definition) return entity_status(MAAGE_STATUS_INVARIANT_VIOLATION, MAAGE_OPERATION_LOOKUP);
        if (!maage_world_simulation_contains(world, entity.position[0], entity.position[2]) || !entity_body_loaded(world, entity.position, definition->width)) continue;
        memcpy(entity.previous, entity.position, sizeof(entity.previous));
        if (entity.age < 4294967295UL) ++entity.age;
        context.entity = &entity;
        context.definition = definition;
        if (entity.type == MAAGE_ENTITY_PROJECTILE) {
            result = definition->behavior ? definition->behavior(definition, &entity, &context) : entity_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_TICK);
            if (!maage_status_failed(result) && entity.type == MAAGE_ENTITY_PROJECTILE) entity_projectile_tick(&context, &entity, definition);
            entities->data[index] = entity;
            if (maage_status_failed(result)) {
                entity_compact(entities);
                return result;
            }
            if (entity.type && !maage_entity_registered_valid(&entity, items, entities->types)) return entity_status(MAAGE_STATUS_INVARIANT_VIOLATION, MAAGE_OPERATION_TICK);
            continue;
        }
        if (entity.payload.living.hurt_ticks) --entity.payload.living.hurt_ticks;
        if (entity.payload.living.hurt_animation_ticks) --entity.payload.living.hurt_animation_ticks;
        if (entity.payload.living.attack_ticks) --entity.payload.living.attack_ticks;
        if (entity.payload.living.panic_ticks) --entity.payload.living.panic_ticks;
        if (entity.payload.living.idle_ticks < 4294967295UL && !(entity.payload.living.flags & (MAAGE_ENTITY_STATE_DYING | MAAGE_ENTITY_STATE_EXPLODING))) ++entity.payload.living.idle_ticks;
        if (!entity.payload.living.health) {
            if (entity.payload.living.death_ticks < 20) ++entity.payload.living.death_ticks;
            memset(entity.velocity, 0, sizeof(entity.velocity));
            result = entity_living_death(&context, &entity, definition);
        } else {
            result = definition->behavior ? definition->behavior(definition, &entity, &context) : entity_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_TICK);
            if (!maage_status_failed(result) && entity.type == MAAGE_ENTITY_LIVING && !(entity.payload.living.flags & MAAGE_ENTITY_STATE_EXPLODING)) {
                entity_soft_collisions(&context, &entity, definition);
                entity_living_move(&context, &entity, definition);
                entity_living_environment(&context, &entity, definition);
                if (definition->ambient_sound && entity.age % 120 == 0 && entity_random_next(&entities->behavior_random, 3) == 0) entity_sound(&context, definition->ambient_sound, entity.position, 1);
                dx = player->position[0] - entity.position[0];
                dz = player->position[2] - entity.position[2];
                distance = dx * dx + dz * dz;
                if (!(entity.payload.living.flags & MAAGE_ENTITY_STATE_PERSISTENT) && definition->despawn_distance > 0 && distance > definition->despawn_distance * definition->despawn_distance) entity.type = 0;
                else if (!(entity.payload.living.flags & MAAGE_ENTITY_STATE_PERSISTENT) && definition->despawn_ticks && entity.payload.living.idle_ticks >= definition->despawn_ticks && distance > 1024 && entity_random_next(&entities->behavior_random, 800) == 0) entity.type = 0;
            }
        }
        /* Callback-created drops/projectiles can grow the array. The local
         * value retains its owned flat state and commits by stable slot index. */
        entities->data[index] = entity;
        if (maage_status_failed(result)) {
            entity_compact(entities);
            return result;
        }
        if (entity.type && !maage_entity_registered_valid(&entity, items, entities->types)) return entity_status(MAAGE_STATUS_INVARIANT_VIOLATION, MAAGE_OPERATION_TICK);
    }
    entity_compact(entities);
    return entity_natural_spawn(entities, world, player);
}

maage_status maage_entities_player_action(maage_entities *entities, maage_world *world, const maage_item_registry *items, maage_player *player, maage_gameplay *gameplay, double yaw, double pitch, int attack, int use, int *handled, void (*emit_sound)(void *, unsigned long, const double[3], double, double), void *sound_context, maage_status_trace *trace) {
    maage_entity_context context;
    const maage_entity_type_definition *definition;
    const maage_item_definition *item;
    maage_item_stack *held;
    maage_entity entity;
    double origin[3], direction[3], nearest, distance, position[3];
    size_t i, selected = (size_t) - 1;
    unsigned int damage = 1;
    maage_world_target block;
    maage_status result;
    static const int face_x[6] = {-1, 1, 0, 0, 0, 0}, face_y[6] = {0, 0, -1, 1, 0, 0}, face_z[6] = {0, 0, 0, 0, -1, 1};
    if (handled) *handled = 0;
    if (!entities || !world || !items || !player || !gameplay || !handled || gameplay->inventory.selected >= MAAGE_HOTBAR_SLOTS) return entity_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_MUTATE);
    if (!gameplay->health || (!attack && !use)) return entity_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_MUTATE);
    held = &gameplay->inventory.slots[gameplay->inventory.selected];
    item = held->count ? maage_item_registry_get(items, held->item_id) : NULL;
    memcpy(origin, player->position, sizeof(origin));
    origin[1] += MAAGE_PLAYER_EYE_HEIGHT;
    direction[0] = sin(yaw) * cos(pitch);
    direction[1] = -sin(pitch);
    direction[2] = -cos(yaw) * cos(pitch);
    nearest = gameplay->reach > 0 ? gameplay->reach : gameplay->mode == MAAGE_GAME_MODE_CREATIVE ? 5
                                                                                                 : 4.5;
    result = maage_world_raycast(world, origin, direction, nearest, &block);
    if (maage_status_failed(result) && maage_status_code(result) != MAAGE_STATUS_NOT_FOUND) return result;
    nearest = block.distance;
    for (i = 0; i < entities->count; ++i) {
        if (entities->data[i].type != MAAGE_ENTITY_LIVING || !entities->data[i].payload.living.health) continue;
        definition = maage_entity_type_get(entities->types, entities->data[i].registered_type);
        if (!definition || !entity_body_loaded(world, entities->data[i].position, definition->width)) continue;
        memcpy(position, entities->data[i].position, sizeof(position));
        position[1] -= 0.1;
        if (maage_entity_ray_box(origin, direction, position, definition->width + 0.2, definition->height + 0.2, nearest, &distance) && distance < nearest) {
            selected = i;
            nearest = distance;
        }
    }
    if (selected == (size_t) - 1) {
        if (use && item && item->spawn_type && gameplay->has_target && !gameplay->use_delay && gameplay->target.face < MAAGE_FACE_COUNT) {
            gameplay->use_delay = 4;
            position[0] = gameplay->target.x + face_x[gameplay->target.face] + 0.5;
            position[1] = gameplay->target.y + face_y[gameplay->target.face];
            position[2] = gameplay->target.z + face_z[gameplay->target.face] + 0.5;
            result = maage_entities_spawn_type(entities, world, item->spawn_type, position, held->variant, 1, NULL);
            *handled = 1;
            if (maage_status_code(result) == MAAGE_STATUS_SUCCESS && gameplay->mode != MAAGE_GAME_MODE_CREATIVE && !--held->count) memset(held, 0, sizeof(*held));
            return result;
        }
        return entity_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_MUTATE);
    }
    *handled = 1;
    gameplay->has_target = 0;
    entity = entities->data[selected];
    definition = maage_entity_type_get(entities->types, entity.registered_type);
    memset(&context, 0, sizeof(context));
    context.entities = entities;
    context.entity = &entity;
    context.world = world;
    context.definition = definition;
    context.items = items;
    context.player = player;
    context.gameplay = gameplay;
    context.held = held;
    context.creative = gameplay->mode == MAAGE_GAME_MODE_CREATIVE;
    context.trace = trace;
    context.emit_sound = emit_sound;
    context.sound_context = sound_context;
    if (attack == 1) {
        unsigned int health = entity.payload.living.health;
        if (item && item->attack_damage) damage = item->attack_damage;
        entity_hurt(&context, &entity, definition, damage, MAAGE_ENTITY_PLAYER_ID, player->position);
        result = entity_status(entity.payload.living.health != health ? MAAGE_STATUS_SUCCESS : MAAGE_STATUS_NOOP, MAAGE_OPERATION_MUTATE);
    } else if (use && !gameplay->use_delay) {
        gameplay->use_delay = 4;
        result = definition->interact ? definition->interact(definition, &entity, &context, MAAGE_ENTITY_INTERACT_USE) : entity_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_MUTATE);
    } else result = entity_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_MUTATE);
    entities->data[selected] = entity;
    return result;
}
