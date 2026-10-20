#include "maage/items.h"
#include <stdlib.h>
#include <string.h>

typedef struct maage_item_slot {
    maage_item_definition definition;
    char *name, *display;
} maage_item_slot;
struct maage_item_registry {
    maage_item_slot *slots;
    maage_item_id *ordered;
    size_t capacity, count;
    int finalized;
};

maage_item_registry *maage_item_registry_create(size_t capacity)
{
    maage_item_registry *registry;
    if (!capacity || capacity > (size_t)-1 / sizeof(maage_item_slot) ||
        capacity > (size_t)-1 / sizeof(maage_item_id)) return NULL;
    registry = (maage_item_registry *)calloc(1, sizeof(*registry));
    if (!registry) return NULL;
    registry->slots = (maage_item_slot *)calloc(capacity, sizeof(*registry->slots));
    registry->ordered = (maage_item_id *)calloc(capacity, sizeof(*registry->ordered));
    registry->capacity = capacity;
    if (!registry->slots || !registry->ordered) {
        maage_item_registry_destroy(registry); return NULL;
    }
    return registry;
}
void maage_item_registry_destroy(maage_item_registry *registry)
{
    size_t i;
    if (!registry) return;
    if (registry->slots) for (i = 0; i < registry->capacity; ++i) {
        free(registry->slots[i].name); free(registry->slots[i].display);
    }
    free(registry->slots); free(registry->ordered); free(registry);
}
maage_registry_result maage_item_registry_register(maage_item_registry *registry,
    const char *name, const maage_item_definition *definition)
{
    size_t i, length, display_length;
    maage_item_slot *slot;
    int colon = 0;
    if (!registry || !definition || !name || !definition->display_name)
        return MAAGE_REGISTRY_INVALID;
    if (registry->finalized) return MAAGE_REGISTRY_FROZEN;
    if (!definition->id || definition->id >= registry->capacity ||
        !definition->max_stack || definition->max_stack > 64 ||
        (definition->durability && definition->max_stack != 1) ||
        !(definition->mining_speed >= 1 && definition->mining_speed <= 1024))
        return MAAGE_REGISTRY_INVALID;
    length = strlen(name); display_length = strlen(definition->display_name);
    if (!length || length > 255 || !display_length || display_length > 127)
        return MAAGE_REGISTRY_INVALID;
    for (i = 0; i < length; ++i) {
        if (name[i] == ':') {
            if (colon || !i || i == length - 1) return MAAGE_REGISTRY_INVALID;
            colon = 1;
        } else if (!((name[i] >= 'a' && name[i] <= 'z') ||
            (name[i] >= '0' && name[i] <= '9') || name[i] == '_' ||
            name[i] == '-' || name[i] == '.' || (colon && name[i] == '/')))
            return MAAGE_REGISTRY_INVALID;
    }
    if (!colon) return MAAGE_REGISTRY_INVALID;
    slot = &registry->slots[definition->id];
    if (slot->name) return MAAGE_REGISTRY_DUPLICATE;
    for (i = 0; i < registry->count; ++i)
        if (!strcmp(registry->slots[registry->ordered[i]].name, name))
            return MAAGE_REGISTRY_DUPLICATE;
    slot->name = (char *)malloc(length + 1);
    slot->display = (char *)malloc(display_length + 1);
    if (!slot->name || !slot->display) {
        free(slot->name); free(slot->display); memset(slot, 0, sizeof(*slot));
        return MAAGE_REGISTRY_OUT_OF_MEMORY;
    }
    memcpy(slot->name, name, length + 1);
    memcpy(slot->display, definition->display_name, display_length + 1);
    slot->definition = *definition; slot->definition.display_name = slot->display;
    registry->ordered[registry->count++] = definition->id;
    return MAAGE_REGISTRY_OK;
}
maage_registry_result maage_item_registry_finalize(maage_item_registry *registry)
{
    if (!registry || !registry->count) return MAAGE_REGISTRY_INVALID;
    if (registry->finalized) return MAAGE_REGISTRY_FROZEN;
    registry->finalized = 1; return MAAGE_REGISTRY_OK;
}
int maage_item_registry_is_finalized(const maage_item_registry *registry)
{
    return registry && registry->finalized;
}
const maage_item_definition *maage_item_registry_get(const maage_item_registry *registry,
    maage_item_id id)
{
    if (!registry || id >= registry->capacity || !registry->slots[id].name) return NULL;
    return &registry->slots[id].definition;
}
size_t maage_item_registry_count(const maage_item_registry *registry)
{
    return registry ? registry->count : 0;
}
const maage_item_definition *maage_item_registry_at(const maage_item_registry *registry,
    size_t index)
{
    return registry && index < registry->count ?
        maage_item_registry_get(registry, registry->ordered[index]) : NULL;
}
maage_item_registry *maage_item_registry_create_builtin(const maage_block_registry *blocks)
{
    static const maage_block_id ids[] = {1, 2, 3, 7, 12, 13, 24};
    static const char *const names[] = {"minecraft:stone", "minecraft:grass", "minecraft:dirt",
        "minecraft:bedrock", "minecraft:sand", "minecraft:gravel", "minecraft:sandstone"};
    static const char *const displays[] = {"Stone", "Grass Block", "Dirt", "Bedrock", "Sand", "Gravel", "Sandstone"};
    maage_item_registry *registry;
    maage_item_definition item;
    const maage_block_state_definition *state;
    size_t i;
    if (!maage_block_registry_is_finalized(blocks)) return NULL;
    registry = maage_item_registry_create(32);
    if (!registry) return NULL;
    for (i = 0; i < sizeof(ids) / sizeof(ids[0]); ++i) {
        state = maage_block_registry_state(blocks, ids[i]);
        if (!state || state->block_id != ids[i]) goto failed;
        memset(&item, 0, sizeof(item));
        item.id = ids[i]; item.has_block = 1; item.block_id = ids[i];
        item.placement_state_id = item.icon_state_id = ids[i]; item.max_stack = 64;
        item.mining_speed = 1; item.texture_id = state->faces[MAAGE_FACE_POSITIVE_Z].texture_id;
        item.display_name = displays[i];
        if (maage_item_registry_register(registry, names[i], &item) != MAAGE_REGISTRY_OK) goto failed;
    }
    if (maage_item_registry_finalize(registry) == MAAGE_REGISTRY_OK) return registry;
failed:
    maage_item_registry_destroy(registry); return NULL;
}
int maage_item_stack_valid(const maage_item_registry *registry, const maage_item_stack *stack)
{
    const maage_item_definition *item;
    if (!stack) return 0;
    if (!stack->count) return !stack->item_id && !stack->metadata && !stack->damage;
    item = maage_item_registry_get(registry, stack->item_id);
    return item && stack->count <= item->max_stack &&
        stack->damage <= item->durability && (!item->durability || stack->damage < item->durability);
}
const maage_item_stack *maage_inventory_selected(const maage_inventory *inventory)
{
    return inventory && inventory->selected < MAAGE_HOTBAR_SLOTS ?
        &inventory->slots[inventory->selected] : NULL;
}
static int maage_stack_matches(const maage_item_stack *a, const maage_item_stack *b)
{
    return a->item_id == b->item_id && a->metadata == b->metadata && a->damage == b->damage;
}
unsigned int maage_inventory_add(maage_inventory *inventory,
    const maage_item_registry *registry, const maage_item_stack *stack)
{
    const maage_item_definition *item;
    maage_item_stack *destination;
    unsigned int i, pass, remaining, amount;
    if (!inventory || !maage_item_stack_valid(registry, stack)) return stack ? stack->count : 0;
    if (!stack->count) return 0;
    item = maage_item_registry_get(registry, stack->item_id); remaining = stack->count;
    for (pass = 0; pass < 2; ++pass) for (i = 0; i < MAAGE_INVENTORY_SLOTS && remaining; ++i) {
        destination = &inventory->slots[i];
        if (pass == 0 ? (!destination->count || !maage_stack_matches(destination, stack)) : destination->count) continue;
        amount = item->max_stack - destination->count;
        if (amount > remaining) amount = remaining;
        if (!destination->count) { *destination = *stack; destination->count = 0; }
        destination->count += amount; remaining -= amount;
    }
    return remaining;
}
int maage_inventory_click(maage_inventory *inventory,
    const maage_item_registry *registry, unsigned int slot, int secondary)
{
    maage_item_stack *source, *cursor, temporary;
    const maage_item_definition *item;
    unsigned int amount;
    if (!inventory || slot >= MAAGE_INVENTORY_SLOTS) return 0;
    source = &inventory->slots[slot]; cursor = &inventory->cursor;
    if (!maage_item_stack_valid(registry, source) || !maage_item_stack_valid(registry, cursor)) return 0;
    if (!cursor->count) {
        if (!source->count) return 1;
        *cursor = *source; cursor->count = secondary ? (source->count + 1) / 2 : source->count;
        source->count -= cursor->count;
        if (!source->count) memset(source, 0, sizeof(*source));
        return 1;
    }
    item = maage_item_registry_get(registry, cursor->item_id);
    if (!source->count || maage_stack_matches(source, cursor)) {
        amount = item->max_stack - source->count;
        if (amount > cursor->count) amount = cursor->count;
        if (secondary && amount > 1) amount = 1;
        if (!source->count) { *source = *cursor; source->count = 0; }
        source->count += amount; cursor->count -= amount;
        if (!cursor->count) memset(cursor, 0, sizeof(*cursor));
    } else {
        temporary = *source; *source = *cursor; *cursor = temporary;
    }
    return 1;
}
int maage_inventory_creative(maage_inventory *inventory,
    const maage_item_registry *registry, maage_item_id id)
{
    const maage_item_definition *item = maage_item_registry_get(registry, id);
    if (!inventory || !item) return 0;
    memset(&inventory->cursor, 0, sizeof(inventory->cursor));
    inventory->cursor.item_id = id; inventory->cursor.count = item->max_stack;
    return 1;
}
int maage_item_placement_state(const maage_item_definition *item,
    unsigned int metadata, maage_block_face face, double yaw,
    maage_block_state_id *state)
{
    if (!item || !item->has_block || !state || face >= MAAGE_FACE_COUNT) return 0;
    if (item->placement) return item->placement(item, metadata, face, yaw, state);
    if (metadata) return 0;
    *state = item->placement_state_id; return 1;
}
