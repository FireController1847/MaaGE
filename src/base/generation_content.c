#include "base_internal.h"
static maage_status emerald_density(const void *context, maage_world_seed seed, const maage_biome_registry *biomes, const maage_generation_policy *policy, int x, int z, void *cache, double *density) {
    const maage_base_ids *ids = (const maage_base_ids *) context;
    maage_generation_sample sample;
    if (!maage_generation_sample_at(ids, seed, biomes, policy, x, z, &sample, (maage_generation_region_cache *) cache)) return maage_status_make(MAAGE_STATUS_INVARIANT_VIOLATION, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_ORES, 0);
    *density = sample.decoration.emeralds;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_ORES, 0);
}
static maage_status base_ore(maage_block_registry *blocks, const maage_base_ids *ids, const char *name, maage_block_state_id state, unsigned int attempts, unsigned int size, unsigned int minimum, unsigned int maximum, unsigned long salt, int triangular, int emerald) {
    maage_ore_definition ore;
    ore.context = ids;
    ore.state = state;
    ore.attempts = attempts;
    ore.size = size;
    ore.minimum_y = minimum;
    ore.maximum_y = maximum;
    ore.salt = salt;
    ore.triangular = triangular;
    ore.density = emerald ? emerald_density : NULL;
    return maage_generation_ore_register(blocks, name, &ore, NULL);
}
maage_status maage_base_generation_register(maage_content_context *context, const maage_base_ids *ids) {
    maage_status result;
    result = base_ore(context->blocks, ids, "minecraft:dirt", ids->state_dirt, 20, 32, 0, 256, 11000, 0, 0);
    if (maage_status_failed(result)) return result;
    result = base_ore(context->blocks, ids, "minecraft:gravel", ids->state_gravel, 10, 32, 0, 256, 11001, 0, 0);
    if (maage_status_failed(result)) return result;
    result = base_ore(context->blocks, ids, "minecraft:coal_ore", ids->state_coal_ore, 20, 17, 0, 128, 11002, 0, 0);
    if (maage_status_failed(result)) return result;
    result = base_ore(context->blocks, ids, "minecraft:iron_ore", ids->state_iron_ore, 20, 9, 0, 64, 11003, 0, 0);
    if (maage_status_failed(result)) return result;
    result = base_ore(context->blocks, ids, "minecraft:gold_ore", ids->state_gold_ore, 2, 9, 0, 32, 11004, 0, 0);
    if (maage_status_failed(result)) return result;
    result = base_ore(context->blocks, ids, "minecraft:redstone_ore", ids->state_redstone_ore, 8, 8, 0, 16, 11005, 0, 0);
    if (maage_status_failed(result)) return result;
    result = base_ore(context->blocks, ids, "minecraft:diamond_ore", ids->state_diamond_ore, 1, 8, 0, 16, 11006, 0, 0);
    if (maage_status_failed(result)) return result;
    result = base_ore(context->blocks, ids, "minecraft:lapis_ore", ids->state_lapis_ore, 1, 7, 0, 32, 11007, 1, 0);
    if (maage_status_failed(result)) return result;
    result = base_ore(context->blocks, ids, "minecraft:emerald_ore", ids->state_emerald_ore, 5, 1, 4, 32, 11008, 0, 1);
    if (maage_status_failed(result)) return result;
    return result;
}
