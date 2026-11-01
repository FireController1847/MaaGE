#include "maage/items.h"
#include "maage/content.h"
#include "world_internal.h"
#include <stdio.h>
#include "maage/internal/registry_pages.h"
#include <stdlib.h>
#include <string.h>
#include <float.h>

typedef struct maage_item_slot {
    maage_item_definition definition;
    char *name, *display;
} maage_item_slot;
struct maage_item_registry {
    maage_registry_pages block_items;
    maage_registry_pages slots;
    maage_item_id *ordered;
    size_t *name_buckets, bucket_count;
    size_t capacity, count;
    int finalized;
};
static maage_item_slot *maage_item_slot_get(const maage_item_registry *registry, maage_item_id id) {
    void **entry = registry ? maage_registry_pages_find(&registry->slots, id) : NULL;
    return entry ? (maage_item_slot *) *entry : NULL;
}
static unsigned long maage_item_name_hash(const char *name) {
    unsigned long hash = 2166136261UL;
    while (*name) {
        hash ^= (unsigned char) *name++;
        hash *= 16777619UL;
    }
    return hash;
}
static size_t maage_item_name_bucket(const maage_item_registry *registry, const char *name, int *found) {
    size_t bucket = maage_item_name_hash(name) % registry->bucket_count, entry;
    while ((entry = registry->name_buckets[bucket]) != 0) {
        if (!strcmp(maage_item_slot_get(registry, registry->ordered[entry - 1])->name, name)) {
            *found = 1;
            return bucket;
        }
        bucket = (bucket + 1) % registry->bucket_count;
    }
    *found = 0;
    return bucket;
}
static int maage_item_grow_names(maage_item_registry *registry) {
    size_t *buckets, i, entry, index, count;
    if (registry->count + 1 < registry->bucket_count / 2) return 1;
    if (registry->bucket_count > ((size_t) - 1 - 1) / 2) return 0;
    count = registry->bucket_count * 2 + 1;
    if (count > (size_t) - 1 / sizeof(*buckets)) return 0;
    buckets = (size_t *) calloc(count, sizeof(*buckets));
    if (!buckets) return 0;
    for (i = 0; i < registry->bucket_count; ++i) {
        entry = registry->name_buckets[i];
        if (!entry) continue;
        index = maage_item_name_hash(maage_item_slot_get(registry, registry->ordered[entry - 1])->name) % count;
        while (buckets[index])
            index = (index + 1) % count;
        buckets[index] = entry;
    }
    free(registry->name_buckets);
    registry->name_buckets = buckets;
    registry->bucket_count = count;
    return 1;
}
static int maage_item_registry_grow(maage_item_registry *registry, maage_item_id id) {
    maage_item_id *ordered;
    size_t count;
    (void) id;
    if (registry->count < registry->capacity) return 1;
    if (registry->capacity > ((size_t) - 1 / sizeof(*ordered)) / 2) count = registry->count + 1;
    else
        count = registry->capacity * 2;
    if (count <= registry->count || count > (size_t) - 1 / sizeof(*ordered)) return 0;
    ordered = (maage_item_id *) realloc(registry->ordered, count * sizeof(*ordered));
    if (!ordered) return 0;
    registry->ordered = ordered;
    registry->capacity = count;
    return 1;
}
maage_status maage_item_registry_create(size_t capacity, maage_item_registry **output) {
    maage_item_registry *registry;
    if (!output) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_CREATE, 1);
    *output = NULL;
    if (!capacity) capacity = 16;
    if (capacity > (size_t) - 1 / sizeof(maage_item_slot) || capacity > (size_t) - 1 / sizeof(maage_item_id)) return maage_status_make(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_CREATE, 0);
    registry = (maage_item_registry *) calloc(1, sizeof(*registry));
    if (!registry) return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_CREATE, 0);
    registry->ordered = (maage_item_id *) calloc(capacity, sizeof(*registry->ordered));
    registry->capacity = capacity;
    registry->bucket_count = 33;
    registry->name_buckets = (size_t *) calloc(registry->bucket_count, sizeof(size_t));
    if (!registry->ordered || !registry->name_buckets) {
        maage_item_registry_destroy(registry);
        return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_CREATE, 0);
    }
    *output = registry;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_CREATE, 0);
}
maage_status maage_item_registry_destroy(maage_item_registry *registry) {
    size_t i;
    maage_item_slot *slot;
    if (!registry) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_DESTROY, 0);
    for (i = 0; i < registry->count; ++i) {
        slot = maage_item_slot_get(registry, registry->ordered[i]);
        if (slot) {
            free(slot->name);
            free(slot->display);
            free(slot);
        }
    }
    maage_registry_pages_destroy(&registry->slots, NULL);
    maage_registry_pages_destroy(&registry->block_items, NULL);
    free(registry->name_buckets);
    free(registry->ordered);
    free(registry);
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_DESTROY, 0);
}
maage_status maage_item_registry_register(maage_item_registry *registry, const char *name, maage_item_definition *definition) {
    size_t i, length, display_length, bucket;
    maage_item_slot *slot;
    int colon = 0, found;
    if (!registry || !definition || !name || !definition->display_name) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0);
    if (registry->finalized) return maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0);
    if (registry->count >= 4294967295UL) return maage_status_make(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0);
    definition->id = (unsigned long) registry->count + 1;
    if (!definition->max_stack || definition->max_stack > 64 ||
        (definition->durability && definition->max_stack != 1) ||
        !(definition->mining_speed >= 1 && definition->mining_speed <= 1024))
        return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0);
    if ((definition->food_ticks && (!definition->food_hunger || definition->food_hunger > 20 || !(definition->food_saturation >= 0 && definition->food_saturation <= DBL_MAX / 40))) ||
        (definition->food_always_edible != 0 && definition->food_always_edible != 1) ||
        (!definition->food_ticks && (definition->food_hunger || definition->food_saturation || definition->food_always_edible)) ||
        (definition->container_item && definition->max_stack != 1)) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0);
    length = strlen(name);
    display_length = strlen(definition->display_name);
    if (!length || length > 255 || !display_length || display_length > 127) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0);
    for (i = 0; i < length; ++i) {
        if (name[i] == ':') {
            if (colon || !i || i == length - 1) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0);
            colon = 1;
        } else if (!((name[i] >= 'a' && name[i] <= 'z') ||
                     (name[i] >= '0' && name[i] <= '9') || name[i] == '_' ||
                     name[i] == '-' || name[i] == '.' || (colon && name[i] == '/')))
            return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0);
    }
    if (!colon) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0);
    bucket = maage_item_name_bucket(registry, name, &found);
    if (found) return maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0);
    if (!maage_item_registry_grow(registry, definition->id) || !maage_item_grow_names(registry)) return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0);
    if (maage_item_slot_get(registry, definition->id)) return maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0);
    slot = (maage_item_slot *) calloc(1, sizeof(*slot));
    if (!slot) return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0);
    bucket = maage_item_name_bucket(registry, name, &found);
    slot->name = (char *) malloc(length + 1);
    slot->display = (char *) malloc(display_length + 1);
    if (!slot->name || !slot->display) {
        free(slot->name);
        free(slot->display);
        free(slot);
        return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0);
    }
    memcpy(slot->name, name, length + 1);
    memcpy(slot->display, definition->display_name, display_length + 1);
    slot->definition = *definition;
    slot->definition.display_name = slot->display;
    {
        void **entry = maage_registry_pages_ensure(&registry->slots, definition->id);
        if (!entry) {
            free(slot->name);
            free(slot->display);
            free(slot);
            return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0);
        }
        *entry = slot;
    }
    if (definition->has_block) {
        void **block_entry = maage_registry_pages_ensure(&registry->block_items, definition->block_id);
        if (!block_entry) {
            void **entry = maage_registry_pages_find(&registry->slots, definition->id);
            *entry = NULL;
            free(slot->name);
            free(slot->display);
            free(slot);
            return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0);
        }
        if (!*block_entry) *block_entry = &slot->definition;
    }
    registry->name_buckets[bucket] = registry->count + 1;
    registry->ordered[registry->count++] = definition->id;
    return maage_status_make(MAAGE_STATUS_SUCCESS, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0);
}
maage_status maage_item_registry_finalize(maage_item_registry *registry) {
    size_t i;
    const maage_item_definition *item;
    if (!registry || !registry->count) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_FINALIZE, 0);
    if (registry->finalized) return maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_FINALIZE, 0);
    for (i = 0; i < registry->count; ++i) {
        item = maage_item_registry_get(registry, registry->ordered[i]);
        if (item->container_item && !maage_item_registry_get(registry, item->container_item)) return maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_FINALIZE, 0);
    }
    registry->finalized = 1;
    return maage_status_make(MAAGE_STATUS_SUCCESS, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_FINALIZE, 0);
}
int maage_item_registry_is_finalized(const maage_item_registry *registry) {
    return registry && registry->finalized;
}
const maage_item_definition *maage_item_registry_get(const maage_item_registry *registry, maage_item_id id) {
    maage_item_slot *slot = maage_item_slot_get(registry, id);
    return slot ? &slot->definition : NULL;
}
size_t maage_item_registry_count(const maage_item_registry *registry) {
    return registry ? registry->count : 0;
}
const maage_item_definition *maage_item_registry_at(const maage_item_registry *registry, size_t index) {
    return registry && index < registry->count ? maage_item_registry_get(registry, registry->ordered[index]) : NULL;
}

int maage_item_stack_valid(const maage_item_registry *registry, const maage_item_stack *stack) {
    const maage_item_definition *item;
    if (!stack) return 0;
    if (!stack->count) return !stack->item_id && !stack->metadata && !stack->damage;
    item = maage_item_registry_get(registry, stack->item_id);
    return item && stack->count <= item->max_stack && stack->metadata <= item->max_metadata && stack->damage <= item->durability;
}
const maage_item_stack *maage_inventory_selected(const maage_inventory *inventory) {
    return inventory && inventory->selected < MAAGE_HOTBAR_SLOTS ? &inventory->slots[inventory->selected] : NULL;
}
static int maage_stack_matches(const maage_item_stack *a, const maage_item_stack *b) {
    return a->item_id == b->item_id && a->metadata == b->metadata && a->damage == b->damage;
}
maage_status maage_inventory_add(maage_inventory *inventory, const maage_item_registry *registry, const maage_item_stack *stack, unsigned int *uninserted) {
    const maage_item_definition *item;
    maage_item_stack *destination;
    unsigned int i, pass, remaining, amount;
    if (uninserted) *uninserted = stack ? stack->count : 0;
    if (!inventory || !maage_item_stack_valid(registry, stack) || !uninserted)
        return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_UPDATE, !inventory ? 0 : (!uninserted ? 3 : 2));
    if (!stack->count) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_UPDATE, 0);
    item = maage_item_registry_get(registry, stack->item_id);
    remaining = stack->count;
    for (pass = 0; pass < 2; ++pass)
        for (i = 0; i < MAAGE_INVENTORY_SLOTS && remaining; ++i) {
            destination = &inventory->slots[i];
            if (pass == 0 ? (!destination->count || !maage_stack_matches(destination, stack)) : destination->count) continue;
            amount = item->max_stack - destination->count;
            if (amount > remaining) amount = remaining;
            if (!destination->count) {
                *destination = *stack;
                destination->count = 0;
            }
            destination->count += amount;
            remaining -= amount;
        }
    *uninserted = remaining;
    return maage_status_make(remaining ? MAAGE_STATUS_PARTIAL : MAAGE_STATUS_SUCCESS, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_UPDATE, 0);
}
maage_status maage_item_stack_click(maage_item_stack *source, maage_item_stack *cursor, const maage_item_registry *registry, int secondary) {
    maage_item_stack temporary;
    const maage_item_definition *item;
    unsigned int amount;
    if (!source || !cursor || source == cursor || (secondary != 0 && secondary != 1)) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_UPDATE, 0);
    if (!maage_item_stack_valid(registry, source) || !maage_item_stack_valid(registry, cursor)) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_UPDATE, 0);
    if (!cursor->count) {
        if (!source->count) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_UPDATE, 0);
        *cursor = *source;
        cursor->count = secondary ? (source->count + 1) / 2 : source->count;
        source->count -= cursor->count;
        if (!source->count) memset(source, 0, sizeof(*source));
        return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_UPDATE, 0);
    }
    item = maage_item_registry_get(registry, cursor->item_id);
    if (!source->count || maage_stack_matches(source, cursor)) {
        amount = item->max_stack - source->count;
        if (amount > cursor->count) amount = cursor->count;
        if (secondary && amount > 1) amount = 1;
        if (!source->count) {
            *source = *cursor;
            source->count = 0;
        }
        source->count += amount;
        cursor->count -= amount;
        if (!cursor->count) memset(cursor, 0, sizeof(*cursor));
    } else {
        temporary = *source;
        *source = *cursor;
        *cursor = temporary;
    }
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_UPDATE, 0);
}
maage_status maage_inventory_click(maage_inventory *inventory, const maage_item_registry *registry, unsigned int slot, int secondary) {
    if (!inventory || slot >= MAAGE_INVENTORY_SLOTS) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_UPDATE, 0);
    return maage_item_stack_click(&inventory->slots[slot], &inventory->cursor, registry, secondary);
}
maage_status maage_inventory_creative(maage_inventory *inventory, const maage_item_registry *registry, maage_item_id id, unsigned int metadata) {
    const maage_item_definition *item = maage_item_registry_get(registry, id);
    if (!inventory || !item || metadata > item->max_metadata) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_UPDATE, 0);
    memset(&inventory->cursor, 0, sizeof(inventory->cursor));
    inventory->cursor.item_id = id;
    inventory->cursor.count = item->max_stack;
    inventory->cursor.metadata = metadata;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_UPDATE, 0);
}
maage_status maage_item_placement_state(const maage_item_definition *item, unsigned int metadata, maage_block_face face, double yaw, maage_block_state_id *state) {
    if (!item || !item->has_block || !state || face >= MAAGE_FACE_COUNT || !(yaw >= -DBL_MAX && yaw <= DBL_MAX)) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_RESOLVE, 0);
    if (item->placement) return item->placement(item, metadata, face, yaw, state);
    if (metadata) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_RESOLVE, 0);
    *state = item->placement_state_id;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_RESOLVE, 0);
}

const char *maage_item_registry_name(const maage_item_registry *registry, maage_item_id id) {
    maage_item_slot *slot = maage_item_slot_get(registry, id);
    return slot ? slot->name : NULL;
}
maage_status maage_item_registry_resolve_name(const maage_item_registry *registry, const char *name, maage_item_id *id) {
    size_t bucket;
    int found;
    if (!registry || !name || !id) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_RESOLVE, 0);
    bucket = maage_item_name_bucket(registry, name, &found);
    if (!found) return maage_status_make(MAAGE_STATUS_NOT_FOUND, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_RESOLVE, 0);
    *id = registry->ordered[registry->name_buckets[bucket] - 1];
    return maage_status_make(MAAGE_STATUS_SUCCESS, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_RESOLVE, 0);
}

int maage_item_can_harvest(const maage_item_definition *tool, const maage_block_definition *block) {
    return block && (!block->harvest_requires_tool || (tool && tool->tool_class == block->required_tool && tool->tool_level >= block->required_tool_level));
}
double maage_item_mining_speed(const maage_item_definition *tool, const maage_block_definition *block) {
    if (!tool || !block) return 1;
    if (block->special_tool_class && tool->tool_class == block->special_tool_class) return block->special_tool_speed;
    return tool->tool_class == block->required_tool && tool->tool_class != MAAGE_TOOL_NONE ? tool->mining_speed : 1;
}
double maage_item_mining_damage(const maage_item_definition *tool, const maage_block_definition *block, int grounded, int submerged) {
    double speed;
    if (!block || block->hardness < 0) return 0;
    if (block->hardness == 0) return 1;
    speed = maage_item_mining_speed(tool, block);
    if (!grounded) speed /= 5;
    if (submerged) speed /= 5;
    return speed / (block->hardness * (maage_item_can_harvest(tool, block) ? 30 : 100));
}

maage_item_id maage_item_registry_block_item(const maage_item_registry *registry, maage_block_id block) {
    void **entry = registry ? maage_registry_pages_find(&registry->block_items, block) : NULL;
    return entry && *entry ? ((const maage_item_definition *) *entry)->id : 0;
}

maage_status maage_item_generate_drops(const maage_block_registry *blocks, const maage_item_registry *items, maage_block_state_id id, const maage_item_definition *tool, unsigned long *random, maage_item_drops *drops) {
    const maage_block_state_definition *state = maage_block_registry_state(blocks, id);
    const maage_block_definition *block = state ? maage_block_registry_block(blocks, state->block_id) : NULL;
    maage_status result;
    size_t i;
    if (!block || !random || !drops) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_RESOLVE, 0);
    memset(drops, 0, sizeof(*drops));
    if (!maage_item_can_harvest(tool, block)) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_RESOLVE, 0);
    if (block->drops) {
        result = block->drops(blocks, items, state, tool, random, drops);
        if (maage_status_failed(result)) return result;
    } else {
        drops->stacks[0].item_id = block->drop_item_id;
        drops->stacks[0].count = block->drop_count;
    }
    drops->count = 0;
    for (i = 0; i < MAAGE_DROP_STACKS; ++i) {
        if (!drops->stacks[i].count) continue;
        if (!maage_item_stack_valid(items, &drops->stacks[i])) return maage_status_make(MAAGE_STATUS_INVARIANT_VIOLATION, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_RESOLVE, 0);
        drops->stacks[drops->count++] = drops->stacks[i];
    }
    return maage_status_make(drops->count ? MAAGE_STATUS_SUCCESS : MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_RESOLVE, 0);
}
