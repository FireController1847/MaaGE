#include "maage/generation.h"
#include "maage/block_entity.h"
#include <maage/blocks.h>
#include "maage/items.h"
#include "maage/block_geometry.h"
#include "world_internal.h"
#include "maage/internal/registry_pages.h"

#include <stdlib.h>
#include <string.h>
#include <float.h>
#include <limits.h>
#include <math.h>

double maage_block_fluid_height(const maage_block_state_definition *state) {
    if (!state || !state->fluid_kind) return 0;
    if (!state->fluid_definition) return 0;
    return 1.0 - ((state->fluid_level >= state->fluid_definition->flow_levels ? 0 : state->fluid_level) + 1) / ((double) state->fluid_definition->flow_levels + 1.0);
}

void maage_block_fluid_flow(const maage_block_registry *registry, const maage_block_state_definition *state, const maage_block_state_definition *sides[4], const maage_block_state_definition *below[4], const maage_block_state_definition *above[4], double flow[3]) {
    static const int dx[4] = {-1, 1, 0, 0}, dz[4] = {0, 0, -1, 1};
    const maage_block_definition *block;
    const maage_block_state_definition *neighbor;
    unsigned int side, levels;
    int wall = 0;
    double level, difference, length;
    flow[0] = flow[1] = flow[2] = 0;
    if (!state || !state->fluid_kind || !state->fluid_definition) return;
    levels = state->fluid_definition->flow_levels;
    level = state->fluid_level >= levels ? 0 : state->fluid_level;
    for (side = 0; side < 4; ++side) {
        neighbor = sides[side];
        block = neighbor ? maage_block_registry_block(registry, neighbor->block_id) : NULL;
        if (neighbor && neighbor->fluid_kind == state->fluid_kind) difference = (neighbor->fluid_level >= levels ? 0 : (double) neighbor->fluid_level) - level;
        else if (block && !block->material_solid && below[side] && below[side]->fluid_kind == state->fluid_kind) difference = (below[side]->fluid_level >= levels ? 0 : (double) below[side]->fluid_level) - level + levels;
        else difference = 0;
        flow[0] += dx[side] * difference;
        flow[2] += dz[side] * difference;
        if (block && block->material_solid && !block->flow_permeable) wall = 1;
        neighbor = above[side];
        block = neighbor ? maage_block_registry_block(registry, neighbor->block_id) : NULL;
        if (block && block->material_solid && !block->flow_permeable) wall = 1;
    }
    length = sqrt(flow[0] * flow[0] + flow[2] * flow[2]);
    if (length > 0) {
        flow[0] /= length;
        flow[2] /= length;
    }
    if (state->fluid_level >= levels && wall) {
        flow[1] = -6;
        length = sqrt(flow[0] * flow[0] + 36 + flow[2] * flow[2]);
        for (side = 0; side < 3; ++side)
            flow[side] /= length;
    }
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
    maage_registry_bindings bindings;
    const maage_fluid_registry *fluids;
    const maage_sound_registry *sounds;
    const struct maage_container_registry *containers;
    const maage_block_geometry_registry *geometry;
    maage_generation_feature *features;
    char **feature_names;
    size_t feature_count;
    maage_ore_definition *ores;
    char **ore_names;
    size_t ore_count;
    maage_block_entity_handler **entities;
    char **entity_names;
    size_t entity_count;
    maage_registry_pages blocks, states;
    maage_block_slot **name_buckets;
    maage_state_slot **state_buckets;
    size_t bucket_count, state_bucket_count;
    size_t block_count, state_count;
    int finalized;
};
static void maage_block_release(void *value) {
    maage_block_slot *slot = (maage_block_slot *) value;
    free(slot->name);
    free(slot);
}
static void maage_state_release(void *value) {
    free(value);
}
static unsigned long maage_block_name_hash(const char *name) {
    unsigned long hash;
    hash = 2166136261UL;
    while (*name != '\0') {
        hash ^= (unsigned char) *name++;
        hash *= 16777619UL;
    }
    return hash;
}

static int maage_block_name_valid(const char *name) {
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

static size_t maage_block_name_bucket(const maage_block_registry *registry, const char *name, int *found) {
    size_t bucket;
    maage_block_slot *entry;
    bucket = maage_block_name_hash(name) % registry->bucket_count;
    while ((entry = registry->name_buckets[bucket]) != NULL) {
        if (strcmp(entry->name, name) == 0) {
            *found = 1;
            return bucket;
        }
        bucket = (bucket + 1) % registry->bucket_count;
    }
    *found = 0;
    return bucket;
}
static size_t maage_block_state_bucket(const maage_block_registry *registry, maage_block_id block, unsigned int metadata, int *found) {
    unsigned long hash = (block * 2654435761UL) ^ ((unsigned long) metadata * 2246822519UL);
    size_t bucket = (size_t) (hash % registry->state_bucket_count);
    maage_state_slot *entry;
    while ((entry = registry->state_buckets[bucket]) != NULL) {
        if (entry->definition.block_id == block && entry->definition.metadata == metadata) {
            *found = 1;
            return bucket;
        }
        bucket = (bucket + 1) % registry->state_bucket_count;
    }
    *found = 0;
    return bucket;
}
static int maage_block_grow_state_buckets(maage_block_registry *registry) {
    maage_state_slot **buckets, *entry;
    size_t index, bucket, count;
    unsigned long hash;
    if (registry->state_count + 1 < registry->state_bucket_count / 2) return 1;
    if (registry->state_bucket_count > ((size_t) - 1 - 1) / 2) return 0;
    count = registry->state_bucket_count * 2 + 1;
    if (count > (size_t) - 1 / sizeof(*buckets)) return 0;
    buckets = (maage_state_slot **) calloc(count, sizeof(*buckets));
    if (!buckets) return 0;
    for (index = 0; index < registry->state_bucket_count; ++index) {
        entry = registry->state_buckets[index];
        if (!entry) continue;
        hash = entry->definition.block_id * 2654435761UL ^ ((unsigned long) entry->definition.metadata * 2246822519UL);
        bucket = (size_t) (hash % count);
        while (buckets[bucket])
            bucket = (bucket + 1) % count;
        buckets[bucket] = entry;
    }
    free(registry->state_buckets);
    registry->state_buckets = buckets;
    registry->state_bucket_count = count;
    return 1;
}
static int maage_block_grow_names(maage_block_registry *registry) {
    maage_block_slot **buckets, *entry;
    size_t i, bucket, count;
    if (registry->block_count + 1 < registry->bucket_count / 2) return 1;
    if (registry->bucket_count > ((size_t) - 1 - 1) / 2) return 0;
    count = registry->bucket_count * 2 + 1;
    if (count > (size_t) - 1 / sizeof(*buckets)) return 0;
    buckets = (maage_block_slot **) calloc(count, sizeof(*buckets));
    if (!buckets) return 0;
    for (i = 0; i < registry->bucket_count; ++i) {
        entry = registry->name_buckets[i];
        if (!entry) continue;
        bucket = maage_block_name_hash(entry->name) % count;
        while (buckets[bucket])
            bucket = (bucket + 1) % count;
        buckets[bucket] = entry;
    }
    free(registry->name_buckets);
    registry->name_buckets = buckets;
    registry->bucket_count = count;
    return 1;
}
maage_status maage_block_registry_create(size_t block_capacity, size_t state_capacity, maage_block_registry **output) {
    maage_block_registry *registry;
    size_t maximum;
    if (!output) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_CREATE, 2);
    *output = NULL;
    maximum = (size_t) - 1;
    if (!block_capacity) block_capacity = 16;
    if (!state_capacity) state_capacity = 16;
    if (block_capacity > (maximum / sizeof(maage_block_slot *) - 1) / 2 || state_capacity > (maximum / sizeof(maage_state_slot *) - 1) / 2) return maage_status_make(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_CREATE, 0);
    registry = (maage_block_registry *) calloc(1, sizeof(*registry));
    if (!registry) return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_CREATE, 0);
    memset(&registry->bindings, 255, sizeof(registry->bindings));
    registry->bucket_count = block_capacity * 2 + 1;
    registry->state_bucket_count = state_capacity * 2 + 1;
    registry->name_buckets = (maage_block_slot **) calloc(registry->bucket_count, sizeof(*registry->name_buckets));
    registry->state_buckets = (maage_state_slot **) calloc(registry->state_bucket_count, sizeof(*registry->state_buckets));
    if (!registry->name_buckets || !registry->state_buckets) {
        maage_block_registry_destroy(registry);
        return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_CREATE, 0);
    }
    *output = registry;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_CREATE, 0);
}
maage_status maage_block_registry_destroy(maage_block_registry *registry) {
    if (!registry) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_DESTROY, 0);
    {
        size_t i;
        for (i = 0; i < registry->entity_count; ++i) {
            free(registry->entities[i]);
            free(registry->entity_names[i]);
        }
        free(registry->entities);
        free(registry->entity_names);
    }
    {
        size_t i;
        for (i = 0; i < registry->feature_count; ++i)
            free(registry->feature_names[i]);
        for (i = 0; i < registry->ore_count; ++i)
            free(registry->ore_names[i]);
        free(registry->features);
        free(registry->feature_names);
        free(registry->ores);
        free(registry->ore_names);
    }
    maage_registry_pages_destroy(&registry->blocks, maage_block_release);
    maage_registry_pages_destroy(&registry->states, maage_state_release);
    free(registry->name_buckets);
    free(registry->state_buckets);
    free(registry);
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_DESTROY, 0);
}
maage_status maage_block_registry_register_block(maage_block_registry *registry, const char *external_name, maage_block_definition *definition) {
    maage_block_slot *slot;
    void **entry;
    size_t bucket, length;
    int found, axis;
    if (!registry || !definition) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0);
    if (registry->finalized) return maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0);
    if (registry->block_count >= 4294967295UL) return maage_status_make(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0);
    definition->id = (unsigned long) registry->block_count;
    if (definition->id > 4294967295UL || definition->emitted_light > 15 || definition->light_opacity > 15 || !maage_block_name_valid(external_name)) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0);
    if (!(definition->hardness >= -DBL_MAX && definition->hardness <= DBL_MAX) || (definition->falls != 0 && definition->falls != 1)) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0);
    for (axis = 0; axis < 3; ++axis) {
        if (definition->contact_damage && !(definition->contact_shape.minimum[axis] >= 0.0 && definition->contact_shape.maximum[axis] <= 1.0 && definition->contact_shape.minimum[axis] < definition->contact_shape.maximum[axis])) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0);
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
    if (found || maage_block_registry_block(registry, definition->id)) return maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0);
    if (!maage_block_grow_names(registry)) return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0);
    slot = (maage_block_slot *) calloc(1, sizeof(*slot));
    if (!slot) return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0);
    length = strlen(external_name) + 1;
    slot->name = (char *) malloc(length);
    if (!slot->name) {
        free(slot);
        return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0);
    }
    memcpy(slot->name, external_name, length);
    slot->definition = *definition;
    entry = maage_registry_pages_ensure(&registry->blocks, definition->id);
    if (!entry) {
        maage_block_release(slot);
        return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0);
    }
    *entry = slot;
    bucket = maage_block_name_bucket(registry, external_name, &found);
    registry->name_buckets[bucket] = slot;
    ++registry->block_count;
    return maage_status_make(MAAGE_STATUS_SUCCESS, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0);
}
static int maage_block_uv_valid(const float uv[4], int optional) {
    unsigned int component;
    for (component = 0; component < 4; ++component)
        if (!(uv[component] >= 0.0f && uv[component] <= 1.0f)) return 0;
    if (optional && uv[0] == 0.0f && uv[1] == 0.0f && uv[2] == 0.0f && uv[3] == 0.0f) return 1;
    return uv[0] < uv[2] && uv[1] < uv[3];
}
static int maage_block_animation_valid(const float uv[4], const float animation[3]) {
    float count = animation[1] < 0.0f ? -animation[1] : animation[1];
    if (count == 0.0f) return animation[0] == 0.0f && animation[2] == 0.0f;
    return count >= 2.0f && count < (float) INT_MAX && count == (float) (int) count &&
           animation[0] > 0.0f && animation[0] <= 1.0f &&
           animation[2] >= 1.0f && animation[2] <= FLT_MAX &&
           uv[3] + animation[0] * (count - 1.0f) <= 1.0f;
}

maage_status maage_block_registry_register_state(maage_block_registry *registry, maage_block_state_definition *definition) {
    const maage_registry_bindings *ids = maage_block_registry_bindings(registry);
    unsigned int face, component;
    size_t bucket;
    int found;
    const maage_block_face_definition *texture;
    maage_state_slot *slot;
    void **entry;
    if (!registry || !definition) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0);
    if (registry->finalized) return maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0);
    if (registry->state_count >= 4294967295UL) return maage_status_make(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0);
    definition->id = (unsigned long) registry->state_count;
    if (definition->id > 4294967295UL || (definition->random_ticks != 0 && definition->random_ticks != 1) || definition->fluid_kind >= MAAGE_CONTENT_ID_INVALID || (!definition->fluid_kind && definition->fluid_level)) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0);
    if (definition->selection_shape.maximum[0] != 0.0)
        for (component = 0; component < 3; ++component)
            if (!(definition->selection_shape.minimum[component] >= 0.0 && definition->selection_shape.maximum[component] <= 1.0 && definition->selection_shape.minimum[component] < definition->selection_shape.maximum[component])) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0);
    if (!maage_block_registry_block(registry, definition->block_id)) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0);
    if (maage_block_registry_state(registry, definition->id)) return maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0);
    bucket = maage_block_state_bucket(registry, definition->block_id, definition->metadata, &found);
    if (found) return maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0);
    if (definition->block_id != ids->block_air) {
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
                if (!(texture->tint[component] >= 0.0f && texture->tint[component] <= 1.0f)) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0);
            }
        }
    }
    if (!maage_block_grow_state_buckets(registry)) return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0);
    slot = (maage_state_slot *) calloc(1, sizeof(*slot));
    if (!slot) return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0);
    slot->definition = *definition;
    slot->definition.fluid_definition = NULL;
    slot->occupied = 1;
    entry = maage_registry_pages_ensure(&registry->states, definition->id);
    if (!entry) {
        free(slot);
        return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0);
    }
    *entry = slot;
    bucket = maage_block_state_bucket(registry, definition->block_id, definition->metadata, &found);
    registry->state_buckets[bucket] = slot;
    if (definition->block_id == registry->bindings.block_air && !definition->metadata) registry->bindings.state_air = definition->id;
    ++registry->state_count;
    return maage_status_make(MAAGE_STATUS_SUCCESS, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0);
}
static int maage_block_definition_valid(const maage_block_definition *block) {
    unsigned int axis;
    if (!block || block->emitted_light > 15 || block->light_opacity > 15 || block->mesh_material > 2 || !(block->hardness >= -DBL_MAX && block->hardness <= DBL_MAX) || !(block->item_drag >= 0 && block->item_drag <= 1) || (block->special_tool_class && !(block->special_tool_speed >= 1 && block->special_tool_speed <= 1024))) return 0;
    for (axis = 0; axis < 3; ++axis) {
        if (block->solid && !(block->collision_shape.minimum[axis] >= 0 && block->collision_shape.maximum[axis] <= 1 && block->collision_shape.minimum[axis] < block->collision_shape.maximum[axis])) return 0;
        if (block->selectable && !(block->selection_shape.minimum[axis] >= 0 && block->selection_shape.maximum[axis] <= 1 && block->selection_shape.minimum[axis] < block->selection_shape.maximum[axis])) return 0;
        if (block->contact_damage && !(block->contact_shape.minimum[axis] >= 0 && block->contact_shape.maximum[axis] <= 1 && block->contact_shape.minimum[axis] < block->contact_shape.maximum[axis])) return 0;
    }
    return 1;
}
static int maage_block_state_presentation_valid(const maage_block_state_definition *state) {
    unsigned int side, channel;
    const maage_block_face_definition *face;
    if (state->texture_axis > 3) return 0;
    for (side = 0; side < MAAGE_FACE_COUNT; ++side) {
        face = &state->faces[side];
        if ((face->has_flow_texture && face->flow_texture >= MAAGE_CONTENT_ID_INVALID) || face->tint_mode > MAAGE_BLOCK_TINT_GRASS_LOCAL || face->overlay_tint_mode > MAAGE_BLOCK_TINT_GRASS_LOCAL || !maage_block_uv_valid(face->uv, 0) || !maage_block_uv_valid(face->overlay_uv, 1) || !maage_block_uv_valid(face->snow_uv, 1) || !maage_block_uv_valid(face->flow_uv, 1) || !maage_block_animation_valid(face->uv, face->animation) || !maage_block_animation_valid(face->flow_uv, face->flow_animation)) return 0;
        for (channel = 0; channel < 4; ++channel)
            if (!(face->tint[channel] >= 0 && face->tint[channel] <= 1)) return 0;
    }
    return 1;
}
maage_status maage_block_registry_bind_sounds(maage_block_registry *registry, const maage_sound_registry *sounds) {
    if (!registry || registry->finalized || !sounds) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_SET, 0);
    registry->sounds = sounds;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_SET, 0);
}
const maage_sound_registry *maage_block_registry_sounds(const maage_block_registry *registry) {
    return registry ? registry->sounds : NULL;
}
maage_status maage_block_registry_bind_containers(maage_block_registry *registry, const struct maage_container_registry *containers) {
    if (!registry || registry->finalized || !containers) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_SET, 0);
    registry->containers = containers;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_SET, 0);
}
const struct maage_container_registry *maage_block_registry_containers(const maage_block_registry *registry) {
    return registry ? registry->containers : NULL;
}
maage_status maage_block_registry_bind_fluids(maage_block_registry *registry, const maage_fluid_registry *fluids) {
    if (!registry || registry->finalized || !fluids) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_SET, 0);
    registry->fluids = fluids;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_SET, 0);
}
const maage_fluid_registry *maage_block_registry_fluids(const maage_block_registry *registry) {
    return registry ? registry->fluids : NULL;
}
maage_status maage_block_registry_bind_geometry(maage_block_registry *registry, const maage_block_geometry_registry *geometry) {
    if (!registry || registry->finalized || !geometry) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_SET, 0);
    registry->geometry = geometry;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_SET, 0);
}
const maage_block_geometry_registry *maage_block_registry_geometry(const maage_block_registry *registry) {
    return registry ? registry->geometry : NULL;
}
maage_status maage_block_registry_finalize(maage_block_registry *registry) {
    const maage_registry_bindings *ids = maage_block_registry_bindings(registry);
    const maage_block_definition *air;
    const maage_block_state_definition *air_state;
    maage_block_state_definition *state;
    size_t bucket;
    size_t i;
    maage_block_definition *block;
    const maage_block_geometry_definition *geometry;
    if (registry == NULL) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_FINALIZE, 0);
    if (registry->finalized) return maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_FINALIZE, 0);
    air = maage_block_registry_block(registry, ids->block_air);
    air_state = maage_block_registry_state(registry, ids->state_air);
    if (air == NULL || air->solid || air->opaque || air_state == NULL || air_state->block_id != ids->block_air) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_FINALIZE, 0);
    if (!maage_sound_is_finalized(registry->sounds) || !maage_fluid_is_finalized(registry->fluids) || !maage_block_geometry_is_finalized(registry->geometry)) return maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_FINALIZE, 0);
    for (i = 0; i < registry->block_count; ++i) {
        block = (maage_block_definition *) maage_block_registry_block(registry, (maage_block_id) i);
        geometry = block ? maage_block_geometry_get(registry->geometry, block->geometry_id) : NULL;
        if (!geometry) return maage_status_make(MAAGE_STATUS_NOT_FOUND, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_FINALIZE, 0);
        if (block->material && !maage_sound_material_get(registry->sounds, block->material)) return maage_status_make(MAAGE_STATUS_NOT_FOUND, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_FINALIZE, 0);
        block->mesh_shape = geometry->primitive;
        if (!maage_block_definition_valid(block) || (block->unsupported_replaces && !maage_block_registry_state(registry, block->unsupported_state)) || (block->entity && maage_block_registry_entity(registry, block->entity->id) != block->entity)) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_FINALIZE, 0);
    }
    for (bucket = 0; bucket < registry->state_bucket_count; ++bucket) {
        if (!registry->state_buckets[bucket]) continue;
        state = &registry->state_buckets[bucket]->definition;
        if (!maage_block_state_presentation_valid(state)) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_FINALIZE, 0);
        state->fluid_definition = state->fluid_kind ? maage_fluid_get(registry->fluids, state->fluid_kind) : NULL;
        if (!state->fluid_kind) {
            if (state->fluid_level) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_FINALIZE, 0);
            continue;
        }
        if (!state->fluid_definition || state->fluid_level >= state->fluid_definition->state_count || state->fluid_definition->states[state->fluid_level] != state->id) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_FINALIZE, 0);
    }
    registry->finalized = 1;
    return maage_status_make(MAAGE_STATUS_SUCCESS, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_FINALIZE, 0);
}

int maage_block_registry_is_finalized(const maage_block_registry *registry) {
    return registry != NULL && registry->finalized;
}

size_t maage_block_registry_block_count(const maage_block_registry *registry) {
    return registry != NULL ? registry->block_count : 0;
}

size_t maage_block_registry_state_count(const maage_block_registry *registry) {
    return registry != NULL ? registry->state_count : 0;
}

maage_status maage_block_registry_resolve_name(const maage_block_registry *registry, const char *external_name, maage_block_id *block_id) {
    size_t bucket;
    int found;
    if (!registry || !external_name || !block_id) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_RESOLVE, 0);
    bucket = maage_block_name_bucket(registry, external_name, &found);
    if (!found) return maage_status_make(MAAGE_STATUS_NOT_FOUND, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_RESOLVE, 0);
    *block_id = registry->name_buckets[bucket]->definition.id;
    return maage_status_make(MAAGE_STATUS_SUCCESS, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_RESOLVE, 0);
}
const maage_block_definition *maage_block_registry_block(const maage_block_registry *registry, maage_block_id id) {
    void **entry = registry ? maage_registry_pages_find(&registry->blocks, id) : NULL;
    return entry && *entry ? &((maage_block_slot *) *entry)->definition : NULL;
}
const maage_block_state_definition *maage_block_registry_state(const maage_block_registry *registry, maage_block_state_id id) {
    void **entry = registry ? maage_registry_pages_find(&registry->states, id) : NULL;
    return entry && *entry ? &((maage_state_slot *) *entry)->definition : NULL;
}
const char *maage_block_registry_block_name(const maage_block_registry *registry, maage_block_id id) {
    void **entry = registry ? maage_registry_pages_find(&registry->blocks, id) : NULL;
    return entry && *entry ? ((maage_block_slot *) *entry)->name : NULL;
}
maage_status maage_block_registry_resolve_state(const maage_block_registry *registry, maage_block_id block, unsigned int metadata, maage_block_state_id *id) {
    size_t bucket;
    int found;
    if (!registry || !id || !maage_block_registry_block(registry, block)) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_RESOLVE, 0);
    bucket = maage_block_state_bucket(registry, block, metadata, &found);
    if (!found) return maage_status_make(MAAGE_STATUS_NOT_FOUND, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_RESOLVE, 0);
    *id = registry->state_buckets[bucket]->definition.id;
    return maage_status_make(MAAGE_STATUS_SUCCESS, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_RESOLVE, 0);
}

/* Complete item references after both independent domains have been registered. */

const maage_registry_bindings *maage_block_registry_bindings(const maage_block_registry *registry) {
    return registry ? &registry->bindings : NULL;
}
maage_status maage_block_registry_bind(maage_block_registry *registry, const maage_registry_bindings *bindings) {
    if (!registry || registry->finalized || !bindings) return maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0);
    registry->bindings = *bindings;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_REGISTER, 0);
}
maage_status maage_block_registry_update_block(maage_block_registry *registry, const maage_block_definition *definition) {
    void **entry;
    if (!registry || registry->finalized || !definition) return maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0);
    entry = maage_registry_pages_find(&registry->blocks, definition->id);
    if (!entry || !*entry) return maage_status_make(MAAGE_STATUS_NOT_FOUND, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0);
    ((maage_block_slot *) *entry)->definition = *definition;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_REGISTER, 0);
}
maage_status maage_block_registry_update_state(maage_block_registry *registry, const maage_block_state_definition *definition) {
    maage_block_state_definition *state;
    if (!registry || registry->finalized || !definition) return maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0);
    state = (maage_block_state_definition *) maage_block_registry_state(registry, definition->id);
    if (!state || state->block_id != definition->block_id || state->metadata != definition->metadata) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0);
    *state = *definition;
    state->fluid_definition = NULL;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_REGISTER, 0);
}

maage_status maage_block_registry_register_entity(maage_block_registry *registry, const char *name, const maage_block_entity_handler *handler, const maage_block_entity_handler **output) {
    maage_block_entity_handler **handlers, *copy;
    char **names, *identity;
    size_t i, count;
    if (output) *output = NULL;
    if (!registry || registry->finalized || !output || !handler || !maage_block_name_valid(name) || !handler->size || handler->size > 65536UL || !handler->encoded_size || handler->encoded_size > 65536UL || !handler->version || handler->drop_slots > (size_t) - 1 - MAAGE_DROP_STACKS || !handler->snapshot_ticks || !handler->valid || !handler->initialize || !handler->tick || !handler->can_commit || !handler->encode || !handler->decode || (handler->drop_slots && !handler->drop)) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0);
    for (i = 0; i < registry->entity_count; ++i)
        if (!strcmp(registry->entity_names[i], name)) return maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0);
    count = registry->entity_count + 1;
    if (!count || count > 4294967295UL || count > (size_t) - 1 / sizeof(*handlers) || count > (size_t) - 1 / sizeof(*names)) return maage_status_make(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0);
    handlers = (maage_block_entity_handler **) realloc(registry->entities, count * sizeof(*handlers));
    if (!handlers) return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0);
    registry->entities = handlers;
    names = (char **) realloc(registry->entity_names, count * sizeof(*names));
    if (!names) return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0);
    registry->entity_names = names;
    copy = (maage_block_entity_handler *) malloc(sizeof(*copy));
    identity = (char *) malloc(strlen(name) + 1);
    if (!copy || !identity) {
        free(copy);
        free(identity);
        return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0);
    }
    strcpy(identity, name);
    *copy = *handler;
    copy->id = (unsigned long) count;
    registry->entities[registry->entity_count] = copy;
    registry->entity_names[registry->entity_count++] = identity;
    *output = copy;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_REGISTER, 0);
}
const maage_block_entity_handler *maage_block_registry_entity(const maage_block_registry *registry, unsigned long id) {
    return registry && id && id <= registry->entity_count ? registry->entities[id - 1] : NULL;
}

maage_status maage_generation_feature_register(maage_block_registry *registry, const char *name, const maage_generation_feature *definition, unsigned long *id) {
    maage_generation_feature *grown;
    char **names, *copy;
    size_t i, count;
    if (!registry || registry->finalized || !definition || !definition->callback || !maage_block_name_valid(name) || definition->stage >= MAAGE_GENERATION_STAGE_COUNT) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0);
    for (i = 0; i < registry->feature_count; ++i)
        if (!strcmp(name, registry->feature_names[i])) return maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0);
    count = registry->feature_count + 1;
    if (!count || count > 4294967295UL || count > (size_t) - 1 / sizeof(*grown) || count > (size_t) - 1 / sizeof(*names)) return maage_status_make(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0);
    grown = (maage_generation_feature *) realloc(registry->features, count * sizeof(*grown));
    if (!grown) return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0);
    registry->features = grown;
    names = (char **) realloc(registry->feature_names, count * sizeof(*names));
    if (!names) return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0);
    registry->feature_names = names;
    copy = (char *) malloc(strlen(name) + 1);
    if (!copy) return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0);
    strcpy(copy, name);
    registry->features[registry->feature_count] = *definition;
    registry->features[registry->feature_count].id = (unsigned long) registry->feature_count;
    registry->feature_names[registry->feature_count] = copy;
    if (id) *id = (unsigned long) registry->feature_count;
    ++registry->feature_count;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_REGISTER, 0);
}
maage_status maage_generation_ore_register(maage_block_registry *registry, const char *name, const maage_ore_definition *definition, unsigned long *id) {
    maage_ore_definition *grown;
    char **names, *copy;
    size_t i, count;
    if (!registry || registry->finalized || !definition || !maage_block_name_valid(name) || !definition->size || definition->size > 4096 || definition->minimum_y >= definition->maximum_y || definition->maximum_y > MAAGE_WORLD_HEIGHT || !maage_block_registry_state(registry, definition->state) || definition->salt > 4294967295UL) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0);
    for (i = 0; i < registry->ore_count; ++i)
        if (!strcmp(name, registry->ore_names[i])) return maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0);
    count = registry->ore_count + 1;
    if (!count || count > 4294967295UL || count > (size_t) - 1 / sizeof(*grown) || count > (size_t) - 1 / sizeof(*names)) return maage_status_make(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0);
    grown = (maage_ore_definition *) realloc(registry->ores, count * sizeof(*grown));
    if (!grown) return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0);
    registry->ores = grown;
    names = (char **) realloc(registry->ore_names, count * sizeof(*names));
    if (!names) return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0);
    registry->ore_names = names;
    copy = (char *) malloc(strlen(name) + 1);
    if (!copy) return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0);
    strcpy(copy, name);
    registry->ores[registry->ore_count] = *definition;
    registry->ore_names[registry->ore_count] = copy;
    if (id) *id = (unsigned long) registry->ore_count;
    ++registry->ore_count;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_REGISTER, 0);
}
size_t maage_generation_ore_count(const maage_block_registry *registry) {
    return registry ? registry->ore_count : 0;
}
const maage_ore_definition *maage_generation_ore_at(const maage_block_registry *registry, size_t index) {
    return registry && index < registry->ore_count ? &registry->ores[index] : NULL;
}
maage_status maage_generation_features_run(const maage_block_registry *registry, unsigned int stage, maage_chunk *chunk, maage_world_seed seed, const maage_biome_registry *biomes, const maage_generation_policy *policy, const maage_job_token *token, maage_status_trace *trace) {
    size_t i;
    maage_status result;
    if (!registry || !registry->finalized || !chunk) return maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_GENERATE, 0);
    for (i = 0; i < registry->feature_count; ++i) {
        if (registry->features[i].stage != stage) continue;
        if (token && maage_job_cancelled(token)) return maage_status_make(MAAGE_STATUS_INTERRUPTED, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_GENERATE, 0);
        result = registry->features[i].callback(registry->features[i].context, chunk, seed, registry, biomes, policy, token, trace);
        if (maage_status_failed(result)) return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_GENERATE);
    }
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_GENERATE, 0);
}
