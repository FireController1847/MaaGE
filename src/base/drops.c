#include "base_internal.h"
static unsigned long drop_random(unsigned long *random, unsigned long range) {
    *random = (*random * 1664525UL + 1013904223UL) & 4294967295UL;
    return (*random >> 8) % range;
}

maage_status maage_base_drop_wheat(const maage_block_registry *blocks, const maage_item_registry *items, const maage_block_state_definition *state, const maage_item_definition *tool, unsigned long *random, maage_item_drops *drops) {
    const maage_base_ids *ids = maage_base_state_references(blocks, state);
    const maage_block_definition *block = maage_block_registry_block(blocks, state->block_id);
    maage_item_stack *drop = &drops->stacks[0];
    (void) items;
    (void) ids;
    (void) tool;
    (void) random;
    drop->item_id = block->drop_item_id;
    drop->count = block->drop_count;
    drop->item_id = maage_base_state_value(blocks, state, ids->property_crop_age) == 7 ? ids->item_wheat : ids->item_seeds;
    drop->count = 1;
    if (maage_base_state_value(blocks, state, ids->property_crop_age) == 7) {
        unsigned int i;
        drops->stacks[1].item_id = ids->item_seeds;
        for (i = 0; i < 3; ++i)
            if (drop_random(random, 15) <= maage_base_state_value(blocks, state, ids->property_crop_age)) ++drops->stacks[1].count;
    }
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_RESOLVE, 0);
}

maage_status maage_base_drop_redstone(const maage_block_registry *blocks, const maage_item_registry *items, const maage_block_state_definition *state, const maage_item_definition *tool, unsigned long *random, maage_item_drops *drops) {
    const maage_base_ids *ids = maage_base_state_references(blocks, state);
    const maage_block_definition *block = maage_block_registry_block(blocks, state->block_id);
    maage_item_stack *drop = &drops->stacks[0];
    (void) items;
    (void) ids;
    (void) tool;
    (void) random;
    drop->item_id = block->drop_item_id;
    drop->count = block->drop_count;
    drop->item_id = ids->item_redstone;
    drop->count = 4 + (unsigned int) drop_random(random, 2);
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_RESOLVE, 0);
}

maage_status maage_base_drop_lapis(const maage_block_registry *blocks, const maage_item_registry *items, const maage_block_state_definition *state, const maage_item_definition *tool, unsigned long *random, maage_item_drops *drops) {
    const maage_base_ids *ids = maage_base_state_references(blocks, state);
    const maage_block_definition *block = maage_block_registry_block(blocks, state->block_id);
    maage_item_stack *drop = &drops->stacks[0];
    (void) items;
    (void) ids;
    (void) tool;
    (void) random;
    drop->item_id = block->drop_item_id;
    drop->count = block->drop_count;
    drop->item_id = ids->item_lapis;
    drop->count = 4 + (unsigned int) drop_random(random, 5);
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_RESOLVE, 0);
}

maage_status maage_base_drop_gravel(const maage_block_registry *blocks, const maage_item_registry *items, const maage_block_state_definition *state, const maage_item_definition *tool, unsigned long *random, maage_item_drops *drops) {
    const maage_base_ids *ids = maage_base_state_references(blocks, state);
    const maage_block_definition *block = maage_block_registry_block(blocks, state->block_id);
    maage_item_stack *drop = &drops->stacks[0];
    (void) items;
    (void) ids;
    (void) tool;
    (void) random;
    drop->item_id = block->drop_item_id;
    drop->count = block->drop_count;
    if (!drop_random(random, 10)) drop->item_id = ids->item_flint;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_RESOLVE, 0);
}

maage_status maage_base_drop_species(const maage_block_registry *blocks, const maage_item_registry *items, const maage_block_state_definition *state, const maage_item_definition *tool, unsigned long *random, maage_item_drops *drops) {
    const maage_base_ids *ids = maage_base_state_references(blocks, state);
    const maage_block_definition *block = maage_block_registry_block(blocks, state->block_id);
    maage_item_stack *drop = &drops->stacks[0];
    (void) items;
    (void) ids;
    (void) tool;
    (void) random;
    drop->item_id = block->drop_item_id;
    drop->count = block->drop_count;
    return maage_item_variant_change(items, drop->item_id, 0, ids->property_wood, maage_base_state_value(blocks, state, ids->property_wood), &drop->variant);
}

maage_status maage_base_drop_planks(const maage_block_registry *blocks, const maage_item_registry *items, const maage_block_state_definition *state, const maage_item_definition *tool, unsigned long *random, maage_item_drops *drops) {
    const maage_base_ids *ids = maage_base_state_references(blocks, state);
    const maage_block_definition *block = maage_block_registry_block(blocks, state->block_id);
    maage_item_stack *drop = &drops->stacks[0];
    (void) items;
    (void) ids;
    (void) tool;
    (void) random;
    drop->item_id = block->drop_item_id;
    drop->count = block->drop_count;
    return maage_item_variant_change(items, drop->item_id, 0, ids->property_wood, maage_base_state_value(blocks, state, ids->property_wood), &drop->variant);
}

maage_status maage_base_drop_leaves(const maage_block_registry *blocks, const maage_item_registry *items, const maage_block_state_definition *state, const maage_item_definition *tool, unsigned long *random, maage_item_drops *drops) {
    const maage_base_ids *ids = maage_base_state_references(blocks, state);
    const maage_block_definition *block = maage_block_registry_block(blocks, state->block_id);
    maage_item_stack *drop = &drops->stacks[0];
    (void) items;
    (void) ids;
    (void) tool;
    (void) random;
    drop->item_id = block->drop_item_id;
    drop->count = block->drop_count;
    if (tool && maage_traits_has(ids->traits, MAAGE_TRAIT_ITEM, tool->id, ids->tool_tags[MAAGE_TOOL_SHEARS])) {
        drop->item_id = ids->item_leaves;
        drop->count = 1;
        drop->variant = 0;
    } else {
        drop->item_id = ids->item_sapling;
        drop->variant = 0;
        drop->count = drop_random(random, 20) == 0 ? 1U : 0U;
        if (maage_base_state_value(blocks, state, ids->property_wood) == ids->wood_values[0] && !drop_random(random, 200)) {
            drops->stacks[1].item_id = ids->item_apple;
            drops->stacks[1].count = 1;
        }
    }
    return maage_item_variant_change(items, drop->item_id, 0, ids->property_wood, maage_base_state_value(blocks, state, ids->property_wood), &drop->variant);
}

maage_status maage_base_drop_grass(const maage_block_registry *blocks, const maage_item_registry *items, const maage_block_state_definition *state, const maage_item_definition *tool, unsigned long *random, maage_item_drops *drops) {
    const maage_base_ids *ids = maage_base_state_references(blocks, state);
    const maage_block_definition *block = maage_block_registry_block(blocks, state->block_id);
    maage_item_stack *drop = &drops->stacks[0];
    (void) items;
    (void) ids;
    (void) tool;
    (void) random;
    drop->item_id = block->drop_item_id;
    drop->count = block->drop_count;
    drop->item_id = tool && maage_traits_has(ids->traits, MAAGE_TRAIT_ITEM, tool->id, ids->tool_tags[MAAGE_TOOL_SHEARS]) ? ids->item_tall_grass : ids->item_seeds;
    drop->count = drop->item_id == ids->item_tall_grass || !drop_random(random, 8) ? 1U : 0U;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_RESOLVE, 0);
}

maage_status maage_base_drop_bush(const maage_block_registry *blocks, const maage_item_registry *items, const maage_block_state_definition *state, const maage_item_definition *tool, unsigned long *random, maage_item_drops *drops) {
    const maage_base_ids *ids = maage_base_state_references(blocks, state);
    const maage_block_definition *block = maage_block_registry_block(blocks, state->block_id);
    maage_item_stack *drop = &drops->stacks[0];
    (void) items;
    (void) ids;
    (void) tool;
    (void) random;
    drop->item_id = block->drop_item_id;
    drop->count = block->drop_count;
    drop->count = tool && maage_traits_has(ids->traits, MAAGE_TRAIT_ITEM, tool->id, ids->tool_tags[MAAGE_TOOL_SHEARS]) ? 1U : 0U;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_RESOLVE, 0);
}

maage_status maage_base_drop_sand(const maage_block_registry *blocks, const maage_item_registry *items, const maage_block_state_definition *state, const maage_item_definition *tool, unsigned long *random, maage_item_drops *drops) {
    const maage_base_ids *ids = maage_base_state_references(blocks, state);
    const maage_block_definition *block = maage_block_registry_block(blocks, state->block_id);
    (void) tool;
    (void) random;
    drops->stacks[0].item_id = block->drop_item_id;
    drops->stacks[0].count = block->drop_count;
    return maage_item_variant_change(items, block->drop_item_id, 0, ids->property_sand, maage_base_state_value(blocks, state, ids->property_sand), &drops->stacks[0].variant);
}
