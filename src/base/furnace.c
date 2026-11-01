#include "maage/furnace.h"
#include <string.h>

static maage_status furnace_status(unsigned char code, unsigned char operation) {
    return maage_status_make(code, MAAGE_CONTEXT_WORLD, operation, 0);
}
static int furnace_equal(const maage_item_stack *a, const maage_item_stack *b) {
    return a->item_id == b->item_id && a->metadata == b->metadata && a->damage == b->damage;
}
int maage_furnace_valid(const maage_furnace_state *state, const maage_item_registry *items) {
    unsigned int i;
    if (!state || !state->revision || state->revision > 4294967295UL || state->duration > 4294967295UL || state->burn > state->duration || (!state->burn && state->cook) || state->cook >= MAAGE_FURNACE_COOK_TICKS) return 0;
    for (i = 0; i < 3; ++i)
        if (!maage_item_stack_valid(items, &state->slots[i])) return 0;
    return 1;
}
static int furnace_can_cook(const maage_furnace_state *state, const maage_smelting_recipe *recipe, const maage_item_registry *items) {
    const maage_item_definition *item;
    if (!recipe || !state->slots[0].count) return 0;
    item = maage_item_registry_get(items, recipe->output.item_id);
    return item && (!state->slots[2].count || furnace_equal(&state->slots[2], &recipe->output)) && recipe->output.count <= item->max_stack - state->slots[2].count;
}
maage_status maage_furnace_tick(maage_furnace_state *state, const maage_item_registry *items, const maage_smelting_registry *recipes, const maage_fuel_registry *fuels) {
    const maage_smelting_recipe *recipe;
    const maage_fuel_definition *fuel;
    unsigned int count;
    int can_cook, changed = 0;
    if (!items || !recipes || !fuels) return furnace_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_TICK);
    if (!maage_furnace_valid(state, items)) return furnace_status(MAAGE_STATUS_INVARIANT_VIOLATION, MAAGE_OPERATION_TICK);
    if (state->revision >= 4294967295UL - 1UL) return furnace_status(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_OPERATION_TICK);
    if (!state->burn && !state->cook && (!state->slots[0].count || !state->slots[1].count)) return furnace_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_TICK);
    recipe = maage_smelting_find(recipes, state->slots[0].item_id, state->slots[0].metadata);
    can_cook = furnace_can_cook(state, recipe, items);
    if (state->burn) {
        --state->burn;
        changed = 1;
    }
    if (!state->burn && can_cook && state->slots[1].count) {
        fuel = maage_fuel_find(fuels, state->slots[1].item_id, state->slots[1].metadata);
        if (fuel) {
            state->burn = state->duration = fuel->ticks;
            if (!--state->slots[1].count) state->slots[1] = fuel->remainder;
            ++state->revision;
            changed = 1;
        }
    }
    if (state->burn && can_cook) {
        if (++state->cook == MAAGE_FURNACE_COOK_TICKS) {
            count = state->slots[2].count;
            state->slots[2] = recipe->output;
            state->slots[2].count += count;
            if (!--state->slots[0].count) memset(&state->slots[0], 0, sizeof(state->slots[0]));
            state->cook = 0;
            ++state->revision;
        }
        changed = 1;
    } else if (state->cook) {
        state->cook = 0;
        changed = 1;
    }
    return furnace_status(changed ? MAAGE_STATUS_SUCCESS : MAAGE_STATUS_NOOP, MAAGE_OPERATION_TICK);
}
static void furnace_merge(maage_item_stack *source, maage_item_stack *destination, const maage_item_registry *items) {
    const maage_item_definition *item;
    unsigned int amount;
    if (!source->count || (destination->count && !furnace_equal(source, destination))) return;
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
maage_status maage_furnace_click(maage_furnace_state *state, maage_inventory *inventory, const maage_item_registry *items, const maage_smelting_registry *recipes, const maage_fuel_registry *fuels, unsigned long revision, unsigned int group, unsigned int slot, unsigned int action, double *experience) {
    maage_furnace_state next;
    maage_inventory carried;
    maage_item_stack *source, extracted;
    unsigned int before, remaining, first, end, pass, i;
    maage_status result;
    int changed;
    if (experience) *experience = 0;
    if (!experience || !inventory || !items || !recipes || !fuels || !maage_furnace_valid(state, items) || action < MAAGE_SLOT_PRIMARY || action > MAAGE_SLOT_QUICK_MOVE || (group != MAAGE_SLOT_INVENTORY && group != MAAGE_SLOT_CRAFT_INPUT && group != MAAGE_SLOT_CRAFT_RESULT) || (group == MAAGE_SLOT_INVENTORY ? slot >= MAAGE_INVENTORY_SLOTS : (group == MAAGE_SLOT_CRAFT_INPUT ? slot >= 2 : slot != 0))) return furnace_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_VALIDATE);
    if (state->revision >= 4294967295UL - 2UL) return furnace_status(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_OPERATION_MUTATE);
    if (group == MAAGE_SLOT_CRAFT_RESULT && revision != state->revision) return maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_VALIDATE, MAAGE_DETAIL_STALE);
    if (!maage_item_stack_valid(items, &inventory->cursor)) return furnace_status(MAAGE_STATUS_INVARIANT_VIOLATION, MAAGE_OPERATION_VALIDATE);
    for (i = 0; i < MAAGE_INVENTORY_SLOTS; ++i)
        if (!maage_item_stack_valid(items, &inventory->slots[i])) return furnace_status(MAAGE_STATUS_INVARIANT_VIOLATION, MAAGE_OPERATION_VALIDATE);
    next = *state;
    carried = *inventory;
    source = group == MAAGE_SLOT_INVENTORY ? &carried.slots[slot] : &next.slots[group == MAAGE_SLOT_CRAFT_RESULT ? 2 : slot];
    extracted = *source;
    before = source->count;
    if (action == MAAGE_SLOT_QUICK_MOVE) {
        if (group != MAAGE_SLOT_INVENTORY) {
            result = maage_inventory_add(&carried, items, source, &remaining);
            if (maage_status_failed(result)) return result;
            source->count = remaining;
            if (!remaining) memset(source, 0, sizeof(*source));
        } else if (maage_smelting_find(recipes, source->item_id, source->metadata)) furnace_merge(source, &next.slots[0], items);
        else if (maage_fuel_find(fuels, source->item_id, source->metadata)) furnace_merge(source, &next.slots[1], items);
        else {
            first = slot < 9 ? 9 : 0;
            end = slot < 9 ? MAAGE_INVENTORY_SLOTS : 9;
            for (pass = 0; pass < 2; ++pass)
                for (i = first; i < end && source->count; ++i)
                    if (pass == 0 ? carried.slots[i].count != 0 : carried.slots[i].count == 0) furnace_merge(source, &carried.slots[i], items);
        }
    } else if (group == MAAGE_SLOT_CRAFT_RESULT) {
        if (!maage_item_stack_valid(items, &carried.cursor)) return furnace_status(MAAGE_STATUS_INVARIANT_VIOLATION, MAAGE_OPERATION_MUTATE);
        if (action == MAAGE_SLOT_SECONDARY && !carried.cursor.count && source->count) {
            carried.cursor = *source;
            carried.cursor.count = (source->count + 1) / 2;
            source->count -= carried.cursor.count;
            if (!source->count) memset(source, 0, sizeof(*source));
        } else furnace_merge(source, &carried.cursor, items);
    } else {
        if (group == MAAGE_SLOT_CRAFT_INPUT && slot == 1 && carried.cursor.count && !maage_fuel_find(fuels, carried.cursor.item_id, carried.cursor.metadata) && !maage_item_registry_get(items, carried.cursor.item_id)->fuel_container) return furnace_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_MUTATE);
        if (group == MAAGE_SLOT_CRAFT_INPUT && slot == 1 && (carried.cursor.count && maage_item_registry_get(items, carried.cursor.item_id)->fuel_container) && carried.cursor.count > 1) {
            if (source->count) return furnace_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_MUTATE);
            *source = carried.cursor;
            source->count = 1;
            --carried.cursor.count;
            result = furnace_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_MUTATE);
        } else result = maage_item_stack_click(source, &carried.cursor, items, action == MAAGE_SLOT_SECONDARY);
        if (maage_status_failed(result)) return result;
    }
    if (group == MAAGE_SLOT_CRAFT_RESULT) *experience = (before - source->count) * maage_smelting_experience(recipes, extracted.item_id, extracted.metadata);
    changed = !furnace_equal(&carried.cursor, &inventory->cursor) || carried.cursor.count != inventory->cursor.count;
    for (i = 0; i < 3 && !changed; ++i)
        changed = !furnace_equal(&next.slots[i], &state->slots[i]) || next.slots[i].count != state->slots[i].count;
    for (i = 0; i < MAAGE_INVENTORY_SLOTS && !changed; ++i)
        changed = !furnace_equal(&carried.slots[i], &inventory->slots[i]) || carried.slots[i].count != inventory->slots[i].count;
    if (!changed) return furnace_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_MUTATE);
    ++next.revision;
    *state = next;
    *inventory = carried;
    return furnace_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_MUTATE);
}
