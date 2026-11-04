#include "maage/crafting.h"
#include "maage/entities.h"
#include <string.h>

static maage_status crafting_status(unsigned char code, unsigned char operation) {
    return maage_status_make(code, MAAGE_CONTEXT_WORLD, operation, 0);
}
static int stacks_equal(const maage_item_stack *a, const maage_item_stack *b) {
    return a->item_id == b->item_id && a->variant == b->variant && a->damage == b->damage;
}
maage_status maage_crafting_refresh(maage_crafting_container *container, const maage_recipe_registry *recipes) {
    maage_recipe_match match;
    maage_status result;
    if (!container) return crafting_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_UPDATE);
    memset(&container->output, 0, sizeof(container->output));
    if (!container->kind || container->closing || !container->grid_width) return crafting_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_UPDATE);
    result = maage_recipe_find(recipes, container->inputs, container->grid_width, &match);
    if (!maage_status_failed(result)) container->output = match.output;
    return maage_status_failed(result) ? result : crafting_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_UPDATE);
}
static maage_status craft_once(maage_crafting_container *container, maage_inventory *inventory, const maage_item_registry *items, const maage_recipe_match *match, int quick) {
    maage_inventory next;
    maage_item_stack grid[MAAGE_CRAFTING_CELLS], *cursor;
    const maage_item_definition *item;
    maage_status result;
    unsigned int i, remaining, amount;
    next = *inventory;
    memcpy(grid, container->inputs, sizeof(grid));
    if (quick) {
        result = maage_inventory_add(&next, items, &match->output, &remaining);
        if (maage_status_failed(result)) return result;
        if (remaining) return crafting_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_MUTATE);
    } else {
        cursor = &next.cursor;
        item = maage_item_registry_get(items, match->output.item_id);
        if (!item || (cursor->count && !stacks_equal(cursor, &match->output)) || cursor->count > item->max_stack - match->output.count) return crafting_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_MUTATE);
        amount = cursor->count;
        *cursor = match->output;
        cursor->count += amount;
    }
    for (i = 0; i < MAAGE_CRAFTING_CELLS; ++i) {
        if (grid[i].count < match->consume[i]) return crafting_status(MAAGE_STATUS_INVARIANT_VIOLATION, MAAGE_OPERATION_MUTATE);
        grid[i].count -= match->consume[i];
        if (!grid[i].count) memset(&grid[i], 0, sizeof(grid[i]));
        if (!match->remainders[i].count) continue;
        /* Return remainders to inventory first, then the newly empty input cell.
         * If neither can accept a remainder, the entire craft stays untouched. */
        result = maage_inventory_add(&next, items, &match->remainders[i], &remaining);
        if (maage_status_failed(result)) return result;
        if (remaining) {
            item = maage_item_registry_get(items, match->remainders[i].item_id);
            if (grid[i].count && (!stacks_equal(&grid[i], &match->remainders[i]) || grid[i].count > item->max_stack - remaining)) return crafting_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_MUTATE);
            amount = grid[i].count;
            grid[i] = match->remainders[i];
            grid[i].count = amount + remaining;
        }
    }
    *inventory = next;
    memcpy(container->inputs, grid, sizeof(grid));
    ++container->revision;
    return crafting_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_MUTATE);
}
maage_status maage_crafting_click(maage_crafting_container *container, maage_inventory *inventory, const maage_item_registry *items, const maage_recipe_registry *recipes, unsigned long revision, unsigned int group, unsigned int slot, unsigned int action) {
    maage_item_stack *source;
    maage_recipe_match match;
    maage_recipe_id original = 0;
    maage_status result, outcome;
    unsigned int i, width;
    if (!container || !inventory || !items || !recipes || action < MAAGE_SLOT_PRIMARY || action > MAAGE_SLOT_QUICK_MOVE || group < MAAGE_SLOT_INVENTORY || group > MAAGE_SLOT_CRAFT_RESULT) return crafting_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_VALIDATE);
    if (!container->kind || container->closing || (container->grid_width != 2 && container->grid_width != 3)) return crafting_status(MAAGE_STATUS_INVALID_STATE, MAAGE_OPERATION_MUTATE);
    if (container->revision >= 4294967295UL - 64UL) return crafting_status(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_OPERATION_MUTATE);
    width = container->grid_width;
    if ((group == MAAGE_SLOT_INVENTORY && slot >= MAAGE_INVENTORY_SLOTS) ||
        (group == MAAGE_SLOT_CRAFT_INPUT && slot >= width * width) ||
        (group == MAAGE_SLOT_CRAFT_RESULT && slot != 0))
        return crafting_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_VALIDATE);
    if (!maage_item_stack_valid(items, &inventory->cursor)) return crafting_status(MAAGE_STATUS_INVARIANT_VIOLATION, MAAGE_OPERATION_VALIDATE);
    if (group != MAAGE_SLOT_CRAFT_RESULT) {
        source = group == MAAGE_SLOT_INVENTORY ? &inventory->slots[slot] : &container->inputs[slot];
        if (action == MAAGE_SLOT_QUICK_MOVE)
            result = maage_inventory_transfer(source, inventory, items, group == MAAGE_SLOT_INVENTORY && slot < 9 ? 9U : 0U, group == MAAGE_SLOT_INVENTORY && slot >= 9 ? 9U : MAAGE_INVENTORY_SLOTS);
        else
            result = maage_item_stack_click(source, &inventory->cursor, items, action == MAAGE_SLOT_SECONDARY);
        if (maage_status_failed(result)) return result;
        ++container->revision;
        if (group == MAAGE_SLOT_CRAFT_INPUT) {
            maage_status refreshed = maage_crafting_refresh(container, recipes);
            if (maage_status_failed(refreshed)) return refreshed;
        }
        return result;
    }
    if (revision != container->revision) return maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_VALIDATE, MAAGE_DETAIL_STALE);
    outcome = crafting_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_MUTATE);
    /* Grid stacks hold at most 64 units. A bounded batch also protects future
     * recipes whose remainders regenerate an ingredient. */
    for (i = 0; i < (action == MAAGE_SLOT_QUICK_MOVE ? 64U : 1U); ++i) {
        result = maage_recipe_find(recipes, container->inputs, width, &match);
        if (maage_status_failed(result)) return result;
        if (!match.recipe || (original && match.recipe != original)) break;
        original = match.recipe;
        result = craft_once(container, inventory, items, &match, action == MAAGE_SLOT_QUICK_MOVE);
        if (maage_status_failed(result)) return result;
        if (maage_status_code(result) == MAAGE_STATUS_NOOP) break;
        outcome = result;
    }
    result = maage_crafting_refresh(container, recipes);
    return maage_status_failed(result) ? result : outcome;
}
maage_status maage_crafting_close(maage_crafting_container *container, maage_inventory *inventory, const maage_item_registry *items, const maage_player *player, maage_world *world) {
    maage_inventory next;
    maage_item_stack *source;
    maage_entities *entities;
    maage_status result;
    unsigned int i, remaining, overflow = 0, occupied = 0;
    double position[3], velocity[3] = {0, 0.1, 0};
    if (!container || !inventory || !player || !world) return crafting_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_CLOSE);
    for (i = 0; i < MAAGE_CRAFTING_CELLS; ++i)
        if (container->inputs[i].count) ++occupied;
    if (!container->kind && !container->closing && !occupied && !inventory->cursor.count) return crafting_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_CLOSE);
    container->closing = 1;
    memset(&container->output, 0, sizeof(container->output));
    next = *inventory;
    for (i = 0; i <= MAAGE_CRAFTING_CELLS; ++i) {
        source = i == MAAGE_CRAFTING_CELLS ? &inventory->cursor : &container->inputs[i];
        result = maage_inventory_add(&next, items, source, &remaining);
        if (maage_status_failed(result)) return result;
        if (remaining) ++overflow;
    }
    entities = maage_world_entities(world);
    result = maage_entities_reserve(entities, overflow);
    if (maage_status_failed(result)) return result;
    memcpy(position, player->position, sizeof(position));
    if (position[1] < -64) position[1] = -64;
    if (position[1] > MAAGE_WORLD_HEIGHT + 63) position[1] = MAAGE_WORLD_HEIGHT + 63;
    /* Reserve precedes every ownership change. Clear each source only after
     * its inventory insertion/drop succeeds, retaining any unexpected failure. */
    for (i = 0; i <= MAAGE_CRAFTING_CELLS; ++i) {
        source = i == MAAGE_CRAFTING_CELLS ? &inventory->cursor : &container->inputs[i];
        result = maage_inventory_add(inventory, items, source, &remaining);
        if (maage_status_failed(result)) return result;
        source->count = remaining;
        if (remaining) {
            result = maage_entities_spawn(entities, items, source, position, velocity, 40);
            if (maage_status_failed(result)) return result;
        }
        memset(source, 0, sizeof(*source));
    }
    container->kind = MAAGE_CONTAINER_CLOSED;
    container->closing = 0;
    ++container->revision;
    return crafting_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_CLOSE);
}
