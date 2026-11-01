#include "base_internal.h"
maage_status maage_block_leaves_state(const maage_block_registry *registry, unsigned int metadata, maage_block_state_id *state) {
    const maage_base_ids *ids = maage_base_references(registry);
    unsigned int species = metadata & MAAGE_LEAF_SPECIES_MASK;
    if (state) *state = MAAGE_CONTENT_ID_INVALID;
    if (!ids || !state || metadata > 15 || species > 2) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_RESOLVE, 0);
    if (!maage_block_registry_state(registry, ids->leaves[metadata])) return maage_status_make(MAAGE_STATUS_NOT_FOUND, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_RESOLVE, 0);
    *state = ids->leaves[metadata];
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_RESOLVE, 0);
}
maage_status maage_block_sapling_state(const maage_block_registry *registry, unsigned int metadata, maage_block_state_id *state) {
    const maage_base_ids *ids = maage_base_references(registry);
    unsigned int species = metadata & 7U;
    if (state) *state = MAAGE_CONTENT_ID_INVALID;
    if (!ids || !state || (metadata & ~15U) || species > 2) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_RESOLVE, 0);
    if (!maage_block_registry_state(registry, ids->saplings[metadata])) return maage_status_make(MAAGE_STATUS_NOT_FOUND, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_RESOLVE, 0);
    *state = ids->saplings[metadata];
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_RESOLVE, 0);
}
