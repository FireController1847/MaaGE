#include "maage/fluids.h"
#include "maage/blocks.h"
#include "maage/internal/registry_pages.h"
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <float.h>

typedef struct maage_fluid_slot {
    maage_fluid_definition definition;
    char *name;
    unsigned long *states;
} maage_fluid_slot;
struct maage_fluid_registry {
    maage_registry_pages definitions;
    maage_fluid_slot **names;
    size_t count, buckets;
    int finalized;
};
static maage_fluid_slot *fluid_slot(const maage_fluid_registry *registry, maage_fluid_id id) {
    void **entry = registry ? maage_registry_pages_find(&registry->definitions, id) : NULL;
    return entry ? (maage_fluid_slot *) *entry : NULL;
}
static maage_status fluid_status(unsigned char code, unsigned char operation) {
    return maage_status_make(code, code < MAAGE_STATUS_WARNING ? 0 : MAAGE_CONTEXT_REGISTRY, operation, 0);
}
static unsigned long fluid_hash(const char *name) {
    unsigned long hash = 2166136261UL;
    while (*name)
        hash = (hash ^ (unsigned char) *name++) * 16777619UL;
    return hash;
}
static int fluid_name_valid(const char *name) {
    int colon = 0;
    const char *start = name;
    if (!name || !*name) return 0;
    while (*name) {
        if (*name == ':') {
            if (colon || name == start || !name[1]) return 0;
            colon = 1;
        } else if (!((*name >= 'a' && *name <= 'z') || (*name >= '0' && *name <= '9') || *name == '_' || *name == '-' || *name == '.' || (colon && *name == '/'))) return 0;
        ++name;
    }
    return colon;
}
static size_t fluid_bucket(const maage_fluid_registry *registry, const char *name) {
    size_t bucket = fluid_hash(name) & (registry->buckets - 1);
    while (registry->names[bucket] && strcmp(name, registry->names[bucket]->name))
        bucket = (bucket + 1) & (registry->buckets - 1);
    return bucket;
}
static int fluid_valid(const maage_fluid_definition *definition) {
    unsigned int channel;
    if (!definition || !definition->flow_levels || definition->flow_levels > UINT_MAX / 2U || definition->flow_levels > (size_t) - 1 / 2U || !definition->tick_delay || !definition->level_step || definition->level_step > definition->flow_levels || definition->falling_spread_level >= definition->flow_levels || definition->routing_depth > (unsigned int) INT_MAX / 2U - 1U || (definition->has_overlay_texture && definition->overlay_texture >= MAAGE_CONTENT_ID_INVALID) || (definition->state_count && (!definition->states || definition->state_count != (size_t) definition->flow_levels * 2U))) return 0;
    if (!(definition->acceleration >= 0 && definition->acceleration <= DBL_MAX && definition->drag >= 0 && definition->drag <= 1 && definition->gravity >= 0 && definition->gravity <= DBL_MAX && definition->swim_impulse >= 0 && definition->swim_impulse <= DBL_MAX && definition->current_impulse >= 0 && definition->current_impulse <= DBL_MAX && definition->wall_exit_impulse >= 0 && definition->wall_exit_impulse <= DBL_MAX && definition->fog_density >= 0 && definition->fog_density <= FLT_MAX && definition->fov_scale > 0 && definition->fov_scale <= FLT_MAX)) return 0;
    for (channel = 0; channel < 3; ++channel)
        if (!(definition->fog_color[channel] >= 0 && definition->fog_color[channel] <= 1)) return 0;
    return 1;
}
static void fluid_release(void *value) {
    maage_fluid_slot *slot = (maage_fluid_slot *) value;
    free(slot->states);
    free(slot->name);
    free(slot);
}
maage_status maage_fluid_registry_create(maage_fluid_registry **output) {
    maage_fluid_registry *registry;
    if (output) *output = NULL;
    if (!output) return fluid_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_CREATE);
    registry = (maage_fluid_registry *) calloc(1, sizeof(*registry));
    if (!registry) return fluid_status(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_CREATE);
    registry->buckets = 16;
    registry->names = (maage_fluid_slot **) calloc(registry->buckets, sizeof(*registry->names));
    if (!registry->names) {
        free(registry);
        return fluid_status(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_CREATE);
    }
    *output = registry;
    return fluid_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_CREATE);
}
maage_status maage_fluid_register(maage_fluid_registry *registry, const char *name, maage_fluid_definition *definition) {
    maage_fluid_slot *slot, **names;
    size_t buckets, index, bucket, length;
    void **entry;
    maage_fluid_id id;
    if (!registry || registry->finalized || !fluid_name_valid(name) || !fluid_valid(definition)) return fluid_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_REGISTER);
    bucket = fluid_bucket(registry, name);
    if (registry->names[bucket]) return fluid_status(MAAGE_STATUS_INVALID_STATE, MAAGE_OPERATION_REGISTER);
    if (registry->count == (size_t) - 1 || registry->count >= MAAGE_CONTENT_ID_INVALID - 1UL) return fluid_status(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_OPERATION_REGISTER);
    if (registry->count >= registry->buckets / 2) {
        if (registry->buckets > (size_t) - 1 / (2 * sizeof(*names))) return fluid_status(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_OPERATION_REGISTER);
        buckets = registry->buckets * 2;
        names = (maage_fluid_slot **) calloc(buckets, sizeof(*names));
        if (!names) return fluid_status(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_REGISTER);
        for (index = 0; index < registry->buckets; ++index) {
            if (!registry->names[index]) continue;
            bucket = fluid_hash(registry->names[index]->name) & (buckets - 1);
            while (names[bucket])
                bucket = (bucket + 1) & (buckets - 1);
            names[bucket] = registry->names[index];
        }
        free(registry->names);
        registry->names = names;
        registry->buckets = buckets;
    }
    length = strlen(name);
    if (length == (size_t) - 1) return fluid_status(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_OPERATION_REGISTER);
    slot = (maage_fluid_slot *) calloc(1, sizeof(*slot));
    if (!slot) return fluid_status(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_REGISTER);
    slot->name = (char *) malloc(length + 1);
    if (!slot->name) {
        free(slot);
        return fluid_status(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_REGISTER);
    }
    strcpy(slot->name, name);
    id = (maage_fluid_id) registry->count + 1UL;
    slot->definition = *definition;
    slot->definition.id = id;
    if (definition->state_count) {
        if (definition->state_count > (size_t) - 1 / sizeof(*slot->states)) {
            fluid_release(slot);
            return fluid_status(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_OPERATION_REGISTER);
        }
        slot->states = (unsigned long *) malloc(definition->state_count * sizeof(*slot->states));
        if (!slot->states) {
            fluid_release(slot);
            return fluid_status(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_REGISTER);
        }
        memcpy(slot->states, definition->states, definition->state_count * sizeof(*slot->states));
    }
    slot->definition.states = slot->states;
    entry = maage_registry_pages_ensure(&registry->definitions, id);
    if (!entry) {
        fluid_release(slot);
        return fluid_status(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_REGISTER);
    }
    *entry = slot;
    registry->names[fluid_bucket(registry, name)] = slot;
    ++registry->count;
    definition->id = id;
    return fluid_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_REGISTER);
}
maage_status maage_fluid_update(maage_fluid_registry *registry, const maage_fluid_definition *definition) {
    maage_fluid_slot *slot;
    unsigned long *states = NULL;
    if (!registry || registry->finalized || !fluid_valid(definition)) return fluid_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_SET);
    slot = fluid_slot(registry, definition->id);
    if (!slot) return fluid_status(MAAGE_STATUS_NOT_FOUND, MAAGE_OPERATION_SET);
    if (definition->state_count) {
        if (definition->state_count > (size_t) - 1 / sizeof(*states)) return fluid_status(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_OPERATION_SET);
        states = (unsigned long *) malloc(definition->state_count * sizeof(*states));
        if (!states) return fluid_status(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_SET);
        memcpy(states, definition->states, definition->state_count * sizeof(*states));
    }
    free(slot->states);
    slot->states = states;
    slot->definition = *definition;
    slot->definition.states = states;
    return fluid_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_SET);
}
maage_status maage_fluid_finalize(maage_fluid_registry *registry, const maage_block_registry *blocks) {
    size_t index, level;
    const maage_fluid_definition *fluid;
    const maage_block_state_definition *state;
    if (!registry || registry->finalized || !blocks) return fluid_status(MAAGE_STATUS_INVALID_STATE, MAAGE_OPERATION_FINALIZE);
    for (index = 0; index < registry->count; ++index) {
        fluid = maage_fluid_get(registry, (maage_fluid_id) index + 1);
        if (!fluid_valid(fluid) || fluid->state_count != (size_t) fluid->flow_levels * 2U) return fluid_status(MAAGE_STATUS_INVALID_STATE, MAAGE_OPERATION_FINALIZE);
        for (level = 0; level < fluid->state_count; ++level) {
            state = maage_block_registry_state(blocks, fluid->states[level]);
            if (!state || state->fluid_kind != fluid->id || state->fluid_level != level) return fluid_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_FINALIZE);
        }
    }
    registry->finalized = 1;
    return fluid_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_FINALIZE);
}
int maage_fluid_is_finalized(const maage_fluid_registry *registry) {
    return registry && registry->finalized;
}
size_t maage_fluid_count(const maage_fluid_registry *registry) {
    return registry ? registry->count : 0;
}
const maage_fluid_definition *maage_fluid_get(const maage_fluid_registry *registry, maage_fluid_id id) {
    const maage_fluid_slot *slot = fluid_slot(registry, id);
    return slot ? &slot->definition : NULL;
}
maage_status maage_fluid_resolve(const maage_fluid_registry *registry, const char *name, maage_fluid_id *id) {
    size_t bucket;
    if (id) *id = MAAGE_CONTENT_ID_INVALID;
    if (!registry || !name || !id) return fluid_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_RESOLVE);
    bucket = fluid_bucket(registry, name);
    if (!registry->names[bucket]) return fluid_status(MAAGE_STATUS_NOT_FOUND, MAAGE_OPERATION_RESOLVE);
    *id = registry->names[bucket]->definition.id;
    return fluid_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_RESOLVE);
}
const char *maage_fluid_name(const maage_fluid_registry *registry, maage_fluid_id id) {
    const maage_fluid_slot *slot = fluid_slot(registry, id);
    return slot ? slot->name : NULL;
}
maage_status maage_fluid_destroy(maage_fluid_registry *registry) {
    if (!registry) return fluid_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_DESTROY);
    maage_registry_pages_destroy(&registry->definitions, fluid_release);
    free(registry->names);
    free(registry);
    return fluid_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_DESTROY);
}
unsigned int maage_fluid_properties(const maage_fluid_definition *definition) {
    return definition ? (definition->swimming ? MAAGE_FLUID_PROPERTY_SWIMMING : 0U) | (definition->submerges ? MAAGE_FLUID_PROPERTY_SUBMERGES : 0U) | (definition->resets_fall ? MAAGE_FLUID_PROPERTY_RESETS_FALL : 0U) : 0;
}
