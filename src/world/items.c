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
    maage_builtin_ids ids;
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
    memset(&registry->ids, 255, sizeof(registry->ids));
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
    slot->definition.builtins = &registry->ids;
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
maage_status maage_item_builtin_placement(const maage_item_definition *item, unsigned int metadata, maage_block_face face, double yaw, maage_block_state_id *state) {
    const maage_builtin_ids *ids = item->builtins;
    maage_block_state_id torches[6];
    unsigned int species;
    torches[0] = ids->torches[2];
    torches[1] = ids->torches[1];
    torches[2] = ids->state_air;
    torches[3] = ids->state_torch;
    torches[4] = ids->torches[4];
    torches[5] = ids->torches[3];

    if ((unsigned int) face >= MAAGE_FACE_COUNT) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_RESOLVE, 0);
    (void) yaw;
    if (item->block_id == ids->block_sand) {
        if (metadata > 1) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_RESOLVE, 0);
        *state = metadata ? ids->state_red_sand : ids->state_sand;
    } else if (item->block_id == ids->block_torch) {
        *state = torches[face];
        if (face == MAAGE_FACE_NEGATIVE_Y) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_RESOLVE, 0);
    } else {
        if (item->block_id == ids->block_planks) {
            if (metadata >= 6) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_RESOLVE, 0);
            *state = metadata ? ids->planks[metadata] : ids->state_planks;
            return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_RESOLVE, 0);
        }
        if (item->block_id == ids->block_log2) {
            if (metadata > 1) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_RESOLVE, 0);
            *state = ids->logs2[(face / 2 == 0 ? 4U : (face / 2 == 2 ? 8U : 0U)) + metadata];
            return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_RESOLVE, 0);
        }
        species = metadata & 3U;
        if (species > (item->block_id == ids->block_log ? 3U : 2U)) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_RESOLVE, 0);
        if (item->block_id == ids->block_sapling) return (*state = ids->saplings[species], maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_RESOLVE, 0));
        if (item->block_id == ids->block_planks) *state = species ? ids->planks[species] : ids->state_planks;
        else if (item->block_id == ids->block_leaves) return (*state = ids->leaves[species | MAAGE_LEAF_PERSISTENT], maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_RESOLVE, 0));
        else if (face / 2 == 0) *state = ids->logs[4 + species];
        else if (face / 2 == 2) *state = ids->logs[8 + species];
        else
            *state = species ? ids->logs[species] : ids->state_log;
    }
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_RESOLVE, 0);
}

maage_status maage_item_registry_create_builtin(const maage_block_registry *blocks, maage_item_registry **output) {
    maage_builtin_ids *ids;
    const maage_builtin_ids *initial_ids = maage_block_registry_builtin_ids(blocks);
    maage_block_id block_ids[39];
    static const char *const names[] = {"minecraft:stone", "minecraft:grass", "minecraft:dirt", "minecraft:bedrock", "minecraft:sand", "minecraft:gravel", "minecraft:sandstone", "minecraft:cobblestone", "minecraft:planks", "minecraft:gold_ore", "minecraft:iron_ore", "minecraft:coal_ore", "minecraft:log", "minecraft:leaves", "minecraft:lapis_ore", "minecraft:tall_grass", "minecraft:dead_bush", "minecraft:dandelion", "minecraft:poppy", "minecraft:brown_mushroom", "minecraft:red_mushroom", "minecraft:mossy_cobblestone", "minecraft:obsidian", "minecraft:torch", "minecraft:diamond_ore", "minecraft:redstone_ore", "minecraft:snow", "minecraft:ice", "minecraft:cactus", "minecraft:emerald_ore", "minecraft:crafting_table", "minecraft:gold_block", "minecraft:iron_block", "minecraft:diamond_block", "minecraft:lapis_block", "minecraft:emerald_block", "minecraft:redstone_block", "minecraft:coal_block", "minecraft:snow_block"};
    static const char *const displays[] = {"Stone", "Grass Block", "Dirt", "Bedrock", "Sand", "Gravel", "Sandstone", "Cobblestone", "Oak Planks", "Gold Ore", "Iron Ore", "Coal Ore", "Oak Log", "Oak Leaves", "Lapis Ore", "Tall Grass", "Dead Bush", "Dandelion", "Poppy", "Brown Mushroom", "Red Mushroom", "Mossy Cobblestone", "Obsidian", "Torch", "Diamond Ore", "Redstone Ore", "Snow Layer", "Ice", "Cactus", "Emerald Ore", "Crafting Table", "Block of Gold", "Block of Iron", "Block of Diamond", "Lapis Lazuli Block", "Block of Emerald", "Block of Redstone", "Block of Coal", "Snow"};
    maage_item_registry *registry;
    maage_item_id *references[39];
    maage_block_state_id placement;
    maage_item_definition item;
    const maage_block_state_definition *state;
    size_t i;
    maage_status result;
    if (output) *output = NULL;
    if (!initial_ids || !output) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_INITIALIZE, 0);
    block_ids[0] = initial_ids->block_stone;
    block_ids[1] = initial_ids->block_grass;
    block_ids[2] = initial_ids->block_dirt;
    block_ids[3] = initial_ids->block_bedrock;
    block_ids[4] = initial_ids->block_sand;
    block_ids[5] = initial_ids->block_gravel;
    block_ids[6] = initial_ids->block_sandstone;
    block_ids[7] = initial_ids->block_cobblestone;
    block_ids[8] = initial_ids->block_planks;
    block_ids[9] = initial_ids->block_gold_ore;
    block_ids[10] = initial_ids->block_iron_ore;
    block_ids[11] = initial_ids->block_coal_ore;
    block_ids[12] = initial_ids->block_log;
    block_ids[13] = initial_ids->block_leaves;
    block_ids[14] = initial_ids->block_lapis_ore;
    block_ids[15] = initial_ids->block_tall_grass;
    block_ids[16] = initial_ids->block_dead_bush;
    block_ids[17] = initial_ids->block_dandelion;
    block_ids[18] = initial_ids->block_poppy;
    block_ids[19] = initial_ids->block_brown_mushroom;
    block_ids[20] = initial_ids->block_red_mushroom;
    block_ids[21] = initial_ids->block_mossy_cobblestone;
    block_ids[22] = initial_ids->block_obsidian;
    block_ids[23] = initial_ids->block_torch;
    block_ids[24] = initial_ids->block_diamond_ore;
    block_ids[25] = initial_ids->block_redstone_ore;
    block_ids[26] = initial_ids->block_snow;
    block_ids[27] = initial_ids->block_ice;
    block_ids[28] = initial_ids->block_cactus;
    block_ids[29] = initial_ids->block_emerald_ore;
    block_ids[30] = initial_ids->block_crafting_table;
    block_ids[31] = initial_ids->block_gold_block;
    block_ids[32] = initial_ids->block_iron_block;
    block_ids[33] = initial_ids->block_diamond_block;
    block_ids[34] = initial_ids->block_lapis_block;
    block_ids[35] = initial_ids->block_emerald_block;
    block_ids[36] = initial_ids->block_redstone_block;
    block_ids[37] = initial_ids->block_coal_block;
    block_ids[38] = initial_ids->block_snow_block;
    if (!maage_block_registry_block_count(blocks)) return maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_INITIALIZE, 0);
    result = maage_item_registry_create(0, &registry);
    if (maage_status_failed(result)) return result;
    registry->ids = *initial_ids;
    ids = &registry->ids;
    references[0] = &ids->item_stone;
    references[1] = &ids->item_grass;
    references[2] = &ids->item_dirt;
    references[3] = &ids->item_bedrock;
    references[4] = &ids->item_sand;
    references[5] = &ids->item_gravel;
    references[6] = &ids->item_sandstone;
    references[7] = &ids->item_cobblestone;
    references[8] = &ids->item_planks;
    references[9] = &ids->item_gold_ore;
    references[10] = &ids->item_iron_ore;
    references[11] = &ids->item_coal_ore;
    references[12] = &ids->item_log;
    references[13] = &ids->item_leaves;
    references[14] = &ids->item_lapis_ore;
    references[15] = &ids->item_tall_grass;
    references[16] = &ids->item_dead_bush;
    references[17] = &ids->item_dandelion;
    references[18] = &ids->item_poppy;
    references[19] = &ids->item_brown_mushroom;
    references[20] = &ids->item_red_mushroom;
    references[21] = &ids->item_mossy_cobblestone;
    references[22] = &ids->item_obsidian;
    references[23] = &ids->item_torch;
    references[24] = &ids->item_diamond_ore;
    references[25] = &ids->item_redstone_ore;
    references[26] = &ids->item_snow;
    references[27] = &ids->item_ice;
    references[28] = &ids->item_cactus;
    references[29] = &ids->item_emerald_ore;
    references[30] = &ids->item_crafting_table;
    references[31] = &ids->item_gold_block;
    references[32] = &ids->item_iron_block;
    references[33] = &ids->item_diamond_block;
    references[34] = &ids->item_lapis_block;
    references[35] = &ids->item_emerald_block;
    references[36] = &ids->item_redstone_block;
    references[37] = &ids->item_coal_block;
    references[38] = &ids->item_snow_block;
    for (i = 0; i < sizeof(block_ids) / sizeof(block_ids[0]); ++i) {
        result = maage_block_registry_resolve_state(blocks, block_ids[i], block_ids[i] == ids->block_tall_grass ? 1U : (block_ids[i] == ids->block_torch ? 5U : 0U), &placement);
        state = maage_status_failed(result) ? NULL : maage_block_registry_state(blocks, placement);
        if (!state) {
            result = maage_status_make(MAAGE_STATUS_INVARIANT_VIOLATION, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_INITIALIZE, 0);
            goto failed;
        }
        memset(&item, 0, sizeof(item));
        item.has_block = 1;
        item.block_id = block_ids[i];
        item.placement_state_id = item.icon_state_id = placement;
        item.max_stack = 64;
        item.mining_speed = 1;
        if (item.block_id == ids->block_planks || item.block_id == ids->block_log || item.block_id == ids->block_crafting_table) item.fuel_ticks = 300;
        if (item.block_id == ids->block_coal_block) item.fuel_ticks = 16000;
        item.texture_id = state->faces[MAAGE_FACE_POSITIVE_Z].texture_id;
        item.display_name = displays[i];
        if (block_ids[i] == ids->block_log || block_ids[i] == ids->block_leaves || block_ids[i] == ids->block_planks) item.max_metadata = block_ids[i] == ids->block_log ? 3U : (block_ids[i] == ids->block_planks ? 5U : 2U);
        if (block_ids[i] == ids->block_sand) {
            item.max_metadata = 1;
            item.placement = maage_item_builtin_placement;
        }
        item.model = maage_block_registry_block(blocks, block_ids[i])->mesh_shape == MAAGE_BLOCK_MESH_CUBE ? MAAGE_ITEM_MODEL_BLOCK : MAAGE_ITEM_MODEL_SPRITE;
        if (item.block_id == ids->block_torch || item.block_id == ids->block_log || item.block_id == ids->block_leaves || item.block_id == ids->block_planks) item.placement = maage_item_builtin_placement;
        result = maage_item_registry_register(registry, names[i], &item);
        if (maage_status_failed(result)) goto failed;
        *references[i] = item.id;
    }
    {
        static const char *const tier[] = {"wooden", "stone", "iron", "diamond", "golden"};
        static const char *const title[] = {"Wooden", "Stone", "Iron", "Diamond", "Golden"};
        static const char *const family[] = {"pickaxe", "shovel", "axe", "hoe"};
        static const char *const label[] = {"Pickaxe", "Shovel", "Axe", "Hoe"};
        static const unsigned int classes[] = {MAAGE_TOOL_PICKAXE, MAAGE_TOOL_SHOVEL, MAAGE_TOOL_AXE, MAAGE_TOOL_HOE};
        static const unsigned int durability[] = {59, 131, 250, 1561, 32};
        static const unsigned int level[] = {0, 1, 2, 3, 0};
        static const double speed[] = {2, 4, 6, 8, 12};
        static const char *const resource[] = {"shears", "coal", "diamond", "redstone", "lapis_lazuli",
                                               "emerald", "flint", "apple", "sapling", "snowball", "wheat_seeds"};
        static const char *const resource_title[] = {"Shears", "Coal", "Diamond", "Redstone Dust", "Lapis Lazuli",
                                                     "Emerald", "Flint", "Apple", "Oak Sapling", "Snowball", "Seeds"};
        char name[80], display[80];
        unsigned int t, f;
        for (t = 0; t < 5; ++t)
            for (f = 0; f < 4; ++f) {
                memset(&item, 0, sizeof(item));
                item.max_stack = 1;
                item.durability = durability[t];
                if (!t) item.fuel_ticks = 200;
                item.tool_class = classes[f];
                item.tool_level = level[t];
                item.mining_speed = f == 3 ? 1 : speed[t];
                item.model = MAAGE_ITEM_MODEL_SPRITE;
                item.texture_index = f == 3 ? (t ? 58 + t : 54) : t * 3 + f;
                item.texture_id = MAAGE_CONTENT_TEXTURE_ITEMS + item.texture_index;
                sprintf(name, "minecraft:%s_%s", tier[t], family[f]);
                sprintf(display, "%s %s", title[t], label[f]);
                item.display_name = display;
                result = maage_item_registry_register(registry, name, &item);
                if (maage_status_failed(result)) goto failed;
                ids->tools[t][f] = item.id;
            }
        ids->item_wooden_hoe = ids->tools[0][3];
        {
            maage_item_id *resources[11];
            resources[0] = &ids->item_shears;
            resources[1] = &ids->item_coal;
            resources[2] = &ids->item_diamond;
            resources[3] = &ids->item_redstone;
            resources[4] = &ids->item_lapis;
            resources[5] = &ids->item_emerald;
            resources[6] = &ids->item_flint;
            resources[7] = &ids->item_apple;
            resources[8] = &ids->item_sapling;
            resources[9] = &ids->item_snowball;
            resources[10] = &ids->item_seeds;
            for (t = 0; t < 11; ++t) {
                memset(&item, 0, sizeof(item));
                item.max_stack = t == 0 ? 1 : (t == 9 ? 16 : 64);
                item.mining_speed = 1;
                if (!t) {
                    item.durability = 238;
                    item.tool_class = MAAGE_TOOL_SHEARS;
                }
                if (t == 8) {
                    item.max_metadata = 2;
                    item.has_block = 1;
                    item.block_id = ids->block_sapling;
                    item.placement_state_id = item.icon_state_id = ids->state_sapling;
                    item.placement = maage_item_builtin_placement;
                }
                if (t == 10) {
                    item.has_block = 1;
                    item.block_id = ids->block_wheat;
                    item.placement_state_id = item.icon_state_id = ids->wheat[0];
                }
                if (t == 1) {
                    item.max_metadata = 1;
                    item.fuel_ticks = 1600;
                }
                if (t == 8) item.fuel_ticks = 100;
                if (t == 7) {
                    item.food_hunger = 4;
                    item.food_saturation = 0.3;
                    item.food_ticks = 32;
                }
                item.model = MAAGE_ITEM_MODEL_SPRITE;
                item.texture_index = 15 + t;
                item.texture_id = MAAGE_CONTENT_TEXTURE_ITEMS + item.texture_index;
                sprintf(name, "minecraft:%s", resource[t]);
                item.display_name = resource_title[t];
                result = maage_item_registry_register(registry, name, &item);
                if (maage_status_failed(result)) goto failed;
                *resources[t] = item.id;
            }
        }
    }
    {
        maage_item_id *resources[6];
        static const char *const names_new[] = {"stick", "iron_ingot", "gold_ingot", "gold_nugget", "bowl", "mushroom_stew"};
        static const char *const labels[] = {"Stick", "Iron Ingot", "Gold Ingot", "Gold Nugget", "Bowl", "Mushroom Stew"};
        char name[80];
        resources[0] = &ids->item_stick;
        resources[1] = &ids->item_iron_ingot;
        resources[2] = &ids->item_gold_ingot;
        resources[3] = &ids->item_gold_nugget;
        resources[4] = &ids->item_bowl;
        resources[5] = &ids->item_mushroom_stew;
        for (i = 0; i < 6; ++i) {
            memset(&item, 0, sizeof(item));
            item.max_stack = i == 5 ? 1U : 64U;
            if (!i) item.fuel_ticks = 100;
            item.mining_speed = 1;
            item.model = MAAGE_ITEM_MODEL_SPRITE;
            item.texture_index = 28 + (unsigned int) i;
            item.texture_id = MAAGE_CONTENT_TEXTURE_ITEMS + item.texture_index;
            item.display_name = labels[i];
            if (i == 5) {
                item.food_hunger = 6;
                item.food_saturation = 0.6;
                item.food_ticks = 32;
                item.container_item = ids->item_bowl;
            }
            strcpy(name, "minecraft:");
            strcat(name, names_new[i]);
            result = maage_item_registry_register(registry, name, &item);
            if (maage_status_failed(result)) goto failed;
            *resources[i] = item.id;
        }
    }
    result = maage_furnace_content_items(registry, blocks, ids);
    if (maage_status_failed(result)) goto failed;
    memset(&item, 0, sizeof(item));
    item.max_stack = 64;
    item.mining_speed = 1;
    item.model = MAAGE_ITEM_MODEL_SPRITE;
    item.texture_index = 63;
    item.texture_id = MAAGE_CONTENT_TEXTURE_ITEMS + item.texture_index;
    item.display_name = "Wheat";
    result = maage_item_registry_register(registry, "minecraft:wheat", &item);
    if (maage_status_failed(result)) goto failed;
    ids->item_wheat = item.id;
    result = maage_item_registry_finalize(registry);
    if (!maage_status_failed(result)) {
        *output = registry;
        return result;
    }
failed:
    maage_item_registry_destroy(registry);
    return result;
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
    const maage_builtin_ids *ids = block ? block->builtins : NULL;
    if (!tool || !block) return 1;
    if (tool->tool_class == MAAGE_TOOL_SHEARS && block->id == ids->block_leaves) return 15;
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
static unsigned long maage_drop_random(unsigned long *random, unsigned long range) {
    *random = (*random * 1664525UL + 1013904223UL) & 4294967295UL;
    return (*random >> 8) % range;
}
maage_status maage_item_generate_drops(const maage_block_registry *blocks, const maage_item_registry *items, maage_block_state_id id, const maage_item_definition *tool, unsigned long *random, maage_item_drops *drops) {
    const maage_builtin_ids *ids = maage_block_registry_builtin_ids(blocks);
    const maage_block_state_definition *state = maage_block_registry_state(blocks, id);
    const maage_block_definition *block = state ? maage_block_registry_block(blocks, state->block_id) : NULL;
    maage_item_stack *stack;
    size_t i;
    if (!block || !random || !drops) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_RESOLVE, 0);
    memset(drops, 0, sizeof(*drops));
    if (!maage_item_can_harvest(tool, block)) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_RESOLVE, 0);
    stack = &drops->stacks[0];
    stack->item_id = block->drop_item_id;
    stack->count = block->drop_count;
    if (block->id == ids->block_wheat) {
        stack->item_id = state->metadata == 7 ? ids->item_wheat : ids->item_seeds;
        stack->count = 1;
        if (state->metadata == 7) {
            drops->stacks[1].item_id = ids->item_seeds;
            for (i = 0; i < 3; ++i)
                if (maage_drop_random(random, 15) <= state->metadata) ++drops->stacks[1].count;
        }
    } else if (block->id == ids->block_stone) {
        stack->item_id = ids->item_cobblestone;
    } else if (block->id == ids->block_coal_ore) {
        stack->item_id = ids->item_coal;
    } else if (block->id == ids->block_diamond_ore) {
        stack->item_id = ids->item_diamond;
    } else if (block->id == ids->block_emerald_ore) {
        stack->item_id = ids->item_emerald;
    } else if (block->id == ids->block_redstone_ore) {
        stack->item_id = ids->item_redstone;
        stack->count = 4 + (unsigned int) maage_drop_random(random, 2);
    } else if (block->id == ids->block_lapis_ore) {
        stack->item_id = ids->item_lapis;
        stack->count = 4 + (unsigned int) maage_drop_random(random, 5);
    } else if (block->id == ids->block_gravel) {
        if (!maage_drop_random(random, 10)) stack->item_id = ids->item_flint;
    } else if (block->id == ids->block_sand || block->id == ids->block_sapling || block->id == ids->block_planks || block->id == ids->block_log || block->id == ids->block_log2) {
        stack->metadata = block->id == ids->block_planks ? state->metadata : (state->metadata & 3U);
    } else if (block->id == ids->block_leaves) {
        if (tool && tool->tool_class == MAAGE_TOOL_SHEARS) {
            stack->item_id = ids->item_leaves;
            stack->count = 1;
            stack->metadata = state->metadata & 3U;
        } else {
            stack->item_id = ids->item_sapling;
            stack->metadata = state->metadata & 3U;
            stack->count = maage_drop_random(random, 20) == 0 ? 1U : 0U;
            if (!(state->metadata & 3U) && !maage_drop_random(random, 200)) {
                drops->stacks[1].item_id = ids->item_apple;
                drops->stacks[1].count = 1;
            }
        }
    } else if (block->id == ids->block_tall_grass) {
        stack->item_id = tool && tool->tool_class == MAAGE_TOOL_SHEARS ? ids->item_tall_grass : ids->item_seeds;
        stack->count = stack->item_id == ids->item_tall_grass || !maage_drop_random(random, 8) ? 1U : 0U;
    } else if (block->id == ids->block_dead_bush) {
        stack->count = tool && tool->tool_class == MAAGE_TOOL_SHEARS ? 1U : 0U;
    } else if (block->id == ids->block_snow) {
        stack->item_id = ids->item_snowball;
        stack->count = 1;
    } else if (block->id == ids->block_ice) {
        stack->count = 0;
    }
    for (i = 0; i < MAAGE_DROP_STACKS; ++i) {
        if (!drops->stacks[i].count) continue;
        if (!maage_item_stack_valid(items, &drops->stacks[i])) return maage_status_make(MAAGE_STATUS_INVARIANT_VIOLATION, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_RESOLVE, 0);
        drops->stacks[drops->count++] = drops->stacks[i];
    }
    return maage_status_make(drops->count ? MAAGE_STATUS_SUCCESS : MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_RESOLVE, 0);
}

const maage_builtin_ids *maage_item_registry_builtin_ids(const maage_item_registry *registry) {
    return registry ? &registry->ids : NULL;
}
maage_item_id maage_item_registry_block_item(const maage_item_registry *registry, maage_block_id block) {
    void **entry = registry ? maage_registry_pages_find(&registry->block_items, block) : NULL;
    return entry && *entry ? ((const maage_item_definition *) *entry)->id : 0;
}
