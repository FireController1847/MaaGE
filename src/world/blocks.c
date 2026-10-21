#include <maage/blocks.h>

#include <stdlib.h>
#include <string.h>
#include <float.h>

typedef struct maage_block_slot {
    maage_block_definition definition;
    char *name;
} maage_block_slot;

typedef struct maage_state_slot {
    maage_block_state_definition definition;
    int occupied;
} maage_state_slot;

struct maage_block_registry {
    maage_block_slot *blocks;
    maage_state_slot *states;
    /* Zero is empty; occupied buckets hold block index plus one. */
    size_t *name_buckets;
    size_t bucket_count;
    size_t block_capacity;
    size_t state_capacity;
    size_t block_count;
    size_t state_count;
    int finalized;
};

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
    size_t entry;
    bucket = maage_block_name_hash(name) % registry->bucket_count;
    while ((entry = registry->name_buckets[bucket]) != 0) {
        if (strcmp(registry->blocks[entry - 1].name, name) == 0) {
            *found = 1;
            return bucket;
        }
        bucket = (bucket + 1) % registry->bucket_count;
    }
    *found = 0;
    return bucket;
}

static int maage_block_grow_blocks(maage_block_registry *registry, maage_block_id id)
{
    size_t count, old;
    maage_block_slot *slots;
    if (id < registry->block_capacity) return 1;
    if (id >= (size_t)-1 / sizeof(*slots)) return 0;
    old = registry->block_capacity;
    count = old;
    while (count <= id) {
        if (count > ((size_t)-1 / sizeof(*slots)) / 2) {
            count = (size_t)id + 1;
            break;
        }
        count *= 2;
    }
    slots = (maage_block_slot *)realloc(registry->blocks, count * sizeof(*slots));
    if (!slots) return 0;
    memset(slots + old, 0, (count - old) * sizeof(*slots));
    registry->blocks = slots;
    registry->block_capacity = count;
    return 1;
}
static int maage_block_grow_states(maage_block_registry *registry, maage_block_state_id id)
{
    size_t count, old;
    maage_state_slot *slots;
    if (id < registry->state_capacity) return 1;
    if (id >= (size_t)-1 / sizeof(*slots)) return 0;
    old = registry->state_capacity;
    count = old;
    while (count <= id) {
        if (count > ((size_t)-1 / sizeof(*slots)) / 2) {
            count = (size_t)id + 1;
            break;
        }
        count *= 2;
    }
    slots = (maage_state_slot *)realloc(registry->states, count * sizeof(*slots));
    if (!slots) return 0;
    memset(slots + old, 0, (count - old) * sizeof(*slots));
    registry->states = slots;
    registry->state_capacity = count;
    return 1;
}
static int maage_block_grow_names(maage_block_registry *registry)
{
    size_t *buckets, i, bucket, count;
    if (registry->block_count + 1 < registry->bucket_count / 2) return 1;
    if (registry->bucket_count > ((size_t)-1 - 1) / 2) return 0;
    count = registry->bucket_count * 2 + 1;
    if (count > (size_t)-1 / sizeof(*buckets)) return 0;
    buckets = (size_t *)calloc(count, sizeof(*buckets));
    if (!buckets) return 0;
    for (i = 0; i < registry->bucket_count; ++i) {
        size_t entry = registry->name_buckets[i];
        if (!entry) continue;
        bucket = maage_block_name_hash(registry->blocks[entry - 1].name) % count;
        while (buckets[bucket]) bucket = (bucket + 1) % count;
        buckets[bucket] = entry;
    }
    free(registry->name_buckets);
    registry->name_buckets = buckets;
    registry->bucket_count = count;
    return 1;
}

maage_block_registry *maage_block_registry_create(size_t block_capacity,
    size_t state_capacity)
{
    maage_block_registry *registry;
    size_t maximum;
    maximum = (size_t)-1;
    if (!block_capacity) block_capacity = 16;
    if (!state_capacity) state_capacity = 16;
    if (
        block_capacity > maximum / sizeof(maage_block_slot) ||
        state_capacity > maximum / sizeof(maage_state_slot) ||
        block_capacity > (maximum / sizeof(size_t) - 1) / 2) return NULL;
    registry = (maage_block_registry *)calloc(1, sizeof(*registry));
    if (registry == NULL) return NULL;
    registry->block_capacity = block_capacity;
    registry->state_capacity = state_capacity;
    registry->bucket_count = block_capacity * 2 + 1;
    registry->blocks = (maage_block_slot *)calloc(block_capacity,
        sizeof(*registry->blocks));
    registry->states = (maage_state_slot *)calloc(state_capacity,
        sizeof(*registry->states));
    registry->name_buckets = (size_t *)calloc(registry->bucket_count,
        sizeof(*registry->name_buckets));
    if (registry->blocks == NULL || registry->states == NULL ||
        registry->name_buckets == NULL) {
        maage_block_registry_destroy(registry);
        return NULL;
    }
    return registry;
}

void maage_block_registry_destroy(maage_block_registry *registry)
{
    size_t index;
    if (registry == NULL) return;
    if (registry->blocks != NULL) {
        for (index = 0; index < registry->block_capacity; ++index)
            free(registry->blocks[index].name);
    }
    free(registry->name_buckets);
    free(registry->states);
    free(registry->blocks);
    free(registry);
}

maage_registry_result maage_block_registry_register_block(
    maage_block_registry *registry, const char *external_name,
    const maage_block_definition *definition)
{
    size_t bucket;
    size_t length;
    int found;
    char *copy;
    int axis;
    if (registry == NULL || definition == NULL) return MAAGE_REGISTRY_INVALID;
    if (registry->finalized) return MAAGE_REGISTRY_FROZEN;
    if (definition->emitted_light > 15 ||
        !maage_block_name_valid(external_name)) return MAAGE_REGISTRY_INVALID;
    if (!(definition->hardness >= -DBL_MAX && definition->hardness <= DBL_MAX))
        return MAAGE_REGISTRY_INVALID;
    for (axis = 0; axis < 3; ++axis) {
        if (definition->solid && !(definition->collision_shape.minimum[axis] >= 0.0 &&
            definition->collision_shape.maximum[axis] <= 1.0 &&
            definition->collision_shape.minimum[axis] < definition->collision_shape.maximum[axis]))
            return MAAGE_REGISTRY_INVALID;
        if (definition->selectable && !(definition->selection_shape.minimum[axis] >= 0.0 &&
            definition->selection_shape.maximum[axis] <= 1.0 &&
            definition->selection_shape.minimum[axis] < definition->selection_shape.maximum[axis]))
            return MAAGE_REGISTRY_INVALID;
    }
    /* Reject a duplicate name before growing a potentially sparse ID domain. */
    bucket = maage_block_name_bucket(registry, external_name, &found);
    if (found) return MAAGE_REGISTRY_DUPLICATE;
    if (!maage_block_grow_blocks(registry, definition->id) ||
        !maage_block_grow_names(registry)) return MAAGE_REGISTRY_OUT_OF_MEMORY;
    if (registry->blocks[definition->id].name != NULL)
        return MAAGE_REGISTRY_DUPLICATE;
    bucket = maage_block_name_bucket(registry, external_name, &found);
    length = strlen(external_name) + 1;
    copy = (char *)malloc(length);
    if (copy == NULL) return MAAGE_REGISTRY_OUT_OF_MEMORY;
    memcpy(copy, external_name, length);
    registry->blocks[definition->id].name = copy;
    registry->blocks[definition->id].definition = *definition;
    registry->name_buckets[bucket] = (size_t)definition->id + 1;
    ++registry->block_count;
    return MAAGE_REGISTRY_OK;
}

maage_registry_result maage_block_registry_register_state(
    maage_block_registry *registry,
    const maage_block_state_definition *definition)
{
    unsigned int face;
    unsigned int component;
    const maage_block_face_definition *texture;
    if (registry == NULL || definition == NULL) return MAAGE_REGISTRY_INVALID;
    if (registry->finalized) return MAAGE_REGISTRY_FROZEN;
    if (maage_block_registry_block(registry, definition->block_id) == NULL)
        return MAAGE_REGISTRY_INVALID;
    if (!maage_block_grow_states(registry, definition->id))
        return MAAGE_REGISTRY_OUT_OF_MEMORY;
    if (registry->states[definition->id].occupied)
        return MAAGE_REGISTRY_DUPLICATE;
    /* A stable (namespaced block, metadata) pair must never be ambiguous
     * when a saved palette is resolved after registry reordering. */
    {
        size_t index;
        for (index = 0; index < registry->state_capacity; ++index) {
            if (registry->states[index].occupied &&
                registry->states[index].definition.block_id == definition->block_id &&
                registry->states[index].definition.metadata == definition->metadata)
                return MAAGE_REGISTRY_DUPLICATE;
        }
    }
    if (definition->block_id != MAAGE_BLOCK_AIR) {
        for (face = 0; face < MAAGE_FACE_COUNT; ++face) {
            texture = &definition->faces[face];
            for (component = 0; component < 4; ++component) {
                if (!(texture->uv[component] >= 0.0f &&
                    texture->uv[component] <= 1.0f &&
                    texture->tint[component] >= 0.0f &&
                    texture->tint[component] <= 1.0f))
                    return MAAGE_REGISTRY_INVALID;
            }
            if (texture->uv[0] >= texture->uv[2] ||
                texture->uv[1] >= texture->uv[3]) return MAAGE_REGISTRY_INVALID;
        }
    }
    registry->states[definition->id].definition = *definition;
    registry->states[definition->id].occupied = 1;
    ++registry->state_count;
    return MAAGE_REGISTRY_OK;
}

maage_registry_result maage_block_registry_finalize(maage_block_registry *registry)
{
    const maage_block_definition *air;
    const maage_block_state_definition *air_state;
    if (registry == NULL) return MAAGE_REGISTRY_INVALID;
    if (registry->finalized) return MAAGE_REGISTRY_FROZEN;
    air = maage_block_registry_block(registry, MAAGE_BLOCK_AIR);
    air_state = maage_block_registry_state(registry, MAAGE_STATE_AIR);
    if (air == NULL || air->solid || air->opaque || air_state == NULL ||
        air_state->block_id != MAAGE_BLOCK_AIR) return MAAGE_REGISTRY_INVALID;
    registry->finalized = 1;
    return MAAGE_REGISTRY_OK;
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

maage_registry_result maage_block_registry_resolve_name(
    const maage_block_registry *registry, const char *external_name,
    maage_block_id *block_id)
{
    size_t bucket;
    int found;
    if (registry == NULL || external_name == NULL || block_id == NULL)
        return MAAGE_REGISTRY_INVALID;
    bucket = maage_block_name_bucket(registry, external_name, &found);
    if (!found) return MAAGE_REGISTRY_INVALID;
    *block_id = (maage_block_id)(registry->name_buckets[bucket] - 1);
    return MAAGE_REGISTRY_OK;
}

const maage_block_definition *maage_block_registry_block(
    const maage_block_registry *registry, maage_block_id id)
{
    if (registry == NULL || id >= registry->block_capacity ||
        registry->blocks[id].name == NULL) return NULL;
    return &registry->blocks[id].definition;
}

const maage_block_state_definition *maage_block_registry_state(
    const maage_block_registry *registry, maage_block_state_id id)
{
    if (registry == NULL || id >= registry->state_capacity ||
        !registry->states[id].occupied) return NULL;
    return &registry->states[id].definition;
}

const char *maage_block_registry_block_name(
    const maage_block_registry *registry, maage_block_id id)
{
    return maage_block_registry_block(registry, id) ? registry->blocks[id].name : NULL;
}
maage_registry_result maage_block_registry_resolve_state(
    const maage_block_registry *registry, maage_block_id block,
    unsigned int metadata, maage_block_state_id *id)
{
    size_t index;
    if (!registry || !id || !maage_block_registry_block(registry, block))
        return MAAGE_REGISTRY_INVALID;
    for (index = 0; index < registry->state_capacity; ++index) {
        if (registry->states[index].occupied &&
            registry->states[index].definition.block_id == block &&
            registry->states[index].definition.metadata == metadata) {
            *id = (maage_block_state_id)index;
            return MAAGE_REGISTRY_OK;
        }
    }
    return MAAGE_REGISTRY_INVALID;
}
