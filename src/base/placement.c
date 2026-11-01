#include "base_internal.h"
static maage_status placement_status(unsigned char code) {
    return maage_status_make(code, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_RESOLVE, 0);
}

maage_status maage_base_place_sand(const maage_item_definition *item, unsigned int metadata, maage_block_face face, double yaw, maage_block_state_id *state) {
    const maage_base_ids *ids = (const maage_base_ids *) item->behavior_context;
    (void) yaw;
    (void) face;
    if (metadata > item->max_metadata) return placement_status(MAAGE_STATUS_INVALID_ARGUMENT);
    *state = metadata ? ids->state_red_sand : ids->state_sand;
    return placement_status(MAAGE_STATUS_SUCCESS);
}

maage_status maage_base_place_planks(const maage_item_definition *item, unsigned int metadata, maage_block_face face, double yaw, maage_block_state_id *state) {
    const maage_base_ids *ids = (const maage_base_ids *) item->behavior_context;
    (void) yaw;
    (void) face;
    if (metadata > item->max_metadata) return placement_status(MAAGE_STATUS_INVALID_ARGUMENT);
    *state = ids->planks[metadata];
    return placement_status(MAAGE_STATUS_SUCCESS);
}

maage_status maage_base_place_log(const maage_item_definition *item, unsigned int metadata, maage_block_face face, double yaw, maage_block_state_id *state) {
    const maage_base_ids *ids = (const maage_base_ids *) item->behavior_context;
    (void) yaw;
    (void) face;
    if (metadata > item->max_metadata) return placement_status(MAAGE_STATUS_INVALID_ARGUMENT);
    *state = ids->logs[(face / 2 == 0 ? 4U : (face / 2 == 2 ? 8U : 0U)) + metadata];
    return placement_status(MAAGE_STATUS_SUCCESS);
}

maage_status maage_base_place_log2(const maage_item_definition *item, unsigned int metadata, maage_block_face face, double yaw, maage_block_state_id *state) {
    const maage_base_ids *ids = (const maage_base_ids *) item->behavior_context;
    (void) yaw;
    (void) face;
    if (metadata > item->max_metadata) return placement_status(MAAGE_STATUS_INVALID_ARGUMENT);
    *state = ids->logs2[(face / 2 == 0 ? 4U : (face / 2 == 2 ? 8U : 0U)) + metadata];
    return placement_status(MAAGE_STATUS_SUCCESS);
}

maage_status maage_base_place_leaves(const maage_item_definition *item, unsigned int metadata, maage_block_face face, double yaw, maage_block_state_id *state) {
    const maage_base_ids *ids = (const maage_base_ids *) item->behavior_context;
    (void) yaw;
    (void) face;
    if (metadata > item->max_metadata) return placement_status(MAAGE_STATUS_INVALID_ARGUMENT);
    *state = ids->leaves[metadata | MAAGE_LEAF_PERSISTENT];
    return placement_status(MAAGE_STATUS_SUCCESS);
}

maage_status maage_base_place_sapling(const maage_item_definition *item, unsigned int metadata, maage_block_face face, double yaw, maage_block_state_id *state) {
    const maage_base_ids *ids = (const maage_base_ids *) item->behavior_context;
    (void) yaw;
    (void) face;
    if (metadata > item->max_metadata) return placement_status(MAAGE_STATUS_INVALID_ARGUMENT);
    *state = ids->saplings[metadata];
    return placement_status(MAAGE_STATUS_SUCCESS);
}

maage_status maage_base_place_torch(const maage_item_definition *item, unsigned int metadata, maage_block_face face, double yaw, maage_block_state_id *state) {
    const maage_base_ids *ids = (const maage_base_ids *) item->behavior_context;
    (void) yaw;
    (void) face;
    if (metadata > item->max_metadata) return placement_status(MAAGE_STATUS_INVALID_ARGUMENT);
    if (face == MAAGE_FACE_NEGATIVE_Y) return placement_status(MAAGE_STATUS_NOOP);
    *state = ids->torches[face == MAAGE_FACE_NEGATIVE_X ? 2 : (face == MAAGE_FACE_POSITIVE_X ? 1 : (face == MAAGE_FACE_POSITIVE_Y ? 5 : (face == MAAGE_FACE_NEGATIVE_Z ? 4 : 3)))];
    return placement_status(MAAGE_STATUS_SUCCESS);
}
