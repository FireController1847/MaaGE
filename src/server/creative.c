#include "creative.h"
#include <string.h>

static maage_status creative_result(unsigned char code) {
    return maage_status_make(code, code < MAAGE_STATUS_WARNING ? 0 : MAAGE_CONTEXT_SERVER, MAAGE_OPERATION_MUTATE, 0);
}
maage_status maage_server_creative_action(const maage_content *content, maage_world *world, maage_gameplay *game, const maage_player *player, const maage_session_command *command, double yaw, double pitch) {
    const maage_creative_registry *catalog = maage_content_creative(content);
    const maage_item_registry *items = maage_content_items(content);
    const maage_container_registry *containers = maage_content_containers(content);
    const maage_creative_action *action;
    const maage_container_definition *container;
    const maage_creative_tab_definition *tab;
    const maage_creative_entry *entry;
    const maage_item_definition *item;
    maage_item_stack *cursor;
    maage_inventory next;
    maage_gameplay drop;
    maage_status result;
    unsigned int i;
    int inventory, auxiliary_changed = 0;
    if (!command || !game || !world || !player) return creative_result(MAAGE_STATUS_INVALID_ARGUMENT);
    action = &command->creative;
    container = maage_container_get(containers, game->crafting.kind);
    tab = maage_creative_tab(catalog, action->tab);
    if (game->mode != MAAGE_GAME_MODE_CREATIVE || !game->health || !container || !container->creative_catalog || !container->personal || !tab || !maage_creative_is_finalized(catalog) || !maage_container_accessible(containers, &game->crafting, player, world)) return creative_result(MAAGE_STATUS_ACCESS_DENIED);
    if (!command->container_identity || command->container_identity != game->crafting.identity || !command->container_revision || command->container_revision != game->crafting.revision) return maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_SERVER, MAAGE_OPERATION_VALIDATE, MAAGE_DETAIL_STALE);
    if (game->crafting.revision >= 4294967295UL) return creative_result(MAAGE_STATUS_BUFFER_OVERFLOW);
    if (action->kind < MAAGE_CREATIVE_TAKE || action->kind > MAAGE_CREATIVE_OUTSIDE || action->secondary > 1 || action->hotbar >= MAAGE_HOTBAR_SLOTS) return creative_result(MAAGE_STATUS_INVALID_ARGUMENT);
    if (!maage_item_stack_valid(items, &game->inventory.cursor) || game->inventory.selected >= MAAGE_HOTBAR_SLOTS) return creative_result(MAAGE_STATUS_INVARIANT_VIOLATION);
    for (i = 0; i < MAAGE_INVENTORY_SLOTS; ++i)
        if (!maage_item_stack_valid(items, &game->inventory.slots[i])) return creative_result(MAAGE_STATUS_INVARIANT_VIOLATION);
    for (i = 0; i < MAAGE_CRAFTING_CELLS; ++i)
        if (!maage_item_stack_valid(items, &game->crafting.inputs[i])) return creative_result(MAAGE_STATUS_INVARIANT_VIOLATION);
    inventory = tab->kind == MAAGE_CREATIVE_INVENTORY;
    next = game->inventory;
    cursor = &next.cursor;
    result = creative_result(MAAGE_STATUS_NOOP);
    if (action->kind <= MAAGE_CREATIVE_DROP) {
        if (inventory) return creative_result(MAAGE_STATUS_ACCESS_DENIED);
        entry = maage_creative_get(catalog, action->entry);
        if (action->entry && (!entry || (tab->kind == MAAGE_CREATIVE_SEARCH ? !entry->searchable : entry->tab != tab->id))) return creative_result(MAAGE_STATUS_INVALID_ARGUMENT);
        item = entry ? maage_item_registry_get(items, entry->stack.item_id) : NULL;
        if (entry && (!item || !maage_item_stack_valid(items, &entry->stack) || entry->stack.damage)) return creative_result(MAAGE_STATUS_INVARIANT_VIOLATION);
        if (action->kind == MAAGE_CREATIVE_HOTBAR) {
            if (entry) {
                next.slots[action->hotbar] = entry->stack;
                next.slots[action->hotbar].count = item->max_stack;
            }
        } else if (action->kind == MAAGE_CREATIVE_CLONE) {
            if (entry && !cursor->count) {
                *cursor = entry->stack;
                cursor->count = item->max_stack;
            }
        } else if (action->kind == MAAGE_CREATIVE_DROP) {
            if (!entry) return result;
            /* A temporary cursor uses the same reserved entity-drop transaction
             * without borrowing or changing the player's carried stack. */
            drop = *game;
            drop.inventory.cursor = entry->stack;
            if (action->secondary) drop.inventory.cursor.count = item->max_stack;
            result = maage_gameplay_drop(&drop, player, world, items, yaw, pitch, MAAGE_INVENTORY_SLOTS, 1);
            if (maage_status_failed(result)) return result;
            ++game->crafting.revision;
            return result;
        } else if (entry && cursor->count && cursor->item_id == entry->stack.item_id && cursor->metadata == entry->stack.metadata && cursor->damage == entry->stack.damage) {
            if (action->secondary) {
                if (--cursor->count == 0) memset(cursor, 0, sizeof(*cursor));
            } else if (action->kind == MAAGE_CREATIVE_TAKE_STACK) cursor->count = item->max_stack;
            else if (cursor->count < item->max_stack) ++cursor->count;
        } else if (entry && !cursor->count) {
            *cursor = entry->stack;
            if (action->kind == MAAGE_CREATIVE_TAKE_STACK) cursor->count = item->max_stack;
        } else memset(cursor, 0, sizeof(*cursor));
    } else if (action->kind == MAAGE_CREATIVE_PLAYER_SLOT) {
        result = maage_inventory_operate(&next, items, &action->inventory, 1, !inventory);
        if (maage_status_failed(result)) return result;
    } else if (action->kind == MAAGE_CREATIVE_PLAYER_DROP || action->kind == MAAGE_CREATIVE_OUTSIDE) {
        i = action->kind == MAAGE_CREATIVE_OUTSIDE ? MAAGE_INVENTORY_SLOTS : action->inventory.slot;
        if (i != MAAGE_INVENTORY_SLOTS && i >= (inventory ? MAAGE_INVENTORY_SLOTS : MAAGE_HOTBAR_SLOTS)) return creative_result(MAAGE_STATUS_INVALID_ARGUMENT);
        /* Slot-drop keys operate only with an empty cursor. Outside clicks drop
         * the whole carried stack on primary, one item on secondary. */
        if (action->kind == MAAGE_CREATIVE_PLAYER_DROP && game->inventory.cursor.count) return result;
        result = maage_gameplay_drop(game, player, world, items, yaw, pitch, i, action->kind == MAAGE_CREATIVE_OUTSIDE ? !action->secondary : action->secondary);
        if (!maage_status_failed(result) && maage_status_code(result) != MAAGE_STATUS_NOOP) ++game->crafting.revision;
        return result;
    } else {
        if (!inventory) return creative_result(MAAGE_STATUS_ACCESS_DENIED);
        if (action->kind == MAAGE_CREATIVE_DESTROY) memset(cursor, 0, sizeof(*cursor));
        else {
            for (i = 0; i < MAAGE_CRAFTING_CELLS; ++i)
                if (game->crafting.inputs[i].count) auxiliary_changed = 1;
            if (game->crafting.output.count) auxiliary_changed = 1;
            memset(next.slots, 0, sizeof(next.slots));
            /* Clear temporary player inputs too; no derived result or stale
             * carried quantity survives an inventory-wide Creative deletion. */
            memset(cursor, 0, sizeof(*cursor));
            memset(game->crafting.inputs, 0, sizeof(game->crafting.inputs));
            memset(&game->crafting.output, 0, sizeof(game->crafting.output));
        }
    }
    if (!auxiliary_changed && !memcmp(&next, &game->inventory, sizeof(next))) return creative_result(MAAGE_STATUS_NOOP);
    game->inventory = next;
    ++game->crafting.revision;
    return action->kind == MAAGE_CREATIVE_PLAYER_SLOT ? result : creative_result(MAAGE_STATUS_SUCCESS);
}
