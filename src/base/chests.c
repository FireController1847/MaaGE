#include "base_internal.h"
#include "maage/block_entity.h"
#include "maage/containers.h"
#include "maage/gameplay.h"
#include <math.h>
#include <string.h>

#define BASE_CHEST_SLOTS 27U
typedef struct base_chest_state {
    maage_item_stack slots[BASE_CHEST_SLOTS];
    unsigned long identity, revision, viewer;
    unsigned int lid, previous_lid;
} base_chest_state;
typedef struct base_chest_group {
    int x[2], y, z[2];
    unsigned int count;
    const maage_block_state_definition *blocks[2];
    base_chest_state states[2];
} base_chest_group;
static const int chest_dx[4] = {-1, 1, 0, 0}, chest_dz[4] = {0, 0, -1, 1};
static maage_status chest_status(unsigned char code, unsigned char operation) {
    return maage_status_make(code, MAAGE_CONTEXT_WORLD, operation, 0);
}
static const maage_block_state_definition *chest_block(const maage_world *world, int x, int y, int z) {
    if (!maage_world_chunk_loaded(world, maage_world_chunk_coordinate(x), maage_world_chunk_coordinate(z))) return NULL;
    return maage_block_registry_state(maage_world_registry(world), maage_world_get(world, x, y, z));
}
static maage_status chest_group(const maage_world *world, int x, int y, int z, int strict, base_chest_group *group) {
    const maage_block_state_definition *state = chest_block(world, x, y, z), *adjacent;
    unsigned int side;
    memset(group, 0, sizeof(*group));
    if (!state) return chest_status(MAAGE_STATUS_NOT_FOUND, MAAGE_OPERATION_QUERY);
    group->count = 1;
    group->x[0] = x;
    group->z[0] = z;
    group->y = y;
    group->blocks[0] = state;
    for (side = 0; side < 4; ++side) {
        adjacent = chest_block(world, x + chest_dx[side], y, z + chest_dz[side]);
        if (!adjacent && strict) return chest_status(MAAGE_STATUS_NOT_FOUND, MAAGE_OPERATION_QUERY);
        if (!adjacent || adjacent->block_id != state->block_id) continue;
        if (group->count == 2) return chest_status(MAAGE_STATUS_INVALID_STATE, MAAGE_OPERATION_QUERY);
        group->x[1] = x + chest_dx[side];
        group->z[1] = z + chest_dz[side];
        group->blocks[1] = adjacent;
        group->count = 2;
    }
    if (group->count == 2 && (group->x[1] < x || group->z[1] < z)) {
        group->x[0] = group->x[1];
        group->z[0] = group->z[1];
        group->blocks[0] = group->blocks[1];
        group->x[1] = x;
        group->z[1] = z;
        group->blocks[1] = state;
    }
    return chest_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_QUERY);
}
static unsigned long chest_type(const maage_world *world, const maage_block_state_definition *state) {
    const maage_block_definition *block = state ? maage_block_registry_block(maage_world_registry(world), state->block_id) : NULL;
    const maage_base_ids *ids = maage_base_state_references(maage_world_registry(world), state);
    return block && ids && (block->id == ids->block_chest || block->id == ids->block_trapped_chest) && block->entity ? block->entity->id : 0;
}
static maage_status chest_read(const maage_world *world, base_chest_group *group) {
    unsigned int half;
    maage_status result;
    for (half = 0; half < group->count; ++half) {
        result = maage_world_entity_read(world, group->x[half], group->y, group->z[half], chest_type(world, group->blocks[half]), &group->states[half], sizeof(group->states[half]));
        if (maage_status_failed(result)) return result;
    }
    return chest_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_READ);
}
static maage_status chest_commit(maage_world *world, base_chest_group *group) {
    maage_block_entity_update updates[2];
    unsigned int half;
    for (half = 0; half < group->count; ++half) {
        updates[half].x = group->x[half];
        updates[half].y = group->y;
        updates[half].z = group->z[half];
        updates[half].type = chest_type(world, group->blocks[half]);
        updates[half].state = &group->states[half];
        updates[half].size = sizeof(group->states[half]);
    }
    return maage_world_entities_commit(world, updates, group->count);
}
static unsigned long chest_revision(const base_chest_group *group) {
    unsigned long revision = group->states[0].revision;
    if (group->count == 2 && group->states[1].revision > revision) revision = group->states[1].revision;
    return revision;
}
static int chest_clear_above(const maage_world *world, const base_chest_group *group) {
    const maage_block_state_definition *state;
    const maage_block_definition *block;
    unsigned int half;
    if (group->y == MAAGE_WORLD_HEIGHT - 1) return 1;
    for (half = 0; half < group->count; ++half) {
        state = chest_block(world, group->x[half], group->y + 1, group->z[half]);
        block = state ? maage_block_registry_block(maage_world_registry(world), state->block_id) : NULL;
        if (!block || block->opaque) return 0;
    }
    return 1;
}
maage_status maage_base_chest_can_place(const maage_world *world, int x, int y, int z, const maage_block_state_definition *state, int *valid) {
    const maage_block_state_definition *neighbor, *other;
    unsigned int side, adjacent, count = 0;
    *valid = 0;
    for (side = 0; side < 4; ++side) {
        neighbor = chest_block(world, x + chest_dx[side], y, z + chest_dz[side]);
        if (!neighbor) return chest_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_QUERY);
        if (neighbor->block_id != state->block_id) continue;
        if (++count > 1) return chest_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_QUERY);
        for (adjacent = 0; adjacent < 4; ++adjacent) {
            int nx = x + chest_dx[side] + chest_dx[adjacent], nz = z + chest_dz[side] + chest_dz[adjacent];
            if (nx == x && nz == z) continue;
            other = chest_block(world, nx, y, nz);
            if (!other) return chest_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_QUERY);
            if (other->block_id == state->block_id) return chest_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_QUERY);
        }
    }
    *valid = 1;
    return chest_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_QUERY);
}
maage_status maage_base_chest_placement(const maage_item_definition *item, maage_item_variant_id variant, maage_block_face face, double yaw, maage_block_state_id *state) {
    const maage_base_ids *ids = (const maage_base_ids *) item->behavior_context;
    unsigned int direction = (unsigned int) ((long) floor(yaw * 2 / 3.141592653589793 + 0.5) & 3L);
    (void) face;
    if (variant) return chest_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_QUERY);
    *state = ids->chest_states[item->block_id == ids->block_trapped_chest][direction];
    return chest_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_QUERY);
}
maage_status maage_base_chest_use(maage_world *world, const maage_world_target *target, maage_crafting_container *container) {
    const maage_block_registry *blocks = maage_world_registry(world);
    const maage_block_state_definition *state = maage_block_registry_state(blocks, target->state_id);
    const maage_base_ids *ids = maage_base_state_references(blocks, state);
    base_chest_group group;
    maage_status result = chest_group(world, target->x, target->y, target->z, 1, &group);
    if (maage_status_failed(result)) return maage_status_code(result) == MAAGE_STATUS_NOT_FOUND ? chest_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_OPEN) : result;
    if (!chest_clear_above(world, &group)) return chest_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_OPEN);
    return maage_container_open(maage_block_registry_containers(blocks), container, ids->container_chest[group.count - 1], group.x[0], group.y, group.z[0], world);
}
maage_status maage_base_chest_opened(const void *context, maage_world *world, maage_crafting_container *container) {
    const maage_base_ids *ids = (const maage_base_ids *) context;
    base_chest_group group;
    maage_status result = chest_group(world, container->x, container->y, container->z, 1, &group);
    unsigned int half;
    unsigned long revision;
    if (!maage_status_failed(result)) result = chest_read(world, &group);
    if (maage_status_failed(result)) return result;
    if (container->kind != ids->container_chest[group.count - 1] || !chest_clear_above(world, &group)) return chest_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_OPEN);
    revision = chest_revision(&group);
    if (revision >= 4294967295UL - 2) return chest_status(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_OPERATION_OPEN);
    for (half = 0; half < group.count; ++half) {
        if (group.states[half].viewer) return chest_status(MAAGE_STATUS_INVALID_STATE, MAAGE_OPERATION_OPEN);
        group.states[half].viewer = container->identity;
        group.states[half].revision = revision + 1;
    }
    result = chest_commit(world, &group);
    if (maage_status_failed(result)) return result;
    container->block_identity = group.states[0].identity;
    container->revision = revision + 1;
    if (group.count == 2) {
        container->secondary_block_identity = group.states[1].identity;
        container->secondary_x = group.x[1];
        container->secondary_y = group.y;
        container->secondary_z = group.z[1];
    }
    return chest_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_OPEN);
}
static int chest_matches(const base_chest_group *group, const maage_crafting_container *container) {
    return group->x[0] == container->x && group->z[0] == container->z && group->states[0].identity == container->block_identity && group->states[0].viewer == container->identity && (group->count == 2 ? container->secondary_block_identity == group->states[1].identity && container->secondary_x == group->x[1] && container->secondary_y == group->y && container->secondary_z == group->z[1] && group->states[1].viewer == container->identity : !container->secondary_block_identity);
}
int maage_base_chest_access(const void *context, const maage_world *world, const maage_crafting_container *container) {
    const maage_base_ids *ids = (const maage_base_ids *) context;
    base_chest_group group;
    maage_status result = chest_group(world, container->x, container->y, container->z, 1, &group);
    if (!maage_status_failed(result)) result = chest_read(world, &group);
    return !maage_status_failed(result) && container->kind == ids->container_chest[group.count - 1] && chest_matches(&group, container) && chest_clear_above(world, &group);
}
maage_status maage_base_chest_closed(const void *context, maage_world *world, maage_gameplay *game, const maage_player *player) {
    base_chest_group group;
    const maage_crafting_container *container = &game->crafting;
    const maage_block_state_definition *block;
    base_chest_state state;
    maage_status result;
    unsigned int half;
    unsigned long identity;
    int x, y, z;
    (void) context;
    (void) player;
    memset(&group, 0, sizeof(group));
    group.y = container->y;
    /* Release either surviving original half. Retried cursor-return failure
     * cannot close a replacement chest or transfer backing inventory ownership. */
    for (half = 0; half < (container->secondary_block_identity ? 2U : 1U); ++half) {
        x = half ? container->secondary_x : container->x;
        y = half ? container->secondary_y : container->y;
        z = half ? container->secondary_z : container->z;
        identity = half ? container->secondary_block_identity : container->block_identity;
        block = chest_block(world, x, y, z);
        if (!chest_type(world, block)) continue;
        result = maage_world_entity_read(world, x, y, z, chest_type(world, block), &state, sizeof(state));
        if (maage_status_failed(result)) return result;
        if (state.identity != identity || state.viewer != container->identity) continue;
        if (state.revision == 4294967295UL) return chest_status(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_OPERATION_CLOSE);
        state.viewer = 0;
        ++state.revision;
        group.x[group.count] = x;
        group.z[group.count] = z;
        group.blocks[group.count] = block;
        group.states[group.count++] = state;
    }
    return chest_commit(world, &group);
}
maage_status maage_base_chest_sample(const void *context, const maage_world *world, maage_crafting_container *container) {
    base_chest_group group;
    maage_status result = chest_group(world, container->x, container->y, container->z, 1, &group);
    unsigned int i, slot;
    (void) context;
    if (!maage_status_failed(result)) result = chest_read(world, &group);
    if (maage_status_failed(result)) return result;
    if (!chest_matches(&group, container)) return chest_status(MAAGE_STATUS_INVALID_STATE, MAAGE_OPERATION_SNAPSHOT);
    container->revision = chest_revision(&group);
    for (i = 0; i < container->view_count; ++i) {
        slot = (unsigned int) container->view_first + i;
        if (slot >= group.count * BASE_CHEST_SLOTS) return chest_status(MAAGE_STATUS_INVALID_STATE, MAAGE_OPERATION_SNAPSHOT);
        container->view[i] = group.states[slot / BASE_CHEST_SLOTS].slots[slot % BASE_CHEST_SLOTS];
    }
    return chest_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_SNAPSHOT);
}
static void chest_merge(maage_item_stack *source, maage_item_stack *destination, const maage_item_registry *items) {
    const maage_item_definition *item;
    unsigned int amount;
    if (!source->count || (destination->count && (source->item_id != destination->item_id || source->variant != destination->variant || source->damage != destination->damage))) return;
    item = maage_item_registry_get(items, source->item_id);
    amount = item->max_stack - destination->count;
    if (amount > source->count) amount = source->count;
    if (!destination->count) {
        *destination = *source;
        destination->count = 0;
    }
    destination->count += amount;
    source->count -= amount;
    if (!source->count) memset(source, 0, sizeof(*source));
}
maage_status maage_base_chest_action(const void *context, maage_world *world, maage_gameplay *game, unsigned long revision, unsigned int group_id, unsigned int slot, unsigned int action) {
    const maage_item_registry *items = maage_world_item_registry(world);
    base_chest_group group;
    maage_inventory inventory = game->inventory;
    maage_item_stack *source, *destination;
    unsigned int half, pass, i, count;
    unsigned long next_revision;
    maage_status result = chest_group(world, game->crafting.x, game->crafting.y, game->crafting.z, 1, &group);
    (void) context;
    (void) revision; /* Ordinary slots follow reliable FIFO order, like player slots. */
    if (!maage_status_failed(result)) result = chest_read(world, &group);
    if (maage_status_failed(result)) return result;
    if (!chest_matches(&group, &game->crafting) || (group_id != MAAGE_SLOT_INVENTORY && group_id != MAAGE_SLOT_CRAFT_INPUT) || slot >= (group_id == MAAGE_SLOT_INVENTORY ? MAAGE_INVENTORY_SLOTS : group.count * BASE_CHEST_SLOTS)) return chest_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_MUTATE);
    next_revision = chest_revision(&group);
    if (next_revision >= 4294967295UL - 2) return chest_status(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_OPERATION_MUTATE);
    source = group_id == MAAGE_SLOT_INVENTORY ? &inventory.slots[slot] : &group.states[slot / BASE_CHEST_SLOTS].slots[slot % BASE_CHEST_SLOTS];
    if (action == MAAGE_SLOT_QUICK_MOVE) {
        count = group_id == MAAGE_SLOT_INVENTORY ? group.count * BASE_CHEST_SLOTS : MAAGE_INVENTORY_SLOTS;
        for (pass = 0; pass < 2; ++pass)
            for (i = 0; i < count && source->count; ++i) {
                /* Chest extraction fills the hotbar from right to left, then
                 * the main inventory, preserving the original slot order. */
                slot = group_id == MAAGE_SLOT_INVENTORY ? i : (i < 9 ? 8 - i : 44 - i);
                destination = group_id == MAAGE_SLOT_INVENTORY ? &group.states[slot / BASE_CHEST_SLOTS].slots[slot % BASE_CHEST_SLOTS] : &inventory.slots[slot];
                if (pass == 0 ? destination->count != 0 : destination->count == 0) chest_merge(source, destination, items);
            }
    } else {
        result = maage_item_stack_click(source, &inventory.cursor, items, action == MAAGE_SLOT_SECONDARY);
        if (maage_status_failed(result)) return result;
    }
    if (!memcmp(&inventory, &game->inventory, sizeof(inventory))) return chest_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_MUTATE);
    for (half = 0; half < group.count; ++half)
        group.states[half].revision = next_revision + 1;
    result = chest_commit(world, &group);
    if (maage_status_failed(result)) return result;
    game->inventory = inventory;
    game->crafting.revision = next_revision + 1;
    return chest_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_MUTATE);
}
static int chest_valid(const maage_block_entity_handler *handler, const maage_block_state_definition *block, const maage_item_registry *items, const void *data) {
    const maage_base_ids *ids = (const maage_base_ids *) handler->context;
    const base_chest_state *state = (const base_chest_state *) data;
    unsigned int slot;
    if (!block || (block->block_id != ids->block_chest && block->block_id != ids->block_trapped_chest) || maage_base_state_value(ids->blocks, block, ids->property_facing) >= 4 || !state->revision || state->revision > 4294967295UL || state->viewer > 4294967295UL || state->lid > 10 || state->previous_lid > 10) return 0;
    for (slot = 0; slot < BASE_CHEST_SLOTS; ++slot)
        if (!maage_item_stack_valid(items, &state->slots[slot])) return 0;
    return 1;
}
static maage_status chest_initialize(const maage_block_entity_handler *handler, const maage_block_state_definition *block, void *data, unsigned long identity, int loaded) {
    base_chest_state *state = (base_chest_state *) data;
    (void) handler;
    (void) block;
    state->identity = identity;
    state->viewer = state->lid = state->previous_lid = 0;
    if (!loaded) state->revision = 1;
    return chest_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_INITIALIZE);
}
static int chest_opaque(const maage_world *world, int x, int y, int z) {
    const maage_block_state_definition *state = chest_block(world, x, y, z);
    const maage_block_definition *block = state ? maage_block_registry_block(maage_world_registry(world), state->block_id) : NULL;
    return block ? block->opaque : -1;
}
static unsigned int chest_facing(const maage_world *world, const base_chest_group *group) {
    const maage_base_ids *ids = maage_base_state_references(maage_world_registry(world), group->blocks[0]);
    static const int front_x[4] = {0, -1, 0, 1}, front_z[4] = {1, 0, -1, 0};
    unsigned int facing = maage_base_state_value(ids->blocks, group->blocks[0], ids->property_facing);
    unsigned int half;
    int front = 0, back = 0, blocked;
    if (group->count == 2) {
        if (group->x[0] != group->x[1] && (facing & 1U)) facing = !(maage_base_state_value(ids->blocks, group->blocks[1], ids->property_facing) & 1U) ? maage_base_state_value(ids->blocks, group->blocks[1], ids->property_facing) : 0;
        if (group->z[0] != group->z[1] && !(facing & 1U)) facing = maage_base_state_value(ids->blocks, group->blocks[1], ids->property_facing) & 1U ? maage_base_state_value(ids->blocks, group->blocks[1], ids->property_facing) : 1;
    }
    for (half = 0; half < group->count; ++half) {
        blocked = chest_opaque(world, group->x[half] + front_x[facing], group->y, group->z[half] + front_z[facing]);
        if (blocked < 0) return facing;
        front |= blocked;
        blocked = chest_opaque(world, group->x[half] - front_x[facing], group->y, group->z[half] - front_z[facing]);
        if (blocked < 0) return facing;
        back |= blocked;
    }
    if (front && !back) facing = (facing + 2) & 3U;
    return facing;
}
static maage_status chest_tick(const maage_block_entity_handler *handler, maage_world *world, int x, int y, int z, const maage_block_state_definition *block, void *data, maage_block_state_id *replacement) {
    const maage_base_ids *ids = (const maage_base_ids *) handler->context;
    base_chest_state *state = (base_chest_state *) data;
    base_chest_group group;
    double position[3];
    maage_status result;
    unsigned int previous = state->previous_lid;
    result = chest_group(world, x, y, z, 0, &group);
    if (maage_status_failed(result)) return result;
    /* Persist one group facing through ordinary expected-state replacement;
     * both independent inventories retain their existing block entities. */
    *replacement = ids->chest_states[block->block_id == ids->block_trapped_chest][chest_facing(world, &group)];
    if (group.x[0] == x && group.z[0] == z && ((state->viewer && !state->lid) || (!state->viewer && state->lid == 5))) {
        position[0] = (group.x[0] + group.x[group.count - 1]) * 0.5 + 0.5;
        position[1] = y + 0.5;
        position[2] = (group.z[0] + group.z[group.count - 1]) * 0.5 + 0.5;
        result = maage_world_emit_sound(world, state->viewer ? ids->chest_open_sound : ids->chest_close_sound, position, 0.5, 0.9 + (double) (state->identity % 101UL) / 1000);
        if (maage_status_failed(result)) return result;
    }
    state->previous_lid = state->lid;
    if (state->viewer && state->lid < 10) ++state->lid;
    else if (!state->viewer && state->lid) --state->lid;
    return chest_status(*replacement != block->id || state->lid != state->previous_lid || previous != state->previous_lid ? MAAGE_STATUS_SUCCESS : MAAGE_STATUS_NOOP, MAAGE_OPERATION_TICK);
}
static maage_status chest_present(const maage_block_entity_handler *handler, const maage_world *world, int x, int y, int z, const maage_block_state_definition *block, const void *data, maage_block_entity_view *view) {
    const maage_base_ids *ids = (const maage_base_ids *) handler->context;
    const base_chest_state *state = (const base_chest_state *) data;
    base_chest_state other;
    base_chest_group group;
    unsigned int facing, lid = state->lid, previous = state->previous_lid;
    maage_status result = chest_group(world, x, y, z, 0, &group);
    if (maage_status_failed(result)) return result;
    if (group.x[0] != x || group.z[0] != z) return chest_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_PRESENT);
    view->secondary_state = MAAGE_CONTENT_ID_INVALID;
    if (group.count == 2) {
        result = maage_world_entity_read(world, group.x[1], y, group.z[1], handler->id, &other, sizeof(other));
        if (maage_status_failed(result)) return result;
        if (other.lid > lid) lid = other.lid;
        if (other.previous_lid > previous) previous = other.previous_lid;
        view->secondary_x = group.x[1];
        view->secondary_y = y;
        view->secondary_z = group.z[1];
        view->secondary_state = group.blocks[1]->id;
    }
    facing = chest_facing(world, &group);
    view->identity = state->identity;
    view->model = ids->chest_models[block->block_id == ids->block_trapped_chest][group.count - 1];
    view->rotation[1] = 3.141592653589793 - facing * 1.5707963267948966;
    view->translation[0] = (group.x[0] + group.x[group.count - 1]) * 0.5 + 0.5;
    view->translation[1] = y;
    view->translation[2] = (group.z[0] + group.z[group.count - 1]) * 0.5 + 0.5;
    view->animation.charge = (double) lid / 10;
    view->previous_charge = (double) previous / 10;
    return chest_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_PRESENT);
}
static int chest_can_commit(const void *before, const void *after) {
    const base_chest_state *a = (const base_chest_state *) before, *b = (const base_chest_state *) after;
    return a->identity == b->identity && a->revision < b->revision && b->revision <= 4294967295UL && a->lid == b->lid && a->previous_lid == b->previous_lid;
}
static const maage_item_stack *chest_drop(const void *data, size_t slot) {
    return slot < BASE_CHEST_SLOTS ? &((const base_chest_state *) data)->slots[slot] : NULL;
}
static maage_status chest_codec(base_chest_state *state, maage_entity_codec *codec) {
    maage_status result;
    unsigned int slot;
    for (slot = 0; slot < BASE_CHEST_SLOTS; ++slot) {
        result = codec->stack(codec->context, &state->slots[slot]);
        if (maage_status_failed(result)) return result;
    }
    return codec->word(codec->context, &state->revision, 4);
}
static maage_status chest_encode(const void *data, maage_entity_codec *codec) {
    base_chest_state copy = *(const base_chest_state *) data;
    return chest_codec(&copy, codec);
}
static maage_status chest_decode(void *data, maage_entity_codec *codec) {
    return chest_codec((base_chest_state *) data, codec);
}
static maage_status chest_sound_register(maage_content_context *context, const char *name, const char *path, maage_sound_id *id) {
    maage_sound_resource_definition resource;
    maage_sound_definition sound;
    maage_status result;
    memset(&resource, 0, sizeof(resource));
    resource.path = path;
    result = maage_sound_resource_register(context->sounds, name, &resource);
    if (maage_status_failed(result)) return result;
    memset(&sound, 0, sizeof(sound));
    sound.resources = &resource.id;
    sound.resource_count = 1;
    sound.bus = MAAGE_SOUND_BUS_BLOCKS;
    sound.positional = 1;
    sound.gain = sound.pitch = 1;
    sound.distance = 16;
    result = maage_sound_register(context->sounds, name, &sound);
    if (!maage_status_failed(result)) *id = sound.id;
    return result;
}
static void chest_bounds(const maage_world *world, int x, int y, int z, const maage_block_state_definition *state, maage_block_shape *shape) {
    const maage_block_state_definition *neighbor;
    unsigned int side, axis;
    for (side = 0; side < 4; ++side) {
        neighbor = chest_block(world, x + chest_dx[side], y, z + chest_dz[side]);
        if (!neighbor || neighbor->block_id != state->block_id) continue;
        axis = side < 2 ? 0 : 2;
        if (side & 1U) shape->maximum[axis] = 1;
        else shape->minimum[axis] = 0;
    }
}
maage_status maage_base_chests_bind(maage_content_context *context, maage_base_ids *ids) {
    maage_block_entity_handler handler;
    const maage_block_entity_handler *registered;
    maage_block_definition block;
    maage_status result;
    unsigned int trapped;
    result = chest_sound_register(context, "minecraft:chest/open", "minecraft/sounds/random/chestopen.ogg", &ids->chest_open_sound);
    if (!maage_status_failed(result)) result = chest_sound_register(context, "minecraft:chest/close", "minecraft/sounds/random/chestclosed.ogg", &ids->chest_close_sound);
    if (maage_status_failed(result)) return result;
    memset(&handler, 0, sizeof(handler));
    handler.version = 1;
    handler.size = sizeof(base_chest_state);
    handler.encoded_size = BASE_CHEST_SLOTS * 13 + 4;
    handler.drop_slots = BASE_CHEST_SLOTS;
    handler.snapshot_ticks = 100;
    handler.context = ids;
    handler.valid = chest_valid;
    handler.initialize = chest_initialize;
    handler.tick = chest_tick;
    handler.present = chest_present;
    handler.can_commit = chest_can_commit;
    handler.drop = chest_drop;
    handler.encode = chest_encode;
    handler.decode = chest_decode;
    result = maage_block_registry_register_entity(context->blocks, "minecraft:chest", &handler, &registered);
    if (maage_status_failed(result)) return result;
    for (trapped = 0; trapped < 2; ++trapped) {
        block = *maage_block_registry_block(context->blocks, trapped ? ids->block_trapped_chest : ids->block_chest);
        block.entity = registered;
        block.geometry_id = ids->geometry_entity;
        block.use = maage_base_chest_use;
        block.can_place = maage_base_chest_can_place;
        block.bounds = chest_bounds;
        block.opaque = 0;
        block.light_opacity = 0;
        block.hardness = 2.5;
        block.material = ids->material_wood;
        block.collision_shape.minimum[0] = block.collision_shape.minimum[2] = 1.0 / 16;
        block.collision_shape.maximum[0] = block.collision_shape.maximum[2] = 15.0 / 16;
        block.collision_shape.maximum[1] = 14.0 / 16;
        block.selection_shape = block.collision_shape;
        result = maage_block_registry_update_block(context->blocks, &block);
        if (maage_status_failed(result)) return result;
    }
    return chest_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_REGISTER);
}
