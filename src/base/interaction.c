#include "base_internal.h"
#include "maage/gameplay.h"
#include "maage/containers.h"
#include <string.h>
static maage_status use_status(unsigned char code) {
    return maage_status_make(code, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_QUERY, 0);
}
maage_status maage_base_table_use(maage_world *world, const maage_world_target *target, maage_crafting_container *container) {
    const maage_block_registry *blocks = maage_world_registry(world);
    const maage_base_ids *ids = maage_base_state_references(blocks, maage_block_registry_state(blocks, target->state_id));
    return maage_container_open(maage_block_registry_containers(blocks), container, ids->container_table, target->x, target->y, target->z, world);
}
maage_status maage_base_furnace_use(maage_world *world, const maage_world_target *target, maage_crafting_container *container) {
    maage_furnace_state furnace;
    const maage_block_registry *blocks = maage_world_registry(world);
    const maage_block_state_definition *above = maage_block_registry_state(blocks, maage_world_get(world, target->x, target->y + 1, target->z));
    const maage_block_definition *cover = above ? maage_block_registry_block(blocks, above->block_id) : NULL;
    maage_status result;
    if (cover && cover->opaque) return use_status(MAAGE_STATUS_NOOP);
    result = maage_world_furnace_get(world, target->x, target->y, target->z, &furnace);
    if (!maage_status_failed(result)) result = maage_container_open(maage_block_registry_containers(blocks), container, maage_base_state_references(blocks, maage_block_registry_state(blocks, target->state_id))->container_furnace, target->x, target->y, target->z, world);
    if (!maage_status_failed(result)) container->block_identity = furnace.identity;
    return result;
}
int maage_base_table_access(const maage_base_ids *ids, const maage_world *world, const maage_crafting_container *container) {
    const maage_block_registry *blocks = maage_world_registry(world);
    const maage_block_state_definition *state = maage_block_registry_state(blocks, maage_world_get(world, container->x, container->y, container->z));
    return state && state->block_id == ids->block_crafting_table;
}
int maage_base_furnace_access(const maage_world *world, const maage_crafting_container *container) {
    maage_furnace_state furnace;
    return !maage_status_failed(maage_world_furnace_get(world, container->x, container->y, container->z, &furnace)) && furnace.identity == container->block_identity;
}
maage_status maage_base_seed_placement(const maage_item_definition *item, const maage_world *world, const maage_world_target *target, maage_block_state_id *placed, int *in_place) {
    const maage_base_ids *ids = (const maage_base_ids *) item->behavior_context;
    const maage_block_state_definition *state = maage_block_registry_state(maage_world_registry(world), target->state_id);
    (void) placed;
    *in_place = 0;
    return use_status(target->face == MAAGE_FACE_POSITIVE_Y && maage_base_state_tag(ids, state, ids->tag_farmland) ? MAAGE_STATUS_SUCCESS : MAAGE_STATUS_NOOP);
}
maage_status maage_base_hoe_placement(const maage_item_definition *item, const maage_world *world, const maage_world_target *target, maage_block_state_id *placed, int *in_place) {
    const maage_base_ids *ids = (const maage_base_ids *) item->behavior_context;
    *in_place = 1;
    if (target->face == MAAGE_FACE_NEGATIVE_Y || target->y >= MAAGE_WORLD_HEIGHT - 1 || (target->block_id != ids->block_grass && target->block_id != ids->block_dirt) || !maage_world_chunk_loaded(world, maage_world_chunk_coordinate(target->x), maage_world_chunk_coordinate(target->z))) return use_status(MAAGE_STATUS_NOOP);
    if (maage_world_get(world, target->x, target->y + 1, target->z) != ids->state_air) return use_status(MAAGE_STATUS_NOOP);
    *placed = ids->farmland[0];
    return use_status(MAAGE_STATUS_SUCCESS);
}
maage_status maage_base_farmland_land(maage_world *world, int x, int y, int z, const maage_block_state_definition *state, double distance, unsigned long *random) {
    const maage_base_ids *ids = maage_base_state_references(maage_world_registry(world), state);
    if (distance <= 0.5) return use_status(MAAGE_STATUS_NOOP);
    *random = (*random * 1664525UL + 1013904223UL) & 4294967295UL;
    if ((*random >> 8) / 16777216.0 < distance - 0.5) return maage_world_set(world, x, y, z, state->id, ids->state_dirt);
    return use_status(MAAGE_STATUS_SUCCESS);
}
maage_status maage_base_leaf_import(const maage_block_registry *blocks, const maage_block_state_definition *state, maage_block_state_id *replacement) {
    *replacement = state->id;
    {
        const maage_base_ids *ids = maage_base_state_references(blocks, state);
        if (!maage_base_state_value(blocks, state, ids->property_persistent) && !maage_base_state_value(blocks, state, ids->property_check_decay)) return maage_block_state_change(blocks, state, ids->property_check_decay, 1, replacement);
    }
    return use_status(MAAGE_STATUS_NOOP);
}

maage_status maage_base_furnace_action(maage_world *world, maage_gameplay *game, unsigned long revision, unsigned int group, unsigned int slot, unsigned int action) {
    maage_furnace_state furnace;
    maage_gameplay next = *game;
    double experience = 0;
    maage_status result = maage_world_furnace_get(world, next.crafting.x, next.crafting.y, next.crafting.z, &furnace);
    if (!maage_status_failed(result)) result = maage_furnace_click(&furnace, &next.inventory, maage_world_item_registry(world), maage_world_smelting_registry(world), maage_world_fuel_registry(world), revision, group, slot, action, &experience);
    if (maage_status_code(result) == MAAGE_STATUS_SUCCESS) {
        result = maage_gameplay_add_experience(&next, experience);
        if (!maage_status_failed(result)) result = maage_world_furnace_set(world, next.crafting.x, next.crafting.y, next.crafting.z, &furnace);
        if (!maage_status_failed(result)) *game = next;
    }
    return result;
}
maage_status maage_base_furnace_sample(const maage_world *world, maage_crafting_container *container) {
    maage_furnace_state furnace;
    maage_status result = maage_world_furnace_get(world, container->x, container->y, container->z, &furnace);
    if (maage_status_failed(result)) return result;
    container->revision = furnace.revision;
    container->inputs[0] = furnace.slots[0];
    container->inputs[1] = furnace.slots[1];
    container->output = furnace.slots[2];
    container->active_remaining = furnace.burn;
    container->active_duration = furnace.duration;
    container->process = furnace.cook;
    container->process_duration = MAAGE_FURNACE_COOK_TICKS;
    return result;
}
