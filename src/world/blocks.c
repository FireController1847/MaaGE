#include <maage/blocks.h>
#include "maage/internal/registry_pages.h"

#include <stdlib.h>
#include <string.h>
#include <float.h>
#include <limits.h>

maage_status maage_block_leaves_state(unsigned int metadata, maage_block_state_id *state)
{
    static const maage_block_state_id checked[3] = {
        MAAGE_STATE_LEAVES, MAAGE_STATE_SPRUCE_LEAVES, MAAGE_STATE_BIRCH_LEAVES
    };
    unsigned int species = metadata & MAAGE_LEAF_SPECIES_MASK;
    if (state) *state = 0;
    if (!state || metadata > 15 || species > 2)
        return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_RESOLVE, 0);
    *state = metadata < 4 ? checked[species] :
        MAAGE_STATE_PERSISTENT_LEAVES_BASE + ((metadata >> 2) - 1) * 3 + species;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_RESOLVE, 0);
}

typedef struct maage_block_slot {
    maage_block_definition definition;
    char *name;
} maage_block_slot;

typedef struct maage_state_slot {
    maage_block_state_definition definition;
    int occupied;
} maage_state_slot;

struct maage_block_registry {
    maage_registry_pages blocks, states;
    maage_block_slot **name_buckets;
    maage_state_slot **state_buckets;
    size_t bucket_count, state_bucket_count;
    size_t block_count, state_count;
    int finalized;
};
static void maage_block_release(void *value)
{
    maage_block_slot *slot = (maage_block_slot *)value;
    free(slot->name);
    free(slot);
}
static void maage_state_release(void *value)
{
    free(value);
}
static unsigned long maage_block_name_hash(const char *name)
{
    unsigned long hash;
    hash = 2166136261UL;
    while (*name != '\0') {
        hash ^= (unsigned char)*name++;
        hash *= 16777619UL;
    }
    return hash;
}

static int maage_block_name_valid(const char *name)
{
    size_t length;
    int colon;
    char value;
    if (name == NULL || *name == '\0') return 0;
    colon = 0;
    length = 0;
    while ((value = name[length]) != '\0') {
        if (length >= 255) return 0;
        if (value == ':') {
            if (colon || length == 0 || name[length + 1] == '\0') return 0;
            colon = 1;
        } else if (!((value >= 'a' && value <= 'z') ||
            (value >= '0' && value <= '9') || value == '_' || value == '-' ||
            value == '.' || (colon && value == '/'))) {
            return 0;
        }
        ++length;
    }
    return colon;
}

static size_t maage_block_name_bucket(const maage_block_registry *registry,
    const char *name, int *found)
{
    size_t bucket;
    maage_block_slot *entry;
    bucket = maage_block_name_hash(name) % registry->bucket_count;
    while ((entry = registry->name_buckets[bucket]) != NULL) {
        if (strcmp(entry->name, name) == 0) { *found = 1; return bucket; }
        bucket = (bucket + 1) % registry->bucket_count;
    }
    *found = 0;
    return bucket;
}
static size_t maage_block_state_bucket(const maage_block_registry *registry,
    maage_block_id block, unsigned int metadata, int *found)
{
    unsigned long hash = (block * 2654435761UL) ^ ((unsigned long)metadata * 2246822519UL);
    size_t bucket = (size_t)(hash % registry->state_bucket_count);
    maage_state_slot *entry;
    while ((entry = registry->state_buckets[bucket]) != NULL) {
        if (entry->definition.block_id == block && entry->definition.metadata == metadata) {
            *found = 1; return bucket;
        }
        bucket = (bucket + 1) % registry->state_bucket_count;
    }
    *found = 0;
    return bucket;
}
static int maage_block_grow_state_buckets(maage_block_registry *registry)
{
    maage_state_slot **buckets, *entry;
    size_t index, bucket, count;
    unsigned long hash;
    if (registry->state_count + 1 < registry->state_bucket_count / 2) return 1;
    if (registry->state_bucket_count > ((size_t)-1 - 1) / 2) return 0;
    count = registry->state_bucket_count * 2 + 1;
    if (count > (size_t)-1 / sizeof(*buckets)) return 0;
    buckets = (maage_state_slot **)calloc(count, sizeof(*buckets));
    if (!buckets) return 0;
    for (index = 0; index < registry->state_bucket_count; ++index) {
        entry = registry->state_buckets[index];
        if (!entry) continue;
        hash = entry->definition.block_id * 2654435761UL ^
            ((unsigned long)entry->definition.metadata * 2246822519UL);
        bucket = (size_t)(hash % count);
        while (buckets[bucket]) bucket = (bucket + 1) % count;
        buckets[bucket] = entry;
    }
    free(registry->state_buckets);
    registry->state_buckets = buckets;
    registry->state_bucket_count = count;
    return 1;
}
static int maage_block_grow_names(maage_block_registry *registry)
{
    maage_block_slot **buckets, *entry;
    size_t i, bucket, count;
    if (registry->block_count + 1 < registry->bucket_count / 2) return 1;
    if (registry->bucket_count > ((size_t)-1 - 1) / 2) return 0;
    count = registry->bucket_count * 2 + 1;
    if (count > (size_t)-1 / sizeof(*buckets)) return 0;
    buckets = (maage_block_slot **)calloc(count, sizeof(*buckets));
    if (!buckets) return 0;
    for (i = 0; i < registry->bucket_count; ++i) {
        entry = registry->name_buckets[i];
        if (!entry) continue;
        bucket = maage_block_name_hash(entry->name) % count;
        while (buckets[bucket]) bucket = (bucket + 1) % count;
        buckets[bucket] = entry;
    }
    free(registry->name_buckets);
    registry->name_buckets = buckets;
    registry->bucket_count = count;
    return 1;
}
maage_status maage_block_registry_create(size_t block_capacity,
    size_t state_capacity, maage_block_registry **output)
{
    maage_block_registry *registry;
    size_t maximum;
    if (!output) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT,
        MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_CREATE, 2);
    *output = NULL;
    maximum = (size_t)-1;
    if (!block_capacity) block_capacity = 16;
    if (!state_capacity) state_capacity = 16;
    if (block_capacity > (maximum / sizeof(maage_block_slot *) - 1) / 2 ||
        state_capacity > (maximum / sizeof(maage_state_slot *) - 1) / 2)
        return maage_status_make(MAAGE_STATUS_BUFFER_OVERFLOW,
            MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_CREATE, 0);
    registry = (maage_block_registry *)calloc(1, sizeof(*registry));
    if (!registry) return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED,
        MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_CREATE, 0);
    registry->bucket_count = block_capacity * 2 + 1;
    registry->state_bucket_count = state_capacity * 2 + 1;
    registry->name_buckets = (maage_block_slot **)calloc(registry->bucket_count,
        sizeof(*registry->name_buckets));
    registry->state_buckets = (maage_state_slot **)calloc(registry->state_bucket_count,
        sizeof(*registry->state_buckets));
    if (!registry->name_buckets || !registry->state_buckets) {
        maage_block_registry_destroy(registry);
        return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED,
            MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_CREATE, 0);
    }
    *output = registry;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_CREATE, 0);
}
maage_status maage_block_registry_destroy(maage_block_registry *registry)
{
    if (!registry) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_DESTROY, 0);
    maage_registry_pages_destroy(&registry->blocks, maage_block_release);
    maage_registry_pages_destroy(&registry->states, maage_state_release);
    free(registry->name_buckets);
    free(registry->state_buckets);
    free(registry);
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_DESTROY, 0);
}
maage_status maage_block_registry_register_block(
    maage_block_registry *registry, const char *external_name,
    const maage_block_definition *definition)
{
    maage_block_slot *slot;
    void **entry;
    size_t bucket, length;
    int found, axis;
    if (!registry || !definition) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT,
        MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0);
    if (registry->finalized) return maage_status_make(MAAGE_STATUS_INVALID_STATE,
        MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0);
    if (definition->id > 4294967295UL || definition->emitted_light > 15 ||
        definition->light_opacity > 15 || definition->mesh_shape > MAAGE_BLOCK_MESH_TORCH ||
        !maage_block_name_valid(external_name))
        return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0);
    if (!(definition->hardness >= -DBL_MAX && definition->hardness <= DBL_MAX))
        return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0);
    for (axis = 0; axis < 3; ++axis) {
        if (definition->solid && !(definition->collision_shape.minimum[axis] >= 0.0 &&
            definition->collision_shape.maximum[axis] <= 1.0 &&
            definition->collision_shape.minimum[axis] < definition->collision_shape.maximum[axis]))
            return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0);
        if (definition->selectable && !(definition->selection_shape.minimum[axis] >= 0.0 &&
            definition->selection_shape.maximum[axis] <= 1.0 &&
            definition->selection_shape.minimum[axis] < definition->selection_shape.maximum[axis]))
            return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0);
    }
    bucket = maage_block_name_bucket(registry, external_name, &found);
    if (found || maage_block_registry_block(registry, definition->id))
        return maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0);
    if (!maage_block_grow_names(registry))
        return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0);
    slot = (maage_block_slot *)calloc(1, sizeof(*slot));
    if (!slot) return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED,
        MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0);
    length = strlen(external_name) + 1;
    slot->name = (char *)malloc(length);
    if (!slot->name) { free(slot); return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED,
        MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0); }
    memcpy(slot->name, external_name, length);
    slot->definition = *definition;
    entry = maage_registry_pages_ensure(&registry->blocks, definition->id);
    if (!entry) { maage_block_release(slot); return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED,
        MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0); }
    *entry = slot;
    bucket = maage_block_name_bucket(registry, external_name, &found);
    registry->name_buckets[bucket] = slot;
    ++registry->block_count;
    return maage_status_make(MAAGE_STATUS_SUCCESS, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0);
}
static int maage_block_uv_valid(const float uv[4], int optional)
{
    unsigned int component;
    for (component = 0; component < 4; ++component)
        if (!(uv[component] >= 0.0f && uv[component] <= 1.0f)) return 0;
    if (optional && uv[0] == 0.0f && uv[1] == 0.0f && uv[2] == 0.0f && uv[3] == 0.0f)
        return 1;
    return uv[0] < uv[2] && uv[1] < uv[3];
}
static int maage_block_animation_valid(const float uv[4], const float animation[3])
{
    float count = animation[1] < 0.0f ? -animation[1] : animation[1];
    if (count == 0.0f) return animation[0] == 0.0f && animation[2] == 0.0f;
    return count >= 2.0f && count < (float)INT_MAX && count == (float)(int)count &&
        animation[0] > 0.0f && animation[0] <= 1.0f &&
        animation[2] >= 1.0f && animation[2] <= FLT_MAX &&
        uv[3] + animation[0] * (count - 1.0f) <= 1.0f;
}

maage_status maage_block_registry_register_state(
    maage_block_registry *registry,
    const maage_block_state_definition *definition)
{
    unsigned int face, component;
    size_t bucket;
    int found;
    const maage_block_face_definition *texture;
    maage_state_slot *slot;
    void **entry;
    if (!registry || !definition) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT,
        MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0);
    if (registry->finalized) return maage_status_make(MAAGE_STATUS_INVALID_STATE,
        MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0);
    if (definition->id > 4294967295UL || (definition->random_ticks != 0 && definition->random_ticks != 1) ||
        definition->fluid_kind > MAAGE_FLUID_LAVA ||
        definition->fluid_level > 15 || (definition->fluid_kind == MAAGE_FLUID_NONE &&
        (definition->fluid_level || definition->fluid_source_state)))
        return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0);
    if (definition->fluid_kind == MAAGE_FLUID_NONE)
        for (component = 0; component < 16; ++component)
            if (definition->fluid_states[component]) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0);
    if (definition->selection_shape.maximum[0] != 0.0)
        for (component = 0; component < 3; ++component)
            if (!(definition->selection_shape.minimum[component] >= 0.0 &&
                definition->selection_shape.maximum[component] <= 1.0 &&
                definition->selection_shape.minimum[component] < definition->selection_shape.maximum[component]))
                return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0);
    if (!maage_block_registry_block(registry, definition->block_id))
        return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0);
    if (maage_block_registry_state(registry, definition->id))
        return maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0);
    bucket = maage_block_state_bucket(registry, definition->block_id,
        definition->metadata, &found);
    if (found) return maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0);
    if (definition->block_id != MAAGE_BLOCK_AIR) {
        for (face = 0; face < MAAGE_FACE_COUNT; ++face) {
            texture = &definition->faces[face];
            if (texture->tint_mode > MAAGE_BLOCK_TINT_GRASS_LOCAL ||
                texture->overlay_tint_mode > MAAGE_BLOCK_TINT_GRASS_LOCAL ||
                !maage_block_uv_valid(texture->uv, 0) ||
                !maage_block_uv_valid(texture->overlay_uv, 1) ||
                !maage_block_uv_valid(texture->snow_uv, 1) ||
                !maage_block_uv_valid(texture->flow_uv, 1) ||
                !maage_block_animation_valid(texture->uv, texture->animation) ||
                !maage_block_animation_valid(texture->flow_uv, texture->flow_animation))
                return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0);
            for (component = 0; component < 4; ++component) {
                if (!(texture->tint[component] >= 0.0f &&
                    texture->tint[component] <= 1.0f))
                    return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0);
            }
        }
    }
    if (!maage_block_grow_state_buckets(registry))
        return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0);
    slot = (maage_state_slot *)calloc(1, sizeof(*slot));
    if (!slot) return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED,
        MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0);
    slot->definition = *definition;
    slot->occupied = 1;
    entry = maage_registry_pages_ensure(&registry->states, definition->id);
    if (!entry) { free(slot); return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED,
        MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0); }
    *entry = slot;
    bucket = maage_block_state_bucket(registry, definition->block_id,
        definition->metadata, &found);
    registry->state_buckets[bucket] = slot;
    ++registry->state_count;
    return maage_status_make(MAAGE_STATUS_SUCCESS, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0);
}
maage_status maage_block_registry_finalize(maage_block_registry *registry)
{
    const maage_block_definition *air;
    const maage_block_state_definition *air_state;
    const maage_block_state_definition *state, *flow;
    size_t bucket;
    unsigned int level;
    if (registry == NULL) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_FINALIZE, 0);
    if (registry->finalized) return maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_FINALIZE, 0);
    air = maage_block_registry_block(registry, MAAGE_BLOCK_AIR);
    air_state = maage_block_registry_state(registry, MAAGE_STATE_AIR);
    if (air == NULL || air->solid || air->opaque || air_state == NULL ||
        air_state->block_id != MAAGE_BLOCK_AIR) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_FINALIZE, 0);
    for (bucket=0;bucket<registry->state_bucket_count;++bucket) {
        if (!registry->state_buckets[bucket]) continue;
        state=&registry->state_buckets[bucket]->definition;
        if (!state->fluid_kind) continue;
        if (state->fluid_states[0]!=state->fluid_source_state) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_FINALIZE, 0);
        for (level=0;level<16;++level) {
            flow=maage_block_registry_state(registry,state->fluid_states[level]);
            if (!flow || flow->fluid_kind!=state->fluid_kind || flow->fluid_level!=level ||
                flow->block_id!=state->block_id || flow->fluid_source_state!=state->fluid_source_state)
                return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_FINALIZE, 0);
        }
        if (state->fluid_states[state->fluid_level]!=state->id) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_FINALIZE, 0);
    }
    registry->finalized = 1;
    return maage_status_make(MAAGE_STATUS_SUCCESS, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_FINALIZE, 0);
}

int maage_block_registry_is_finalized(const maage_block_registry *registry)
{
    return registry != NULL && registry->finalized;
}

size_t maage_block_registry_block_count(const maage_block_registry *registry)
{
    return registry != NULL ? registry->block_count : 0;
}

size_t maage_block_registry_state_count(const maage_block_registry *registry)
{
    return registry != NULL ? registry->state_count : 0;
}

maage_status maage_block_registry_resolve_name(
    const maage_block_registry *registry, const char *external_name,
    maage_block_id *block_id)
{
    size_t bucket;
    int found;
    if (!registry || !external_name || !block_id)
        return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_RESOLVE, 0);
    bucket = maage_block_name_bucket(registry, external_name, &found);
    if (!found) return maage_status_make(MAAGE_STATUS_NOT_FOUND, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_RESOLVE, 0);
    *block_id = registry->name_buckets[bucket]->definition.id;
    return maage_status_make(MAAGE_STATUS_SUCCESS, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_RESOLVE, 0);
}
const maage_block_definition *maage_block_registry_block(
    const maage_block_registry *registry, maage_block_id id)
{
    void **entry = registry ? maage_registry_pages_find(&registry->blocks, id) : NULL;
    return entry && *entry ? &((maage_block_slot *)*entry)->definition : NULL;
}
const maage_block_state_definition *maage_block_registry_state(
    const maage_block_registry *registry, maage_block_state_id id)
{
    void **entry = registry ? maage_registry_pages_find(&registry->states, id) : NULL;
    return entry && *entry ? &((maage_state_slot *)*entry)->definition : NULL;
}
const char *maage_block_registry_block_name(
    const maage_block_registry *registry, maage_block_id id)
{
    void **entry = registry ? maage_registry_pages_find(&registry->blocks, id) : NULL;
    return entry && *entry ? ((maage_block_slot *)*entry)->name : NULL;
}
maage_status maage_block_registry_resolve_state(
    const maage_block_registry *registry, maage_block_id block,
    unsigned int metadata, maage_block_state_id *id)
{
    size_t bucket;
    int found;
    if (!registry || !id || !maage_block_registry_block(registry, block))
        return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_RESOLVE, 0);
    bucket = maage_block_state_bucket(registry, block, metadata, &found);
    if (!found) return maage_status_make(MAAGE_STATUS_NOT_FOUND, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_RESOLVE, 0);
    *id = registry->state_buckets[bucket]->definition.id;
    return maage_status_make(MAAGE_STATUS_SUCCESS, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_RESOLVE, 0);
}
