#include "base_internal.h"
#include "maage/block_entity.h"
#include <string.h>
static maage_status entity_status(unsigned char code) {
    return maage_status_make(code, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_UPDATE, 0);
}
static int furnace_entity_valid(const maage_block_entity_handler *handler, const maage_block_state_definition *block, const maage_item_registry *items, const void *data) {
    const maage_base_ids *ids = (const maage_base_ids *) handler->context;
    const maage_furnace_state *state = (const maage_furnace_state *) data;
    return block && (block->block_id == ids->block_furnace || block->block_id == ids->block_lit_furnace) && block->metadata < 4 && maage_furnace_valid(state, items) && (state->burn != 0) == (block->block_id == ids->block_lit_furnace);
}
static maage_status furnace_entity_initialize(const maage_block_entity_handler *handler, const maage_block_state_definition *block, void *data, unsigned long identity, int loaded) {
    const maage_base_ids *ids = (const maage_base_ids *) handler->context;
    maage_furnace_state *state = (maage_furnace_state *) data;
    if (!loaded && block->block_id != ids->block_furnace) return entity_status(MAAGE_STATUS_INVALID_STATE);
    state->identity = identity;
    if (!loaded) state->revision = 1;
    return entity_status(MAAGE_STATUS_SUCCESS);
}
static maage_status furnace_entity_tick(const maage_block_entity_handler *handler, maage_world *world, int x, int y, int z, const maage_block_state_definition *block, void *data, maage_block_state_id *replacement) {
    const maage_base_ids *ids = (const maage_base_ids *) handler->context;
    maage_furnace_state *state = (maage_furnace_state *) data;
    maage_status result;
    (void) x;
    (void) y;
    (void) z;
    result = maage_furnace_tick(state, maage_world_item_registry(world), maage_world_smelting_registry(world), maage_world_fuel_registry(world));
    if (!maage_status_failed(result)) *replacement = ids->furnaces[state->burn ? 1 : 0][block->metadata];
    return result;
}
static int furnace_entity_commit(const void *before, const void *after) {
    const maage_furnace_state *a = (const maage_furnace_state *) before, *b = (const maage_furnace_state *) after;
    return b->identity == a->identity && b->burn == a->burn && b->duration == a->duration && b->cook == a->cook && a->revision < 4294967295UL && b->revision == a->revision + 1UL;
}
static const maage_item_stack *furnace_entity_drop(const void *data, size_t slot) {
    return slot < 3 ? &((const maage_furnace_state *) data)->slots[slot] : NULL;
}
static maage_status furnace_entity_codec(maage_furnace_state *state, maage_entity_codec *codec) {
    maage_status result;
    unsigned long cook;
    size_t i;
    for (i = 0; i < 3; ++i) {
        result = codec->stack(codec->context, &state->slots[i]);
        if (maage_status_failed(result)) return result;
    }
    result = codec->word(codec->context, &state->burn, 4);
    if (!maage_status_failed(result)) result = codec->word(codec->context, &state->duration, 4);
    cook = state->cook;
    if (!maage_status_failed(result)) result = codec->word(codec->context, &cook, 4);
    if (maage_status_failed(result)) return result;
    if (cook >= MAAGE_FURNACE_COOK_TICKS) return entity_status(MAAGE_STATUS_MALFORMED_INPUT);
    state->cook = (unsigned int) cook;
    return codec->word(codec->context, &state->revision, 4);
}
static maage_status furnace_entity_encode(const void *data, maage_entity_codec *codec) {
    maage_furnace_state copy = *(const maage_furnace_state *) data;
    return furnace_entity_codec(&copy, codec);
}
static maage_status furnace_entity_decode(void *data, maage_entity_codec *codec) {
    return furnace_entity_codec((maage_furnace_state *) data, codec);
}
maage_status maage_base_furnace_entity(maage_content_context *context, const maage_base_ids *ids) {
    maage_block_entity_handler handler;
    const maage_block_entity_handler *registered;
    maage_block_definition block;
    maage_status result;
    memset(&handler, 0, sizeof(handler));
    handler.version = MAAGE_FURNACE_VERSION;
    handler.size = sizeof(maage_furnace_state);
    handler.encoded_size = 55;
    handler.drop_slots = 3;
    handler.snapshot_ticks = 100;
    handler.context = ids;
    handler.valid = furnace_entity_valid;
    handler.initialize = furnace_entity_initialize;
    handler.tick = furnace_entity_tick;
    handler.can_commit = furnace_entity_commit;
    handler.drop = furnace_entity_drop;
    handler.encode = furnace_entity_encode;
    handler.decode = furnace_entity_decode;
    result = maage_block_registry_register_entity(context->blocks, "minecraft:furnace", &handler, &registered);
    if (maage_status_failed(result)) return result;
    block = *maage_block_registry_block(context->blocks, ids->block_furnace);
    block.entity = registered;
    result = maage_block_registry_update_block(context->blocks, &block);
    if (maage_status_failed(result)) return result;
    block = *maage_block_registry_block(context->blocks, ids->block_lit_furnace);
    block.entity = registered;
    return maage_block_registry_update_block(context->blocks, &block);
}
static unsigned long furnace_type(const maage_world *world, int x, int y, int z) {
    const maage_block_registry *blocks = maage_world_registry(world);
    const maage_block_state_definition *state = maage_block_registry_state(blocks, maage_world_get(world, x, y, z));
    const maage_block_definition *block = state ? maage_block_registry_block(blocks, state->block_id) : NULL;
    return block && block->use == maage_base_furnace_use && block->entity ? block->entity->id : 0;
}
maage_status maage_world_furnace_get(const maage_world *world, int x, int y, int z, maage_furnace_state *state) {
    return maage_world_entity_read(world, x, y, z, furnace_type(world, x, y, z), state, sizeof(*state));
}
maage_status maage_world_furnace_set(maage_world *world, int x, int y, int z, const maage_furnace_state *state) {
    return maage_world_entity_commit(world, x, y, z, furnace_type(world, x, y, z), state, sizeof(*state));
}
