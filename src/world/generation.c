#include "world_internal.h"
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
