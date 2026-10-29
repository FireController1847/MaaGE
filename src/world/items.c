#include "maage/items.h"
#include "maage/content.h"
#include <stdio.h>
#include "maage/internal/registry_pages.h"
#include <stdlib.h>
#include <string.h>

typedef struct maage_item_slot {
    maage_item_definition definition;
    char *name, *display;
} maage_item_slot;
struct maage_item_registry {
    maage_registry_pages slots;
    maage_item_id *ordered;
    size_t *name_buckets, bucket_count;
    size_t capacity, count;
    int finalized;
};
static maage_item_slot *maage_item_slot_get(const maage_item_registry *registry,
    maage_item_id id)
{
    void **entry = registry ? maage_registry_pages_find(&registry->slots, id) : NULL;
    return entry ? (maage_item_slot *)*entry : NULL;
}
static unsigned long maage_item_name_hash(const char *name)
{
    unsigned long hash = 2166136261UL;
    while (*name) { hash ^= (unsigned char)*name++; hash *= 16777619UL; }
    return hash;
}
static size_t maage_item_name_bucket(const maage_item_registry *registry,
    const char *name, int *found)
{
    size_t bucket = maage_item_name_hash(name) % registry->bucket_count, entry;
    while ((entry = registry->name_buckets[bucket]) != 0) {
        if (!strcmp(maage_item_slot_get(registry, registry->ordered[entry - 1])->name, name)) {
            *found = 1; return bucket;
        }
        bucket = (bucket + 1) % registry->bucket_count;
    }
    *found = 0;
    return bucket;
}
static int maage_item_grow_names(maage_item_registry *registry)
{
    size_t *buckets, i, entry, index, count;
    if (registry->count + 1 < registry->bucket_count / 2) return 1;
    if (registry->bucket_count > ((size_t)-1 - 1) / 2) return 0;
    count = registry->bucket_count * 2 + 1;
    if (count > (size_t)-1 / sizeof(*buckets)) return 0;
    buckets = (size_t *)calloc(count, sizeof(*buckets));
    if (!buckets) return 0;
    for (i = 0; i < registry->bucket_count; ++i) {
        entry = registry->name_buckets[i];
        if (!entry) continue;
        index = maage_item_name_hash(maage_item_slot_get(registry, registry->ordered[entry - 1])->name) % count;
        while (buckets[index]) index = (index + 1) % count;
        buckets[index] = entry;
    }
    free(registry->name_buckets);
    registry->name_buckets = buckets;
    registry->bucket_count = count;
    return 1;
}
static int maage_item_registry_grow(maage_item_registry *registry, maage_item_id id)
{
    maage_item_id *ordered;
    size_t count;
    (void)id;
    if (registry->count < registry->capacity) return 1;
    if (registry->capacity > ((size_t)-1 / sizeof(*ordered)) / 2)
        count = registry->count + 1;
    else count = registry->capacity * 2;
    if (count <= registry->count || count > (size_t)-1 / sizeof(*ordered)) return 0;
    ordered = (maage_item_id *)realloc(registry->ordered, count * sizeof(*ordered));
    if (!ordered) return 0;
    registry->ordered = ordered;
    registry->capacity = count;
    return 1;
}
maage_status maage_item_registry_create(size_t capacity, maage_item_registry **output)
{
    maage_item_registry *registry;
    if (!output) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_CREATE, 1);
    *output = NULL;
    if (!capacity) capacity = 16;
    if (capacity > (size_t)-1 / sizeof(maage_item_slot) ||
        capacity > (size_t)-1 / sizeof(maage_item_id))
        return maage_status_make(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_CREATE, 0);
    registry = (maage_item_registry *)calloc(1, sizeof(*registry));
    if (!registry) return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_CREATE, 0);
    registry->ordered = (maage_item_id *)calloc(capacity, sizeof(*registry->ordered));
    registry->capacity = capacity;
    registry->bucket_count = 33;
    registry->name_buckets = (size_t *)calloc(registry->bucket_count, sizeof(size_t));
    if (!registry->ordered || !registry->name_buckets) {
        maage_item_registry_destroy(registry);
        return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_CREATE, 0);
    }
    *output = registry;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_CREATE, 0);
}
maage_status maage_item_registry_destroy(maage_item_registry *registry)
{
    size_t i;
    maage_item_slot *slot;
    if (!registry) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_DESTROY, 0);
    for (i = 0; i < registry->count; ++i) {
        slot = maage_item_slot_get(registry, registry->ordered[i]);
        if (slot) { free(slot->name); free(slot->display); free(slot); }
    }
    maage_registry_pages_destroy(&registry->slots, NULL);
    free(registry->name_buckets);
    free(registry->ordered);
    free(registry);
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_DESTROY, 0);
}
maage_status maage_item_registry_register(maage_item_registry *registry,
    const char *name, const maage_item_definition *definition)
{
    size_t i, length, display_length, bucket;
    maage_item_slot *slot;
    int colon = 0, found;
    if (!registry || !definition || !name || !definition->display_name)
        return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0);
    if (registry->finalized) return maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0);
    if (!definition->id || definition->id > 4294967295UL ||
        !definition->max_stack || definition->max_stack > 64 ||
        (definition->durability && definition->max_stack != 1) ||
        !(definition->mining_speed >= 1 && definition->mining_speed <= 1024))
        return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0);
    length = strlen(name); display_length = strlen(definition->display_name);
    if (!length || length > 255 || !display_length || display_length > 127)
        return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0);
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
    if (!maage_item_registry_grow(registry, definition->id) ||
        !maage_item_grow_names(registry))
        return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0);
    if (maage_item_slot_get(registry, definition->id))
        return maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0);
    slot = (maage_item_slot *)calloc(1, sizeof(*slot));
    if (!slot) return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0);
    bucket = maage_item_name_bucket(registry, name, &found);
    slot->name = (char *)malloc(length + 1);
    slot->display = (char *)malloc(display_length + 1);
    if (!slot->name || !slot->display) {
        free(slot->name); free(slot->display); free(slot);
        return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0);
    }
    memcpy(slot->name, name, length + 1);
    memcpy(slot->display, definition->display_name, display_length + 1);
    slot->definition = *definition; slot->definition.display_name = slot->display;
    {
        void **entry = maage_registry_pages_ensure(&registry->slots, definition->id);
        if (!entry) {
            free(slot->name); free(slot->display); free(slot);
            return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0);
        }
        *entry = slot;
    }
    registry->name_buckets[bucket] = registry->count + 1;
    registry->ordered[registry->count++] = definition->id;
    return maage_status_make(MAAGE_STATUS_SUCCESS, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0);
}
maage_status maage_item_registry_finalize(maage_item_registry *registry)
{
    if (!registry || !registry->count) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_FINALIZE, 0);
    if (registry->finalized) return maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_FINALIZE, 0);
    registry->finalized = 1; return maage_status_make(MAAGE_STATUS_SUCCESS, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_FINALIZE, 0);
}
int maage_item_registry_is_finalized(const maage_item_registry *registry)
{
    return registry && registry->finalized;
}
const maage_item_definition *maage_item_registry_get(const maage_item_registry *registry,
    maage_item_id id)
{
    maage_item_slot *slot = maage_item_slot_get(registry, id);
    return slot ? &slot->definition : NULL;
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
static maage_status maage_item_oriented_placement(const maage_item_definition *item,
    unsigned int metadata, maage_block_face face, double yaw,
    maage_block_state_id *state)
{
    static const maage_block_state_id torches[MAAGE_FACE_COUNT] = {
        MAAGE_STATE_TORCH_POSITIVE_X, MAAGE_STATE_TORCH_NEGATIVE_X,
        MAAGE_STATE_AIR, MAAGE_STATE_TORCH,
        MAAGE_STATE_TORCH_POSITIVE_Z, MAAGE_STATE_TORCH_NEGATIVE_Z
    };
    unsigned int species;
    (void)yaw;
    if ((unsigned int)face >= MAAGE_FACE_COUNT)
        return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_RESOLVE, 0);
    if (item->block_id == MAAGE_BLOCK_TORCH) {
        *state = torches[face];
        if (face == MAAGE_FACE_NEGATIVE_Y)
            return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_RESOLVE, 0);
    } else {
        species = metadata & 3U;
        if (species > 2) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT,
            MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_RESOLVE, 0);
        if (item->block_id == MAAGE_BLOCK_PLANKS)
            *state = species ? MAAGE_STATE_SPRUCE_PLANKS + species - 1 : MAAGE_STATE_PLANKS;
        else if (item->block_id == MAAGE_BLOCK_LEAVES)
            return maage_block_leaves_state(species | MAAGE_LEAF_PERSISTENT, state);
        else if (face / 2 == 0) *state = MAAGE_STATE_LOG_X_BASE + species;
        else if (face / 2 == 2) *state = MAAGE_STATE_LOG_Z_BASE + species;
        else *state = species ? MAAGE_STATE_SPRUCE_LOG + species - 1 : MAAGE_STATE_LOG;
    }
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_RESOLVE, 0);
}

maage_status maage_item_registry_create_builtin(const maage_block_registry *blocks,
    maage_item_registry **output)
{
    static const maage_block_id ids[] = {1, 2, 3, 7, 12, 13, 24, 4, 5, 14, 15, 16, 17, 18, 21, 31, 32, 37, 38, 39, 40, 48, 49, 50, 56, 73, 78, 79, 81, 129, 58, 41, 42, 57, 22, 133, 152, 173, 80};
    static const char *const names[] = {"minecraft:stone", "minecraft:grass", "minecraft:dirt", "minecraft:bedrock", "minecraft:sand", "minecraft:gravel", "minecraft:sandstone", "minecraft:cobblestone", "minecraft:planks", "minecraft:gold_ore", "minecraft:iron_ore", "minecraft:coal_ore", "minecraft:log", "minecraft:leaves", "minecraft:lapis_ore", "minecraft:tall_grass", "minecraft:dead_bush", "minecraft:dandelion", "minecraft:poppy", "minecraft:brown_mushroom", "minecraft:red_mushroom", "minecraft:mossy_cobblestone", "minecraft:obsidian", "minecraft:torch", "minecraft:diamond_ore", "minecraft:redstone_ore", "minecraft:snow", "minecraft:ice", "minecraft:cactus", "minecraft:emerald_ore", "minecraft:crafting_table", "minecraft:gold_block", "minecraft:iron_block", "minecraft:diamond_block", "minecraft:lapis_block", "minecraft:emerald_block", "minecraft:redstone_block", "minecraft:coal_block", "minecraft:snow_block"};
    static const char *const displays[] = {"Stone", "Grass Block", "Dirt", "Bedrock", "Sand", "Gravel", "Sandstone", "Cobblestone", "Oak Planks", "Gold Ore", "Iron Ore", "Coal Ore", "Oak Log", "Oak Leaves", "Lapis Ore", "Tall Grass", "Dead Bush", "Dandelion", "Poppy", "Brown Mushroom", "Red Mushroom", "Mossy Cobblestone", "Obsidian", "Torch", "Diamond Ore", "Redstone Ore", "Snow Layer", "Ice", "Cactus", "Emerald Ore", "Crafting Table", "Block of Gold", "Block of Iron", "Block of Diamond", "Lapis Lazuli Block", "Block of Emerald", "Block of Redstone", "Block of Coal", "Snow"};
    maage_item_registry *registry;
    maage_item_definition item;
    const maage_block_state_definition *state;
    size_t i;
    maage_status result;
    if (!output) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_INITIALIZE, 1);
    *output = NULL;
    if (!maage_block_registry_is_finalized(blocks))
        return maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_INITIALIZE, 0);
    result = maage_item_registry_create(0, &registry);
    if (maage_status_failed(result)) return result;
    for (i = 0; i < sizeof(ids) / sizeof(ids[0]); ++i) {
        state = maage_block_registry_state(blocks, ids[i]);
        if (!state) continue;
        if (state->block_id != ids[i]) {
            result = maage_status_make(MAAGE_STATUS_INVARIANT_VIOLATION, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_INITIALIZE, 0);
            goto failed;
        }
        memset(&item, 0, sizeof(item));
        item.id = ids[i]; item.has_block = 1; item.block_id = ids[i];
        item.placement_state_id = item.icon_state_id = ids[i]; item.max_stack = 64;
        item.mining_speed = 1; item.texture_id = state->faces[MAAGE_FACE_POSITIVE_Z].texture_id;
        item.display_name = displays[i];
        if (ids[i] == MAAGE_BLOCK_LOG || ids[i] == MAAGE_BLOCK_LEAVES || ids[i] == MAAGE_BLOCK_PLANKS) item.max_metadata = 2;
        item.model = maage_block_registry_block(blocks, ids[i])->mesh_shape == MAAGE_BLOCK_MESH_CUBE ?
            MAAGE_ITEM_MODEL_BLOCK : MAAGE_ITEM_MODEL_SPRITE;
        if (item.block_id == MAAGE_BLOCK_TORCH || item.block_id == MAAGE_BLOCK_LOG || item.block_id == MAAGE_BLOCK_LEAVES || item.block_id == MAAGE_BLOCK_PLANKS)
            item.placement = maage_item_oriented_placement;
        result = maage_item_registry_register(registry, names[i], &item);
        if (maage_status_failed(result)) goto failed;
    }
    {
        static const char *const tier[] = {"wooden", "stone", "iron", "diamond", "golden"};
        static const char *const title[] = {"Wooden", "Stone", "Iron", "Diamond", "Golden"};
        static const char *const family[] = {"pickaxe", "shovel", "axe"};
        static const char *const label[] = {"Pickaxe", "Shovel", "Axe"};
        static const unsigned int durability[] = {59, 131, 250, 1561, 32};
        static const unsigned int level[] = {0, 1, 2, 3, 0};
        static const double speed[] = {2, 4, 6, 8, 12};
        static const char *const resource[] = {"shears", "coal", "diamond", "redstone", "lapis_lazuli",
            "emerald", "flint", "apple", "sapling", "snowball", "wheat_seeds"};
        static const char *const resource_title[] = {"Shears", "Coal", "Diamond", "Redstone Dust", "Lapis Lazuli",
            "Emerald", "Flint", "Apple", "Oak Sapling", "Snowball", "Seeds"};
        char name[80], display[80];
        unsigned int t, f;
        for (t = 0; t < 5; ++t) for (f = 0; f < 3; ++f) {
            memset(&item, 0, sizeof(item));
            item.id = MAAGE_ITEM_TOOL_BASE + t * 3 + f;
            item.max_stack = 1; item.durability = durability[t];
            item.tool_class = f + 1; item.tool_level = level[t]; item.mining_speed = speed[t];
            item.model = MAAGE_ITEM_MODEL_SPRITE; item.texture_index = t * 3 + f;
            item.texture_id = MAAGE_CONTENT_TEXTURE_ITEMS + item.texture_index;
            sprintf(name, "minecraft:%s_%s", tier[t], family[f]);
            sprintf(display, "%s %s", title[t], label[f]); item.display_name = display;
            result = maage_item_registry_register(registry, name, &item);
            if (maage_status_failed(result)) goto failed;
        }
        for (t = 0; t < 11; ++t) {
            memset(&item, 0, sizeof(item)); item.id = MAAGE_ITEM_SHEARS + t;
            item.max_stack = t == 0 ? 1 : (t == 9 ? 16 : 64); item.mining_speed = 1;
            if (!t) { item.durability = 238; item.tool_class = MAAGE_TOOL_SHEARS; }
            if (item.id == MAAGE_ITEM_SAPLING) item.max_metadata = 2;
            if (item.id == MAAGE_ITEM_COAL) item.max_metadata = 1;
            item.model = MAAGE_ITEM_MODEL_SPRITE; item.texture_index = 15 + t;
            item.texture_id = MAAGE_CONTENT_TEXTURE_ITEMS + item.texture_index;
            sprintf(name, "minecraft:%s", resource[t]); item.display_name = resource_title[t];
            result = maage_item_registry_register(registry, name, &item);
            if (maage_status_failed(result)) goto failed;
        }
    }
    {
        static const char *const names_new[] = {"stick", "iron_ingot", "gold_ingot", "gold_nugget", "bowl", "mushroom_stew"};
        static const char *const labels[] = {"Stick", "Iron Ingot", "Gold Ingot", "Gold Nugget", "Bowl", "Mushroom Stew"};
        char name[80];
        for (i = 0; i < 6; ++i) {
            memset(&item, 0, sizeof(item)); item.id = MAAGE_ITEM_STICK + i;
            item.max_stack = i == 5 ? 1U : 64U; item.mining_speed = 1;
            item.model = MAAGE_ITEM_MODEL_SPRITE; item.texture_index = 28 + (unsigned int)i;
            item.texture_id = MAAGE_CONTENT_TEXTURE_ITEMS + item.texture_index;
            item.display_name = labels[i]; strcpy(name, "minecraft:"); strcat(name, names_new[i]);
            result = maage_item_registry_register(registry, name, &item);
            if (maage_status_failed(result)) goto failed;
        }
    }
    result = maage_item_registry_finalize(registry);
    if (!maage_status_failed(result)) { *output = registry; return result; }
failed:
    maage_item_registry_destroy(registry); return result;
}
int maage_item_stack_valid(const maage_item_registry *registry, const maage_item_stack *stack)
{
    const maage_item_definition *item;
    if (!stack) return 0;
    if (!stack->count) return !stack->item_id && !stack->metadata && !stack->damage;
    item = maage_item_registry_get(registry, stack->item_id);
    return item && stack->count <= item->max_stack && stack->metadata <= item->max_metadata &&
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
maage_status maage_inventory_add(maage_inventory *inventory,
    const maage_item_registry *registry, const maage_item_stack *stack,
    unsigned int *uninserted)
{
    const maage_item_definition *item;
    maage_item_stack *destination;
    unsigned int i, pass, remaining, amount;
    if (uninserted) *uninserted = stack ? stack->count : 0;
    if (!inventory || !maage_item_stack_valid(registry, stack) || !uninserted)
        return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_UPDATE, !inventory ? 0 : (!uninserted ? 3 : 2));
    if (!stack->count) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_UPDATE, 0);
    item = maage_item_registry_get(registry, stack->item_id); remaining = stack->count;
    for (pass = 0; pass < 2; ++pass) for (i = 0; i < MAAGE_INVENTORY_SLOTS && remaining; ++i) {
        destination = &inventory->slots[i];
        if (pass == 0 ? (!destination->count || !maage_stack_matches(destination, stack)) : destination->count) continue;
        amount = item->max_stack - destination->count;
        if (amount > remaining) amount = remaining;
        if (!destination->count) { *destination = *stack; destination->count = 0; }
        destination->count += amount; remaining -= amount;
    }
    *uninserted = remaining;
    return maage_status_make(remaining ? MAAGE_STATUS_PARTIAL : MAAGE_STATUS_SUCCESS, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_UPDATE, 0);
}
maage_status maage_item_stack_click(maage_item_stack *source, maage_item_stack *cursor,
    const maage_item_registry *registry, int secondary)
{
    maage_item_stack temporary;
    const maage_item_definition *item;
    unsigned int amount;
    if (!source || !cursor || source == cursor || (secondary != 0 && secondary != 1))
        return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_UPDATE, 0);
    if (!maage_item_stack_valid(registry, source) || !maage_item_stack_valid(registry, cursor)) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_UPDATE, 0);
    if (!cursor->count) {
        if (!source->count) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_UPDATE, 0);
        *cursor = *source; cursor->count = secondary ? (source->count + 1) / 2 : source->count;
        source->count -= cursor->count;
        if (!source->count) memset(source, 0, sizeof(*source));
        return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_UPDATE, 0);
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
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_UPDATE, 0);
}
maage_status maage_inventory_click(maage_inventory *inventory,
    const maage_item_registry *registry, unsigned int slot, int secondary)
{
    if (!inventory || slot >= MAAGE_INVENTORY_SLOTS)
        return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_UPDATE, 0);
    return maage_item_stack_click(&inventory->slots[slot], &inventory->cursor, registry, secondary);
}
maage_status maage_inventory_creative(maage_inventory *inventory,
    const maage_item_registry *registry, maage_item_id id, unsigned int metadata)
{
    const maage_item_definition *item = maage_item_registry_get(registry, id);
    if (!inventory || !item || metadata > item->max_metadata) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_UPDATE, 0);
    memset(&inventory->cursor, 0, sizeof(inventory->cursor));
    inventory->cursor.item_id = id; inventory->cursor.count = item->max_stack; inventory->cursor.metadata = metadata;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_UPDATE, 0);
}
maage_status maage_item_placement_state(const maage_item_definition *item,
    unsigned int metadata, maage_block_face face, double yaw,
    maage_block_state_id *state)
{
    if (!item || !item->has_block || !state || face >= MAAGE_FACE_COUNT) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_RESOLVE, 0);
    if (item->placement) return item->placement(item, metadata, face, yaw, state);
    if (metadata) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_RESOLVE, 0);
    *state = item->placement_state_id; return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_RESOLVE, 0);
}

const char *maage_item_registry_name(const maage_item_registry *registry, maage_item_id id)
{
    maage_item_slot *slot = maage_item_slot_get(registry, id);
    return slot ? slot->name : NULL;
}
maage_status maage_item_registry_resolve_name(
    const maage_item_registry *registry, const char *name, maage_item_id *id)
{
    size_t bucket;
    int found;
    if (!registry || !name || !id) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_RESOLVE, 0);
    bucket = maage_item_name_bucket(registry, name, &found);
    if (!found) return maage_status_make(MAAGE_STATUS_NOT_FOUND, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_RESOLVE, 0);
    *id = registry->ordered[registry->name_buckets[bucket] - 1];
    return maage_status_make(MAAGE_STATUS_SUCCESS, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_RESOLVE, 0);
}

int maage_item_can_harvest(const maage_item_definition *tool,
    const maage_block_definition *block)
{
    return block && (!block->harvest_requires_tool || (tool &&
        tool->tool_class == block->required_tool && tool->tool_level >= block->required_tool_level));
}
double maage_item_mining_speed(const maage_item_definition *tool,
    const maage_block_definition *block)
{
    if (!tool || !block) return 1;
    if (tool->tool_class == MAAGE_TOOL_SHEARS && block->id == MAAGE_BLOCK_LEAVES) return 15;
    return tool->tool_class == block->required_tool && tool->tool_class != MAAGE_TOOL_NONE ? tool->mining_speed : 1;
}
double maage_item_mining_damage(const maage_item_definition *tool,
    const maage_block_definition *block, int grounded, int submerged)
{
    double speed;
    if (!block || block->hardness < 0) return 0;
    if (block->hardness == 0) return 1;
    speed = maage_item_mining_speed(tool, block);
    if (!grounded) speed /= 5;
    if (submerged) speed /= 5;
    return speed / (block->hardness * (maage_item_can_harvest(tool, block) ? 30 : 100));
}
static unsigned long maage_drop_random(unsigned long *random, unsigned long range)
{
    *random = (*random * 1664525UL + 1013904223UL) & 4294967295UL;
    return (*random >> 8) % range;
}
maage_status maage_item_generate_drops(const maage_block_registry *blocks,
    const maage_item_registry *items, maage_block_state_id id,
    const maage_item_definition *tool, unsigned long *random, maage_item_drops *drops)
{
    const maage_block_state_definition *state = maage_block_registry_state(blocks, id);
    const maage_block_definition *block = state ? maage_block_registry_block(blocks, state->block_id) : NULL;
    maage_item_stack *stack;
    size_t i;
    if (!block || !random || !drops) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT,
        MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_RESOLVE, 0);
    memset(drops, 0, sizeof(*drops));
    if (!maage_item_can_harvest(tool, block)) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_RESOLVE, 0);
    stack = &drops->stacks[0]; stack->item_id = block->drop_item_id; stack->count = block->drop_count;
    switch (block->id) {
    case MAAGE_BLOCK_STONE: stack->item_id = MAAGE_BLOCK_COBBLESTONE; break;
    case MAAGE_BLOCK_COAL_ORE: stack->item_id = MAAGE_ITEM_COAL; break;
    case MAAGE_BLOCK_DIAMOND_ORE: stack->item_id = MAAGE_ITEM_DIAMOND; break;
    case MAAGE_BLOCK_EMERALD_ORE: stack->item_id = MAAGE_ITEM_EMERALD; break;
    case MAAGE_BLOCK_REDSTONE_ORE: stack->item_id = MAAGE_ITEM_REDSTONE; stack->count = 4 + (unsigned int)maage_drop_random(random, 2); break;
    case MAAGE_BLOCK_LAPIS_ORE: stack->item_id = MAAGE_ITEM_LAPIS; stack->count = 4 + (unsigned int)maage_drop_random(random, 5); break;
    case MAAGE_BLOCK_GRAVEL: if (!maage_drop_random(random, 10)) stack->item_id = MAAGE_ITEM_FLINT; break;
    case MAAGE_BLOCK_PLANKS:
    case MAAGE_BLOCK_LOG: stack->metadata = state->metadata & 3U; break;
    case MAAGE_BLOCK_LEAVES:
        if (tool && tool->tool_class == MAAGE_TOOL_SHEARS) {
            stack->item_id = MAAGE_BLOCK_LEAVES; stack->count = 1; stack->metadata = state->metadata & 3U;
        } else {
            stack->item_id = MAAGE_ITEM_SAPLING; stack->metadata = state->metadata & 3U;
            stack->count = maage_drop_random(random, 20) == 0 ? 1U : 0U;
            if (!(state->metadata & 3U) && !maage_drop_random(random, 200)) {
                drops->stacks[1].item_id = MAAGE_ITEM_APPLE; drops->stacks[1].count = 1;
            }
        }
        break;
    case MAAGE_BLOCK_TALL_GRASS:
        stack->item_id = tool && tool->tool_class == MAAGE_TOOL_SHEARS ? MAAGE_BLOCK_TALL_GRASS : MAAGE_ITEM_SEEDS;
        stack->count = stack->item_id == MAAGE_BLOCK_TALL_GRASS || !maage_drop_random(random, 8) ? 1U : 0U;
        break;
    case MAAGE_BLOCK_DEAD_BUSH:
        stack->count = tool && tool->tool_class == MAAGE_TOOL_SHEARS ? 1U : 0U; break;
    case MAAGE_BLOCK_SNOW: stack->item_id = MAAGE_ITEM_SNOWBALL; stack->count = 1; break;
    case MAAGE_BLOCK_ICE: stack->count = 0; break;
    default: break;
    }
    for (i = 0; i < MAAGE_DROP_STACKS; ++i) {
        if (!drops->stacks[i].count) continue;
        if (!maage_item_stack_valid(items, &drops->stacks[i])) return maage_status_make(
            MAAGE_STATUS_INVARIANT_VIOLATION, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_RESOLVE, 0);
        drops->stacks[drops->count++] = drops->stacks[i];
    }
    return maage_status_make(drops->count ? MAAGE_STATUS_SUCCESS : MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_RESOLVE, 0);
}
