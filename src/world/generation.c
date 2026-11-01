#include "world_internal.h"
maage_status maage_generate_chunk_job(maage_chunk *chunk, maage_world_seed seed, const maage_block_registry *blocks, const maage_biome_registry *biomes, const maage_generation_policy *policy, const maage_job_token *token, maage_status_trace *trace) {
    const maage_registry_bindings *bindings = maage_block_registry_bindings(blocks);
    if (!bindings || !bindings->generate) return maage_status_make(MAAGE_STATUS_NOT_IMPLEMENTED, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_GENERATE, 0);
    return bindings->generate(chunk, seed, blocks, biomes, policy, token, trace);
}
maage_status maage_generation_spawn(maage_world_seed seed, const maage_block_registry *blocks, const maage_biome_registry *biomes, const maage_generation_policy *policy, double feet[3]) {
    const maage_registry_bindings *bindings = maage_block_registry_bindings(blocks);
    if (!bindings || !bindings->spawn) return maage_status_make(MAAGE_STATUS_NOT_IMPLEMENTED, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_GENERATE, 0);
    return bindings->spawn(seed, blocks, biomes, policy, feet);
}

maage_status maage_generation_policy_default(maage_generation_policy *policy) {
    if (!policy) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_INITIALIZE, 0);
    policy->sea_level = 63;
    policy->spawn_search_radius = 4096;
    policy->spawn_search_step = 16;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_INITIALIZE, 0);
}

int maage_generation_policy_valid(const maage_generation_policy *policy) {
    return policy && policy->sea_level >= 0 && policy->sea_level < MAAGE_WORLD_HEIGHT &&
           policy->spawn_search_radius >= 0 && policy->spawn_search_radius < MAAGE_WORLD_LIMIT &&
           policy->spawn_search_step >= 1 && policy->spawn_search_step < MAAGE_WORLD_LIMIT;
}
