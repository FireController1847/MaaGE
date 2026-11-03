#include "base_internal.h"

static maage_status placement_status(unsigned char code) {
    return maage_status_make(code, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_RESOLVE, 0);
}
static const maage_item_variant_definition *placement_variant(const maage_item_definition *item, maage_item_variant_id variant) {
    return item && variant < item->variant_count ? &item->variants[variant] : NULL;
}
static maage_status base_place_variant(const maage_item_definition *item, maage_item_variant_id variant, maage_block_state_id *state) {
    const maage_item_variant_definition *definition = placement_variant(item, variant);
    if (!definition || !state) return placement_status(MAAGE_STATUS_INVALID_ARGUMENT);
    *state = definition->placement_state_id;
    return placement_status(MAAGE_STATUS_SUCCESS);
}
maage_status maage_base_place_sand(const maage_item_definition *item, maage_item_variant_id variant, maage_block_face face, double yaw, maage_block_state_id *state) {
    (void) face;
    (void) yaw;
    return base_place_variant(item, variant, state);
}
maage_status maage_base_place_planks(const maage_item_definition *item, maage_item_variant_id variant, maage_block_face face, double yaw, maage_block_state_id *state) {
    (void) face;
    (void) yaw;
    return base_place_variant(item, variant, state);
}
maage_status maage_base_place_log(const maage_item_definition *item, maage_item_variant_id variant, maage_block_face face, double yaw, maage_block_state_id *state) {
    const maage_base_ids *ids = (const maage_base_ids *) item->behavior_context;
    const maage_item_variant_definition *definition = placement_variant(item, variant);
    const maage_block_state_definition *source;
    (void) yaw;
    if (!definition) return placement_status(MAAGE_STATUS_INVALID_ARGUMENT);
    source = maage_block_registry_state(ids->blocks, definition->placement_state_id);
    return maage_block_state_change(ids->blocks, source, ids->property_axis, face / 2 == 0 ? 1 : (face / 2 == 2 ? 2 : 0), state);
}
maage_status maage_base_place_log2(const maage_item_definition *item, maage_item_variant_id variant, maage_block_face face, double yaw, maage_block_state_id *state) {
    return maage_base_place_log(item, variant, face, yaw, state);
}
maage_status maage_base_place_leaves(const maage_item_definition *item, maage_item_variant_id variant, maage_block_face face, double yaw, maage_block_state_id *state) {
    const maage_base_ids *ids = (const maage_base_ids *) item->behavior_context;
    const maage_item_variant_definition *definition = placement_variant(item, variant);
    (void) face;
    (void) yaw;
    if (!definition) return placement_status(MAAGE_STATUS_INVALID_ARGUMENT);
    return maage_block_state_change(ids->blocks, maage_block_registry_state(ids->blocks, definition->placement_state_id), ids->property_persistent, 1, state);
}
maage_status maage_base_place_sapling(const maage_item_definition *item, maage_item_variant_id variant, maage_block_face face, double yaw, maage_block_state_id *state) {
    (void) face;
    (void) yaw;
    return base_place_variant(item, variant, state);
}
maage_status maage_base_place_torch(const maage_item_definition *item, maage_item_variant_id variant, maage_block_face face, double yaw, maage_block_state_id *state) {
    const maage_base_ids *ids = (const maage_base_ids *) item->behavior_context;
    const maage_item_variant_definition *definition = placement_variant(item, variant);
    maage_property_value attachment;
    (void) yaw;
    if (!definition) return placement_status(MAAGE_STATUS_INVALID_ARGUMENT);
    if (face == MAAGE_FACE_NEGATIVE_Y) return placement_status(MAAGE_STATUS_NOOP);
    attachment = face == MAAGE_FACE_NEGATIVE_X ? 2 : (face == MAAGE_FACE_POSITIVE_X ? 1 : (face == MAAGE_FACE_POSITIVE_Y ? 5 : (face == MAAGE_FACE_NEGATIVE_Z ? 4 : 3)));
    return maage_block_state_change(ids->blocks, maage_block_registry_state(ids->blocks, definition->placement_state_id), ids->property_attachment, attachment, state);
}
