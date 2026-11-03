#include "registry_policy.h"
#include "maage/containers.h"
#include "maage/gameplay.h"
#include "maage/internal/registry_pages.h"
#include <stdlib.h>
#include <string.h>

typedef struct container_entry {
    maage_container_definition definition;
    maage_container_slot_definition *slots;
    maage_container_progress_definition *progress;
    char *name, *title, *inventory_title;
} container_entry;
struct maage_container_registry {
    content_policy *policy;
    maage_registry_pages entries;
    container_entry **names;
    size_t count, buckets;
    maage_container_id inventory;
    int finalized;
};
static maage_status container_status(unsigned char code, unsigned char operation) {
    return maage_status_make(code, code < MAAGE_STATUS_WARNING ? 0 : MAAGE_CONTEXT_REGISTRY, operation, 0);
}
static container_entry *container_at(const maage_container_registry *registry, maage_container_id id) {
    void **entry = registry ? maage_registry_pages_find(&registry->entries, id) : NULL;
    return entry ? (container_entry *) *entry : NULL;
}
static unsigned long container_hash(const char *name) {
    unsigned long value = 2166136261UL;
    while (*name)
        value = (value ^ (unsigned char) *name++) * 16777619UL;
    return value;
}
static int container_name_valid(const char *name) {
    int colon = 0;
    const char *first = name;
    if (!name || !*name) return 0;
    while (*name) {
        if (*name == ':') {
            if (colon || name == first || !name[1]) return 0;
            colon = 1;
        } else if (!((*name >= 'a' && *name <= 'z') || (*name >= '0' && *name <= '9') || *name == '_' || *name == '-' || *name == '.' || (colon && *name == '/'))) return 0;
        ++name;
    }
    return colon;
}
static size_t container_bucket(const maage_container_registry *registry, const char *name) {
    size_t bucket = container_hash(name) & (registry->buckets - 1);
    while (registry->names[bucket] && strcmp(registry->names[bucket]->name, name))
        bucket = (bucket + 1) & (registry->buckets - 1);
    return bucket;
}
static char *container_copy_text(const char *text) {
    char *copy;
    if (!text) return NULL;
    copy = (char *) malloc(strlen(text) + 1);
    if (copy) strcpy(copy, text);
    return copy;
}
static void container_release(void *value) {
    container_entry *entry = (container_entry *) value;
    free(entry->slots);
    free(entry->progress);
    free(entry->name);
    free(entry->title);
    free(entry->inventory_title);
    free(entry);
}
maage_status maage_container_registry_create(maage_container_registry **output) {
    maage_container_registry *registry;
    if (output) *output = NULL;
    if (!output) return container_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_CREATE);
    registry = (maage_container_registry *) calloc(1, sizeof(*registry));
    if (!registry) return container_status(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_CREATE);
    registry->buckets = 16;
    registry->names = (container_entry **) calloc(registry->buckets, sizeof(*registry->names));
    if (!registry->names) {
        free(registry);
        return container_status(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_CREATE);
    }
    *output = registry;
    return container_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_CREATE);
}
maage_status maage_container_registry_destroy(maage_container_registry *registry) {
    if (!registry) return container_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_DESTROY);
    maage_registry_pages_destroy(&registry->entries, container_release);
    free(registry->names);
    free(registry);
    return container_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_DESTROY);
}
maage_status maage_container_register(maage_container_registry *registry, const char *name, maage_container_definition *definition) {
    maage_status policy_result;
    container_entry *entry, **names;
    size_t i, j, bucket, buckets;
    void **destination;
    const maage_container_slot_definition *slot;
    const maage_container_progress_definition *progress;
    if (registry) {
        policy_result = content_policy_register(registry->policy, CONTENT_CONTAINER, name, (unsigned long) (registry->count + 1));
        if (maage_status_failed(policy_result)) return policy_result;
    }
    if (!registry || registry->finalized || !container_name_valid(name) || !definition || (definition->grid_width && definition->grid_width != 2 && definition->grid_width != 3) || !definition->presentation.width || !definition->presentation.height || !definition->presentation.texture_edge || (definition->slot_count && !definition->slots) || (definition->progress_count && !definition->progress) || (definition->creative_catalog && (!definition->personal || !definition->presentation.creative_width || !definition->presentation.creative_height)) || (!definition->grid_width && !definition->action) || (!definition->personal && !definition->accessible)) return container_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_REGISTER);
    if (registry->count >= MAAGE_CONTENT_ID_INVALID - 1UL || definition->slot_count > 4294967295UL || definition->slot_count > (size_t) - 1 / sizeof(*entry->slots) || definition->progress_count > 4294967295UL || definition->progress_count > (size_t) - 1 / sizeof(*entry->progress)) return container_status(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_OPERATION_REGISTER);
    if (definition->presentation.background_split && (definition->presentation.background_split >= definition->presentation.height || definition->presentation.background_bottom > definition->presentation.texture_edge || definition->presentation.height - definition->presentation.background_split > definition->presentation.texture_edge - definition->presentation.background_bottom)) return container_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_REGISTER);
    if (definition->creative_catalog && (!definition->personal || !definition->presentation.show_inventory || definition->presentation.creative_width != 195 || definition->presentation.creative_height != 136 || definition->presentation.texture_edge != 256)) return container_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_REGISTER);
    bucket = container_bucket(registry, name);
    if (registry->names[bucket]) return container_status(MAAGE_STATUS_INVALID_STATE, MAAGE_OPERATION_REGISTER);
    for (i = 0; i < definition->slot_count; ++i) {
        slot = &definition->slots[i];
        if (!slot->group || slot->group == MAAGE_SLOT_INVENTORY || (!slot->storage && !definition->sample) || (slot->flags & ~(MAAGE_CONTAINER_SLOT_INSERT | MAAGE_CONTAINER_SLOT_EXTRACT | MAAGE_CONTAINER_SLOT_REVISION)) || slot->storage > MAAGE_CONTAINER_SLOT_OUTPUT || (slot->storage == MAAGE_CONTAINER_SLOT_INPUT && slot->slot >= MAAGE_CRAFTING_CELLS) || (slot->storage == MAAGE_CONTAINER_SLOT_OUTPUT && slot->slot)) return container_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_REGISTER);
        if (!definition->action && !((slot->group == MAAGE_SLOT_CRAFT_INPUT && slot->storage == MAAGE_CONTAINER_SLOT_INPUT && slot->slot < definition->grid_width * definition->grid_width) || (slot->group == MAAGE_SLOT_CRAFT_RESULT && slot->storage == MAAGE_CONTAINER_SLOT_OUTPUT && slot->slot == 0))) return container_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_REGISTER);
        for (j = 0; j < i; ++j)
            if (definition->slots[j].group == slot->group && definition->slots[j].slot == slot->slot) return container_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_REGISTER);
    }
    for (i = 0; i < definition->progress_count; ++i) {
        progress = &definition->progress[i];
        if (!progress->steps || progress->extra_pixel > 1 || progress->width <= 0 || progress->height <= 0 || progress->steps > (unsigned int) (progress->vertical ? progress->height : progress->width)) return container_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_REGISTER);
        for (j = 0; j < 4; ++j)
            if (!(progress->uv[j] >= 0 && progress->uv[j] <= 1)) return container_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_REGISTER);
    }
    if (registry->count >= registry->buckets / 2) {
        if (registry->buckets > (size_t) - 1 / (2 * sizeof(*names))) return container_status(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_OPERATION_REGISTER);
        buckets = registry->buckets * 2;
        names = (container_entry **) calloc(buckets, sizeof(*names));
        if (!names) return container_status(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_REGISTER);
        for (i = 0; i < registry->buckets; ++i) {
            if (!registry->names[i]) continue;
            bucket = container_hash(registry->names[i]->name) & (buckets - 1);
            while (names[bucket])
                bucket = (bucket + 1) & (buckets - 1);
            names[bucket] = registry->names[i];
        }
        free(registry->names);
        registry->names = names;
        registry->buckets = buckets;
    }
    entry = (container_entry *) calloc(1, sizeof(*entry));
    if (!entry) return container_status(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_REGISTER);
    entry->name = container_copy_text(name);
    entry->title = container_copy_text(definition->presentation.title);
    entry->inventory_title = container_copy_text(definition->presentation.inventory_title);
    if (!entry->name || (definition->presentation.title && !entry->title) || (definition->presentation.inventory_title && !entry->inventory_title)) goto allocation_failure;
    if (definition->slot_count) {
        entry->slots = (maage_container_slot_definition *) malloc(definition->slot_count * sizeof(*entry->slots));
        if (!entry->slots) goto allocation_failure;
        memcpy(entry->slots, definition->slots, definition->slot_count * sizeof(*entry->slots));
    }
    if (definition->progress_count) {
        entry->progress = (maage_container_progress_definition *) malloc(definition->progress_count * sizeof(*entry->progress));
        if (!entry->progress) goto allocation_failure;
        memcpy(entry->progress, definition->progress, definition->progress_count * sizeof(*entry->progress));
    }
    entry->definition = *definition;
    entry->definition.id = (maage_container_id) registry->count + 1UL;
    entry->definition.slots = entry->slots;
    entry->definition.progress = entry->progress;
    entry->definition.presentation.title = entry->title;
    entry->definition.presentation.inventory_title = entry->inventory_title;
    destination = maage_registry_pages_ensure(&registry->entries, entry->definition.id);
    if (!destination) goto allocation_failure;
    *destination = entry;
    registry->names[container_bucket(registry, name)] = entry;
    ++registry->count;
    definition->id = entry->definition.id;
    return container_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_REGISTER);
allocation_failure:
    container_release(entry);
    return container_status(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_REGISTER);
}
maage_status maage_container_finalize(maage_container_registry *registry) {
    maage_status policy_result;
    if (registry) {
        policy_result = content_policy_finalize(registry->policy);
        if (maage_status_failed(policy_result)) return policy_result;
    }
    if (!registry || registry->finalized) return container_status(MAAGE_STATUS_INVALID_STATE, MAAGE_OPERATION_FINALIZE);
    registry->finalized = 1;
    return container_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_FINALIZE);
}
int maage_container_is_finalized(const maage_container_registry *registry) {
    return registry && registry->finalized;
}
size_t maage_container_count(const maage_container_registry *registry) {
    return registry ? registry->count : 0;
}
const maage_container_definition *maage_container_get(const maage_container_registry *registry, maage_container_id id) {
    const container_entry *entry = container_at(registry, id);
    return entry ? &entry->definition : NULL;
}
const char *maage_container_name(const maage_container_registry *registry, maage_container_id id) {
    const container_entry *entry = container_at(registry, id);
    return entry ? entry->name : NULL;
}
maage_status maage_container_resolve(const maage_container_registry *registry, const char *name, maage_container_id *id) {
    maage_status policy_result;
    container_entry *entry;
    if (id) *id = 0;
    if (registry) {
        policy_result = content_policy_read(registry->policy, CONTENT_CONTAINER, name);
        if (maage_status_failed(policy_result)) return policy_result;
    }
    if (!registry || !name || !id) return container_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_RESOLVE);
    entry = registry->names[container_bucket(registry, name)];
    if (entry) *id = entry->definition.id;
    return container_status(entry ? MAAGE_STATUS_SUCCESS : MAAGE_STATUS_NOT_FOUND, MAAGE_OPERATION_RESOLVE);
}
maage_status maage_container_bind_inventory(maage_container_registry *registry, maage_container_id id) {
    maage_status policy_result;
    const maage_container_definition *definition = maage_container_get(registry, id);
    if (registry) {
        policy_result = content_policy_register(registry->policy, CONTENT_INVENTORY_BINDING, "inventory", 0);
        if (maage_status_failed(policy_result)) return policy_result;
    }
    if (!registry || registry->finalized || !definition || !definition->personal) return container_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_SET);
    registry->inventory = id;
    return container_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_SET);
}
maage_container_id maage_container_inventory(const maage_container_registry *registry) {
    return registry ? registry->inventory : 0;
}
static unsigned int container_window_count(size_t count, unsigned long first) {
    size_t remaining = first < count ? count - first : 0;
    return (unsigned int) (remaining > MAAGE_CONTAINER_VIEW_SLOTS ? MAAGE_CONTAINER_VIEW_SLOTS : remaining);
}
static int container_window_valid(const maage_container_definition *definition, const maage_crafting_container *container) {
    size_t count = definition->slot_count > definition->progress_count ? definition->slot_count : definition->progress_count;
    return (!container->view_first || container->view_first < count) && container->view_first % MAAGE_CONTAINER_VIEW_SLOTS == 0 && container->view_count == container_window_count(definition->slot_count, container->view_first) && container->progress_count == container_window_count(definition->progress_count, container->view_first);
}
maage_status maage_container_open(const maage_container_registry *registry, maage_crafting_container *container, maage_container_id id, int x, int y, int z, maage_world *world) {
    const maage_container_definition *definition = maage_container_get(registry, id);
    maage_crafting_container next;
    maage_status result;
    unsigned int i;
    if (!registry || !registry->finalized || !container || !definition || !world) return container_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_OPEN);
    if (container->kind || container->closing) return container_status(MAAGE_STATUS_INVALID_STATE, MAAGE_OPERATION_OPEN);
    for (i = 0; i < MAAGE_CRAFTING_CELLS; ++i)
        if (container->inputs[i].count) return container_status(MAAGE_STATUS_INVALID_STATE, MAAGE_OPERATION_OPEN);
    if (container->identity == 4294967295UL) return container_status(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_OPERATION_OPEN);
    memset(&next, 0, sizeof(next));
    next.identity = container->identity + 1UL;
    next.revision = 1;
    next.kind = id;
    next.grid_width = definition->grid_width;
    next.x = x;
    next.y = y;
    next.z = z;
    next.view_count = container_window_count(definition->slot_count, 0);
    next.progress_count = container_window_count(definition->progress_count, 0);
    if (definition->opened) {
        result = definition->opened(definition->context, world, &next);
        if (maage_status_failed(result) || maage_status_code(result) == MAAGE_STATUS_NOOP) return result;
    }
    if (next.identity != container->identity + 1UL || next.kind != id || !next.revision || next.revision > 4294967295UL || next.closing || next.x != x || next.y != y || next.z != z || next.grid_width != definition->grid_width || !container_window_valid(definition, &next)) return container_status(MAAGE_STATUS_INVARIANT_VIOLATION, MAAGE_OPERATION_OPEN);
    *container = next;
    return container_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_OPEN);
}
maage_status maage_container_page(const maage_container_registry *registry, maage_crafting_container *container, unsigned long first) {
    const maage_container_definition *definition = container ? maage_container_get(registry, container->kind) : NULL;
    size_t count = definition ? (definition->slot_count > definition->progress_count ? definition->slot_count : definition->progress_count) : 0;
    if (!definition || container->closing || first >= count || first % MAAGE_CONTAINER_VIEW_SLOTS) return container_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_SET);
    container->view_first = first;
    container->view_count = container_window_count(definition->slot_count, first);
    container->progress_count = container_window_count(definition->progress_count, first);
    memset(container->view, 0, sizeof(container->view));
    memset(container->progress, 0, sizeof(container->progress));
    return container_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_SET);
}
maage_status maage_container_sample(const maage_container_registry *registry, const maage_world *world, maage_crafting_container *container) {
    const maage_container_definition *definition = container ? maage_container_get(registry, container->kind) : NULL;
    const maage_container_slot_definition *slot;
    maage_status result;
    unsigned long identity, first, block_identity, secondary_block_identity;
    unsigned int i, width;
    int x, y, z, secondary_x, secondary_y, secondary_z;
    if (!container || !world) return container_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_SNAPSHOT);
    if (!container->kind) return container_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_SNAPSHOT);
    if (!definition || !container_window_valid(definition, container)) return container_status(MAAGE_STATUS_INVARIANT_VIOLATION, MAAGE_OPERATION_SNAPSHOT);
    identity = container->identity;
    first = container->view_first;
    block_identity = container->block_identity;
    secondary_block_identity = container->secondary_block_identity;
    secondary_x = container->secondary_x;
    secondary_y = container->secondary_y;
    secondary_z = container->secondary_z;
    width = container->grid_width;
    x = container->x;
    y = container->y;
    z = container->z;
    memset(container->view, 0, sizeof(container->view));
    memset(container->progress, 0, sizeof(container->progress));
    if (definition->sample) {
        result = definition->sample(definition->context, world, container);
        if (maage_status_failed(result)) return result;
    }
    if (container->kind != definition->id || container->identity != identity || container->view_first != first || container->block_identity != block_identity || container->secondary_block_identity != secondary_block_identity || container->secondary_x != secondary_x || container->secondary_y != secondary_y || container->secondary_z != secondary_z || container->grid_width != width || container->x != x || container->y != y || container->z != z || !container->revision || container->revision > 4294967295UL || !container_window_valid(definition, container)) return container_status(MAAGE_STATUS_INVARIANT_VIOLATION, MAAGE_OPERATION_SNAPSHOT);
    for (i = 0; i < container->view_count; ++i) {
        slot = &definition->slots[container->view_first + i];
        if (slot->storage == MAAGE_CONTAINER_SLOT_INPUT) container->view[i] = container->inputs[slot->slot];
        else if (slot->storage == MAAGE_CONTAINER_SLOT_OUTPUT) container->view[i] = container->output;
        if (!maage_item_stack_valid(maage_world_item_registry(world), &container->view[i])) return container_status(MAAGE_STATUS_INVARIANT_VIOLATION, MAAGE_OPERATION_SNAPSHOT);
    }
    for (i = 0; i < container->progress_count; ++i)
        if (container->progress[i].value > container->progress[i].total || container->progress[i].total > 4294967295UL || (container->progress[i].active != 0 && container->progress[i].active != 1)) return container_status(MAAGE_STATUS_INVARIANT_VIOLATION, MAAGE_OPERATION_SNAPSHOT);
    return container_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_SNAPSHOT);
}
int maage_container_accessible(const maage_container_registry *registry, const maage_crafting_container *container, const maage_player *player, const maage_world *world) {
    const maage_container_definition *definition = container ? maage_container_get(registry, container->kind) : NULL;
    double dx, dy, dz;
    if (!definition || !player || !world || container->closing) return 0;
    if (definition->personal) return !definition->accessible || definition->accessible(definition->context, world, container);
    if (!maage_world_chunk_loaded(world, maage_world_chunk_coordinate(container->x), maage_world_chunk_coordinate(container->z))) return 0;
    if (!definition->accessible || !definition->accessible(definition->context, world, container)) return 0;
    dx = player->position[0] - (container->x + 0.5);
    dy = player->position[1] - (container->y + 0.5);
    dz = player->position[2] - (container->z + 0.5);
    return dx * dx + dy * dy + dz * dz <= 64.0;
}
maage_status maage_container_action(const maage_container_registry *registry, maage_world *world, maage_gameplay *game, const maage_player *player, const maage_recipe_registry *recipes, unsigned long revision, unsigned int group, unsigned int index, unsigned int action) {
    const maage_container_definition *definition = game ? maage_container_get(registry, game->crafting.kind) : NULL;
    const maage_container_slot_definition *slot = NULL;
    const maage_item_stack *source, *cursor;
    maage_crafting_container observed, before;
    maage_status result;
    size_t i;
    if (!definition || !world || !game || !game->health || !container_window_valid(definition, &game->crafting) || action < MAAGE_SLOT_PRIMARY || action > MAAGE_SLOT_QUICK_MOVE || !maage_container_accessible(registry, &game->crafting, player, world)) return container_status(MAAGE_STATUS_ACCESS_DENIED, MAAGE_OPERATION_VALIDATE);
    if (group == MAAGE_SLOT_INVENTORY) {
        if (!definition->presentation.show_inventory || index >= MAAGE_INVENTORY_SLOTS) return container_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_VALIDATE);
    } else {
        for (i = game->crafting.view_first; i - game->crafting.view_first < game->crafting.view_count; ++i)
            if (definition->slots[i].group == group && definition->slots[i].slot == index) {
                slot = &definition->slots[i];
                break;
            }
        if (!slot) return container_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_VALIDATE);
        if (i < game->crafting.view_first || i - game->crafting.view_first >= game->crafting.view_count) return maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_VALIDATE, MAAGE_DETAIL_STALE);
        observed = game->crafting;
        result = maage_container_sample(registry, world, &observed);
        if (maage_status_failed(result)) return result;
        if ((slot->flags & MAAGE_CONTAINER_SLOT_REVISION) && revision != observed.revision) return maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_VALIDATE, MAAGE_DETAIL_STALE);
        source = &observed.view[i - observed.view_first];
        cursor = &game->inventory.cursor;
        if (!(slot->flags & MAAGE_CONTAINER_SLOT_EXTRACT) && (!cursor->count || action == MAAGE_SLOT_QUICK_MOVE || (source->count && (source->item_id != cursor->item_id || source->metadata != cursor->metadata || source->damage != cursor->damage)))) return container_status(MAAGE_STATUS_ACCESS_DENIED, MAAGE_OPERATION_MUTATE);
        if (!(slot->flags & MAAGE_CONTAINER_SLOT_INSERT) && cursor->count && (action != MAAGE_SLOT_QUICK_MOVE) && (!source->count || source->item_id != cursor->item_id || source->metadata != cursor->metadata || source->damage != cursor->damage)) return container_status(MAAGE_STATUS_ACCESS_DENIED, MAAGE_OPERATION_MUTATE);
        if (!(slot->flags & (MAAGE_CONTAINER_SLOT_INSERT | MAAGE_CONTAINER_SLOT_EXTRACT))) return container_status(MAAGE_STATUS_ACCESS_DENIED, MAAGE_OPERATION_MUTATE);
        if (definition->policy) {
            result = definition->policy(definition->context, world, game, slot, action);
            if (maage_status_failed(result) || maage_status_code(result) == MAAGE_STATUS_NOOP) return result;
        }
    }
    before = game->crafting;
    if (definition->action) result = definition->action(definition->context, world, game, revision, group, index, action);
    else result = maage_crafting_click(&game->crafting, &game->inventory, maage_world_item_registry(world), recipes, revision, group, index, action);
    if (game->crafting.identity != before.identity || game->crafting.kind != before.kind || game->crafting.block_identity != before.block_identity || game->crafting.secondary_block_identity != before.secondary_block_identity || game->crafting.secondary_x != before.secondary_x || game->crafting.secondary_y != before.secondary_y || game->crafting.secondary_z != before.secondary_z || game->crafting.x != before.x || game->crafting.y != before.y || game->crafting.z != before.z || game->crafting.grid_width != before.grid_width || game->crafting.closing != before.closing || game->crafting.view_first != before.view_first || game->crafting.revision < before.revision || game->crafting.revision > 4294967295UL || !container_window_valid(definition, &game->crafting)) return container_status(MAAGE_STATUS_INVARIANT_VIOLATION, MAAGE_OPERATION_MUTATE);
    return result;
}
maage_status maage_container_close(const maage_container_registry *registry, maage_world *world, maage_gameplay *game, const maage_player *player) {
    const maage_container_definition *definition = game ? maage_container_get(registry, game->crafting.kind) : NULL;
    maage_status result;
    if (!game || !world || !player) return container_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_CLOSE);
    if (game->crafting.kind && !definition) return container_status(MAAGE_STATUS_INVARIANT_VIOLATION, MAAGE_OPERATION_CLOSE);
    if (definition && definition->closed) {
        game->crafting.closing = 1;
        result = definition->closed(definition->context, world, game, player);
        game->crafting.closing = 1;
        if (maage_status_failed(result)) return result;
    }
    result = maage_crafting_close(&game->crafting, &game->inventory, maage_world_item_registry(world), player, world);
    if (!maage_status_failed(result)) {
        game->crafting.view_first = game->crafting.view_count = game->crafting.progress_count = 0;
        memset(game->crafting.view, 0, sizeof(game->crafting.view));
        memset(game->crafting.progress, 0, sizeof(game->crafting.progress));
    }
    return result;
}

void content_containers_policy(maage_container_registry *registry, content_policy *policy) {
    registry->policy = policy;
}
