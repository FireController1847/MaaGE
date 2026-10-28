#include "maage/crafting.h"
#include <stdlib.h>
#include <string.h>

typedef struct recipe_entry {
    maage_recipe_definition definition;
    maage_recipe_id id;
    unsigned int size;
} recipe_entry;
struct maage_recipe_registry {
    const maage_item_registry *items;
    recipe_entry *entries;
    size_t count, capacity;
    int finalized;
};
static maage_status recipe_status(unsigned char code, unsigned char operation)
{
    return maage_status_make(code, MAAGE_CONTEXT_REGISTRY, operation, 0);
}
static void recipe_clear(maage_recipe_definition *recipe)
{
    unsigned int i;
    for (i = 0; i < MAAGE_CRAFTING_CELLS; ++i)
        free((void *)recipe->ingredients[i].choices);
}
maage_status maage_recipe_registry_create(const maage_item_registry *items,
    maage_recipe_registry **output)
{
    if (!output) return recipe_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_CREATE);
    *output = NULL;
    if (!maage_item_registry_is_finalized(items))
        return recipe_status(MAAGE_STATUS_INVALID_STATE, MAAGE_OPERATION_CREATE);
    *output = (maage_recipe_registry *)calloc(1, sizeof(**output));
    if (!*output) return recipe_status(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_CREATE);
    (*output)->items = items;
    return recipe_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_CREATE);
}
maage_status maage_recipe_registry_destroy(maage_recipe_registry *registry)
{
    size_t i;
    if (!registry) return recipe_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_DESTROY);
    for (i = 0; i < registry->count; ++i) recipe_clear(&registry->entries[i].definition);
    free(registry->entries); free(registry);
    return recipe_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_DESTROY);
}
maage_status maage_recipe_registry_register(maage_recipe_registry *registry,
    const maage_recipe_definition *definition, maage_recipe_id *id)
{
    recipe_entry entry, *grown;
    maage_recipe_ingredient *ingredient;
    maage_recipe_choice *choices;
    const maage_item_definition *item;
    unsigned int i, cells, size = 0;
    size_t j, capacity;
    if (id) *id = 0;
    if (!registry || !definition) return recipe_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_REGISTER);
    if (registry->finalized) return recipe_status(MAAGE_STATUS_INVALID_STATE, MAAGE_OPERATION_REGISTER);
    if (definition->kind == MAAGE_RECIPE_SHAPED) {
        if (!definition->width || definition->width > 3 || !definition->height || definition->height > 3)
            return recipe_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_REGISTER);
        cells = definition->width * definition->height;
    } else if (definition->kind == MAAGE_RECIPE_SHAPELESS) cells = definition->ingredient_count;
    else return recipe_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_REGISTER);
    if (!cells || cells > MAAGE_CRAFTING_CELLS ||
        (definition->mirror != 0 && definition->mirror != 1) ||
        !definition->output.count || !maage_item_stack_valid(registry->items, &definition->output))
        return recipe_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_REGISTER);
    for (i = 0; i < MAAGE_CRAFTING_CELLS; ++i) {
        const maage_recipe_ingredient *source = &definition->ingredients[i];
        if (i >= cells || !source->choice_count) {
            if (source->choice_count || source->quantity || source->remainder.count ||
                (i < cells && definition->kind == MAAGE_RECIPE_SHAPELESS))
                return recipe_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_REGISTER);
            continue;
        }
        if (!source->choices || !source->quantity || source->quantity > 64 ||
            source->choice_count > (size_t)-1 / sizeof(*choices) ||
            !maage_item_stack_valid(registry->items, &source->remainder))
            return recipe_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_REGISTER);
        for (j = 0; j < source->choice_count; ++j) {
            item = maage_item_registry_get(registry->items, source->choices[j].item);
            if (!item || source->quantity > item->max_stack ||
                (source->choices[j].metadata != MAAGE_RECIPE_METADATA_ANY &&
                 source->choices[j].metadata > item->max_metadata))
                return recipe_status(MAAGE_STATUS_NOT_FOUND, MAAGE_OPERATION_REGISTER);
        }
        ++size;
    }
    if (!size) return recipe_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_REGISTER);
    if (registry->count >= 4294967295UL) return recipe_status(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_OPERATION_REGISTER);
    if (registry->count == registry->capacity) {
        capacity = registry->capacity ? registry->capacity * 2 : 32;
        if (capacity < registry->capacity || capacity > (size_t)-1 / sizeof(*grown))
            return recipe_status(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_OPERATION_REGISTER);
        grown = (recipe_entry *)realloc(registry->entries, capacity * sizeof(*grown));
        if (!grown) return recipe_status(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_REGISTER);
        registry->entries = grown; registry->capacity = capacity;
    }
    memset(&entry, 0, sizeof(entry));
    entry.definition = *definition;
    for (i = 0; i < MAAGE_CRAFTING_CELLS; ++i) entry.definition.ingredients[i].choices = NULL;
    for (i = 0; i < cells; ++i) {
        ingredient = &entry.definition.ingredients[i];
        if (!ingredient->choice_count) continue;
        choices = (maage_recipe_choice *)malloc(ingredient->choice_count * sizeof(*choices));
        if (!choices) { recipe_clear(&entry.definition); return recipe_status(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_REGISTER); }
        memcpy(choices, definition->ingredients[i].choices, ingredient->choice_count * sizeof(*choices));
        ingredient->choices = choices;
    }
    entry.id = (maage_recipe_id)registry->count + 1; entry.size = size;
    registry->entries[registry->count++] = entry;
    if (id) *id = entry.id;
    return recipe_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_REGISTER);
}
static int recipe_compare(const void *left, const void *right)
{
    const recipe_entry *a = (const recipe_entry *)left, *b = (const recipe_entry *)right;
    if (a->definition.kind != b->definition.kind) return a->definition.kind < b->definition.kind ? -1 : 1;
    if (a->size != b->size) return a->size > b->size ? -1 : 1;
    return a->id < b->id ? -1 : (a->id != b->id);
}
maage_status maage_recipe_registry_finalize(maage_recipe_registry *registry)
{
    if (!registry || registry->finalized) return recipe_status(MAAGE_STATUS_INVALID_STATE, MAAGE_OPERATION_FINALIZE);
    if (registry->count) qsort(registry->entries, registry->count, sizeof(*registry->entries), recipe_compare);
    registry->finalized = 1;
    return recipe_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_FINALIZE);
}
size_t maage_recipe_registry_count(const maage_recipe_registry *registry)
{
    return registry ? registry->count : 0;
}
static int ingredient_matches(const maage_recipe_ingredient *ingredient, const maage_item_stack *stack)
{
    size_t i;
    if (!ingredient->choice_count) return stack->count == 0;
    if (stack->count < ingredient->quantity || stack->damage) return 0;
    for (i = 0; i < ingredient->choice_count; ++i)
        if (stack->item_id == ingredient->choices[i].item &&
            (ingredient->choices[i].metadata == MAAGE_RECIPE_METADATA_ANY ||
             stack->metadata == ingredient->choices[i].metadata)) return 1;
    return 0;
}
static int shaped_match(const maage_recipe_definition *recipe, const maage_item_stack *grid,
    unsigned int width, unsigned int ox, unsigned int oy, int mirror, int mapping[9])
{
    unsigned int x, y, rx, ry, cell;
    int ingredient;
    for (y = 0; y < width; ++y) for (x = 0; x < width; ++x) {
        cell = y * width + x; ingredient = -1;
        if (x >= ox && y >= oy && x - ox < recipe->width && y - oy < recipe->height) {
            rx = x - ox; ry = y - oy;
            if (mirror) rx = recipe->width - 1 - rx;
            ingredient = (int)(ry * recipe->width + rx);
        }
        mapping[cell] = ingredient;
        if (ingredient < 0 ? grid[cell].count != 0 :
            !ingredient_matches(&recipe->ingredients[ingredient], &grid[cell])) return 0;
    }
    return 1;
}
/* Bounded augmenting paths give a complete bipartite matching, including
 * duplicate and overlapping alternatives. Each ingredient owns one cell. */
static int shapeless_assign(const maage_recipe_definition *recipe, const maage_item_stack *grid,
    unsigned int cells, int ingredient, int mapping[9], int seen[9])
{
    unsigned int cell;
    for (cell = 0; cell < cells; ++cell) {
        if (seen[cell] || !grid[cell].count ||
            !ingredient_matches(&recipe->ingredients[ingredient], &grid[cell])) continue;
        seen[cell] = 1;
        if (mapping[cell] < 0 || shapeless_assign(recipe, grid, cells, mapping[cell], mapping, seen)) {
            mapping[cell] = ingredient; return 1;
        }
    }
    return 0;
}
maage_status maage_recipe_find(const maage_recipe_registry *registry,
    const maage_item_stack *grid, unsigned int width, maage_recipe_match *match)
{
    const recipe_entry *entry;
    const maage_recipe_definition *recipe;
    const maage_recipe_ingredient *ingredient;
    size_t index;
    unsigned int i, ox, oy, occupied = 0;
    int mapping[9], seen[9], found;
    if (!match) return recipe_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_RESOLVE);
    memset(match, 0, sizeof(*match));
    if (!registry || !registry->finalized || !grid || (width != 2 && width != 3))
        return recipe_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_RESOLVE);
    for (i = 0; i < width * width; ++i) {
        if (!maage_item_stack_valid(registry->items, &grid[i]))
            return recipe_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_RESOLVE);
        if (grid[i].count) ++occupied;
    }
    for (index = 0; index < registry->count; ++index) {
        entry = &registry->entries[index]; recipe = &entry->definition;
        if (entry->size != occupied) continue;
        found = 0;
        if (recipe->kind == MAAGE_RECIPE_SHAPED) {
            if (recipe->width > width || recipe->height > width) continue;
            for (oy = 0; oy <= width - recipe->height && !found; ++oy)
                for (ox = 0; ox <= width - recipe->width && !found; ++ox) {
                    found = shaped_match(recipe, grid, width, ox, oy, 0, mapping);
                    if (!found && recipe->mirror) found = shaped_match(recipe, grid, width, ox, oy, 1, mapping);
                }
        } else {
            for (i = 0; i < width * width; ++i) mapping[i] = -1;
            found = 1;
            for (i = 0; i < recipe->ingredient_count && found; ++i) {
                memset(seen, 0, sizeof(seen));
                found = shapeless_assign(recipe, grid, width * width, (int)i, mapping, seen);
            }
        }
        if (!found) continue;
        match->recipe = entry->id; match->output = recipe->output;
        for (i = 0; i < width * width; ++i) if (mapping[i] >= 0) {
            ingredient = &recipe->ingredients[mapping[i]];
            match->consume[i] = ingredient->quantity;
            match->remainders[i] = ingredient->remainder;
        }
        return recipe_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_RESOLVE);
    }
    return recipe_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_RESOLVE);
}
