#include "maage/crafting.h"
#include "registry_policy.h"
#include <stdlib.h>
#include <string.h>

typedef struct recipe_projection_entry {
    maage_item_id item;
    maage_item_variant_id variant;
    maage_item_stack output;
} recipe_projection_entry;
typedef struct recipe_entry {
    maage_recipe_definition definition;
    maage_recipe_id id;
    unsigned int size;
    recipe_projection_entry *projection;
    size_t projection_count;
} recipe_entry;
struct maage_recipe_registry {
    const maage_item_registry *items;
    recipe_entry *entries;
    size_t count, capacity;
    int finalized;
};
static maage_status recipe_status(unsigned char code, unsigned char operation) {
    return maage_status_make(code, MAAGE_CONTEXT_REGISTRY, operation, 0);
}
static maage_status recipe_read_tag(const maage_traits_registry *traits, maage_tag_id tag) {
    maage_tag_id resolved;
    const char *name = maage_tag_name(traits, tag);
    if (!name) return recipe_status(MAAGE_STATUS_NOT_FOUND, MAAGE_OPERATION_REGISTER);
    return maage_tag_find(traits, name, &resolved);
}
static maage_status recipe_read_property(const maage_traits_registry *traits, maage_property_id property) {
    maage_property_id resolved;
    const char *name = maage_property_name(traits, property);
    if (!name) return recipe_status(MAAGE_STATUS_NOT_FOUND, MAAGE_OPERATION_REGISTER);
    return maage_property_find(traits, name, &resolved);
}
static maage_status recipe_read_item(const maage_item_registry *items, maage_item_id item) {
    maage_item_id resolved;
    const char *name = maage_item_registry_name(items, item);
    if (!name) return recipe_status(MAAGE_STATUS_NOT_FOUND, MAAGE_OPERATION_REGISTER);
    return maage_item_registry_resolve_name(items, name, &resolved);
}
static void recipe_clear(maage_recipe_definition *recipe) {
    unsigned int i;
    for (i = 0; i < MAAGE_CRAFTING_CELLS; ++i)
        free((void *) recipe->ingredients[i].choices);
    free((void *) recipe->projection.properties);
}
maage_status maage_recipe_registry_create(const maage_item_registry *items, maage_recipe_registry **output) {
    if (!output) return recipe_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_CREATE);
    *output = NULL;
    if (!items) return recipe_status(MAAGE_STATUS_INVALID_STATE, MAAGE_OPERATION_CREATE);
    *output = (maage_recipe_registry *) calloc(1, sizeof(**output));
    if (!*output) return recipe_status(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_CREATE);
    (*output)->items = items;
    return recipe_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_CREATE);
}
maage_status maage_recipe_registry_destroy(maage_recipe_registry *registry) {
    size_t i;
    if (!registry) return recipe_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_DESTROY);
    for (i = 0; i < registry->count; ++i) {
        recipe_clear(&registry->entries[i].definition);
        free(registry->entries[i].projection);
    }
    free(registry->entries);
    free(registry);
    return recipe_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_DESTROY);
}
maage_status maage_recipe_registry_register(maage_recipe_registry *registry, const maage_recipe_definition *definition, maage_recipe_id *id) {
    recipe_entry entry, *grown;
    maage_status result;
    const maage_traits_registry *traits;
    maage_recipe_ingredient *ingredient;
    maage_recipe_choice *choices;
    const maage_item_definition *item;
    unsigned int i, cells, size = 0;
    size_t j, capacity;
    if (id) *id = 0;
    if (!registry || !definition) return recipe_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_REGISTER);
    if (registry->finalized) return recipe_status(MAAGE_STATUS_INVALID_STATE, MAAGE_OPERATION_REGISTER);
    traits = maage_item_registry_traits(registry->items);
    if (definition->kind == MAAGE_RECIPE_SHAPED) {
        if (!definition->width || definition->width > 3 || !definition->height || definition->height > 3) return recipe_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_REGISTER);
        cells = definition->width * definition->height;
    } else if (definition->kind == MAAGE_RECIPE_SHAPELESS) cells = definition->ingredient_count;
    else
        return recipe_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_REGISTER);
    if (!cells || cells > MAAGE_CRAFTING_CELLS ||
        (definition->mirror != 0 && definition->mirror != 1) ||
        !definition->output.count || (!definition->projection.property_count && !maage_item_stack_valid(registry->items, &definition->output)))
        return recipe_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_REGISTER);
    if (definition->projection.property_count && (!definition->projection.properties || definition->projection.property_count > (size_t) - 1 / sizeof(*definition->projection.properties) || definition->projection.ingredient >= cells || !definition->ingredients[definition->projection.ingredient].choice_count)) return recipe_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_REGISTER);
    if (definition->projection.property_count) {
        result = recipe_read_tag(traits, definition->projection.output_tag);
        if (maage_status_failed(result)) return result;
        for (j = 0; j < definition->projection.property_count; ++j) {
            result = recipe_read_property(traits, definition->projection.properties[j].source);
            if (maage_status_failed(result)) return result;
            result = recipe_read_property(traits, definition->projection.properties[j].target);
            if (maage_status_failed(result)) return result;
        }
    } else {
        result = recipe_read_item(registry->items, definition->output.item_id);
        if (maage_status_failed(result)) return result;
    }
    for (i = 0; i < MAAGE_CRAFTING_CELLS; ++i) {
        const maage_recipe_ingredient *source = &definition->ingredients[i];
        if (i >= cells || !source->choice_count) {
            if (source->choice_count || source->quantity || source->remainder.count || (i < cells && definition->kind == MAAGE_RECIPE_SHAPELESS)) return recipe_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_REGISTER);
            continue;
        }
        if (!source->choices || !source->quantity || source->quantity > 64 ||
            source->choice_count > (size_t) - 1 / sizeof(*choices) ||
            !maage_item_stack_valid(registry->items, &source->remainder))
            return recipe_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_REGISTER);
        if (source->remainder.count) {
            result = recipe_read_item(registry->items, source->remainder.item_id);
            if (maage_status_failed(result)) return result;
        }
        for (j = 0; j < source->choice_count; ++j) {
            const maage_recipe_choice *choice = &source->choices[j];
            const maage_tag_definition *tag = maage_tag_get(maage_item_registry_traits(registry->items), choice->tag);
            item = maage_item_registry_get(registry->items, choice->item);
            if (choice->kind > MAAGE_RECIPE_CHOICE_TAG || (choice->kind == MAAGE_RECIPE_CHOICE_ITEM && (!item || source->quantity > item->max_stack || (choice->variant != MAAGE_RECIPE_VARIANT_ANY && choice->variant >= item->variant_count))) || (choice->kind == MAAGE_RECIPE_CHOICE_TAG && (!tag || !(tag->domains & MAAGE_TRAIT_ITEM))) || !maage_property_set_get(maage_item_registry_traits(registry->items), choice->properties)) return recipe_status(MAAGE_STATUS_NOT_FOUND, MAAGE_OPERATION_REGISTER);
            result = choice->kind == MAAGE_RECIPE_CHOICE_TAG ? recipe_read_tag(traits, choice->tag) : recipe_read_item(registry->items, choice->item);
            if (maage_status_failed(result)) return result;
            result = content_traits_read_set(traits, choice->properties);
            if (maage_status_failed(result)) return result;
        }
        ++size;
    }
    if (!size) return recipe_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_REGISTER);
    if (registry->count >= MAAGE_CONTENT_ID_INVALID - 1UL) return recipe_status(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_OPERATION_REGISTER);
    if (registry->count == registry->capacity) {
        capacity = registry->capacity ? registry->capacity * 2 : 32;
        if (capacity < registry->capacity || capacity > (size_t) - 1 / sizeof(*grown)) return recipe_status(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_OPERATION_REGISTER);
        grown = (recipe_entry *) realloc(registry->entries, capacity * sizeof(*grown));
        if (!grown) return recipe_status(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_REGISTER);
        registry->entries = grown;
        registry->capacity = capacity;
    }
    memset(&entry, 0, sizeof(entry));
    entry.definition = *definition;
    entry.definition.projection.properties = NULL;
    for (i = 0; i < MAAGE_CRAFTING_CELLS; ++i)
        entry.definition.ingredients[i].choices = NULL;
    for (i = 0; i < cells; ++i) {
        ingredient = &entry.definition.ingredients[i];
        if (!ingredient->choice_count) continue;
        choices = (maage_recipe_choice *) malloc(ingredient->choice_count * sizeof(*choices));
        if (!choices) {
            recipe_clear(&entry.definition);
            return recipe_status(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_REGISTER);
        }
        memcpy(choices, definition->ingredients[i].choices, ingredient->choice_count * sizeof(*choices));
        ingredient->choices = choices;
    }
    if (definition->projection.property_count) {
        maage_recipe_property_binding *bindings = (maage_recipe_property_binding *) malloc(definition->projection.property_count * sizeof(*bindings));
        if (!bindings) {
            recipe_clear(&entry.definition);
            return recipe_status(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_REGISTER);
        }
        memcpy(bindings, definition->projection.properties, definition->projection.property_count * sizeof(*bindings));
        entry.definition.projection.properties = bindings;
    }
    entry.id = (maage_recipe_id) registry->count + 1;
    entry.size = size;
    registry->entries[registry->count++] = entry;
    if (id) *id = entry.id;
    return recipe_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_REGISTER);
}
static int recipe_compare(const void *left, const void *right) {
    const recipe_entry *a = (const recipe_entry *) left, *b = (const recipe_entry *) right;
    if (a->definition.kind != b->definition.kind) return a->definition.kind < b->definition.kind ? -1 : 1;
    if (a->size != b->size) return a->size > b->size ? -1 : 1;
    return a->id < b->id ? -1 : (a->id != b->id);
}
static int ingredient_matches(const maage_item_registry *, const maage_recipe_ingredient *, const maage_item_stack *);
static int projection_compare(const void *left, const void *right) {
    const recipe_projection_entry *a = (const recipe_projection_entry *) left, *b = (const recipe_projection_entry *) right;
    if (a->item != b->item) return a->item < b->item ? -1 : 1;
    return a->variant < b->variant ? -1 : (a->variant != b->variant);
}
static maage_status recipe_compile_projection(maage_recipe_registry *registry, recipe_entry *entry) {
    const maage_recipe_projection *projection = &entry->definition.projection;
    const maage_recipe_ingredient *ingredient;
    const maage_item_definition *input, *output;
    const maage_property_definition *source_property, *target_property;
    const maage_tag_definition *tag;
    const maage_traits_registry *traits = maage_item_registry_traits(registry->items);
    recipe_projection_entry *grown;
    maage_property_value source, target;
    maage_item_stack stack, resolved;
    size_t i, j, k, v, out_variant, matches, capacity = 0;
    if (!projection->property_count) return recipe_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_FINALIZE);
    free(entry->projection);
    entry->projection = NULL;
    entry->projection_count = 0;
    tag = maage_tag_get(traits, projection->output_tag);
    if (!tag || !(tag->domains & MAAGE_TRAIT_ITEM)) return recipe_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_FINALIZE);
    for (k = 0; k < projection->property_count; ++k) {
        source_property = maage_property_get(traits, projection->properties[k].source);
        target_property = maage_property_get(traits, projection->properties[k].target);
        if (!source_property || !target_property || source_property->kind != target_property->kind || (source_property->kind == MAAGE_PROPERTY_IDENTITY && source_property->id != target_property->id)) return recipe_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_FINALIZE);
    }
    ingredient = &entry->definition.ingredients[projection->ingredient];
    for (i = 0; i < maage_item_registry_count(registry->items); ++i) {
        input = maage_item_registry_at(registry->items, i);
        memset(&stack, 0, sizeof(stack));
        stack.item_id = input->id;
        stack.count = ingredient->quantity;
        for (v = 0; v < input->variant_count; ++v) {
            stack.variant = (maage_item_variant_id) v;
            if (!ingredient_matches(registry->items, ingredient, &stack)) continue;
            if (stack.count > input->max_stack) return recipe_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_FINALIZE);
            memset(&resolved, 0, sizeof(resolved));
            matches = 0;
            for (j = 0; j < maage_item_registry_count(registry->items); ++j) {
                output = maage_item_registry_at(registry->items, j);
                if (!maage_traits_has(traits, MAAGE_TRAIT_ITEM, output->id, projection->output_tag)) continue;
                for (out_variant = 0; out_variant < output->variant_count; ++out_variant) {
                    for (k = 0; k < projection->property_count; ++k)
                        if (!maage_item_variant_value(registry->items, input->id, (maage_item_variant_id) v, projection->properties[k].source, &source) || !maage_item_variant_value(registry->items, output->id, (maage_item_variant_id) out_variant, projection->properties[k].target, &target) || source != target) break;
                    if (k != projection->property_count) continue;
                    ++matches;
                    resolved = entry->definition.output;
                    resolved.item_id = output->id;
                    resolved.variant = (maage_item_variant_id) out_variant;
                }
            }
            if (matches != 1) return content_items_failure(registry->items, recipe_status(MAAGE_STATUS_INVALID_STATE, MAAGE_OPERATION_FINALIZE), maage_item_registry_name(registry->items, input->id), matches ? "Recipe property projection has multiple matching output variants" : "Recipe property projection has no matching output variant");
            if (!maage_item_stack_valid(registry->items, &resolved)) return content_items_failure(registry->items, recipe_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_FINALIZE), maage_item_registry_name(registry->items, resolved.item_id), "Recipe projected output stack is invalid");
            if (entry->projection_count == capacity) {
                capacity = capacity ? capacity * 2 : 16;
                if (capacity < entry->projection_count || capacity > (size_t) - 1 / sizeof(*grown)) return recipe_status(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_OPERATION_FINALIZE);
                grown = (recipe_projection_entry *) realloc(entry->projection, capacity * sizeof(*grown));
                if (!grown) return recipe_status(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_FINALIZE);
                entry->projection = grown;
            }
            entry->projection[entry->projection_count].item = input->id;
            entry->projection[entry->projection_count].variant = (maage_item_variant_id) v;
            entry->projection[entry->projection_count++].output = resolved;
        }
    }
    if (!entry->projection_count) return content_items_failure(registry->items, recipe_status(MAAGE_STATUS_INVALID_STATE, MAAGE_OPERATION_FINALIZE), maage_tag_name(traits, projection->output_tag), "Recipe property projection has no eligible input variants");
    qsort(entry->projection, entry->projection_count, sizeof(*entry->projection), projection_compare);
    return recipe_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_FINALIZE);
}
maage_status maage_recipe_registry_finalize(maage_recipe_registry *registry) {
    maage_status result;
    size_t i;
    if (!registry || registry->finalized || !maage_item_registry_is_finalized(registry->items)) return recipe_status(MAAGE_STATUS_INVALID_STATE, MAAGE_OPERATION_FINALIZE);
    for (i = 0; i < registry->count; ++i) {
        result = recipe_compile_projection(registry, &registry->entries[i]);
        if (maage_status_failed(result)) return result;
    }
    if (registry->count) qsort(registry->entries, registry->count, sizeof(*registry->entries), recipe_compare);
    registry->finalized = 1;
    return recipe_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_FINALIZE);
}
size_t maage_recipe_registry_count(const maage_recipe_registry *registry) {
    return registry ? registry->count : 0;
}
static int choice_matches(const maage_item_registry *items, const maage_recipe_choice *choice, const maage_item_stack *stack) {
    const maage_traits_registry *traits = maage_item_registry_traits(items);
    const maage_property_set *constraints = maage_property_set_get(traits, choice->properties);
    maage_property_value value;
    size_t i;
    if (choice->kind == MAAGE_RECIPE_CHOICE_ITEM ? stack->item_id != choice->item : !maage_traits_has(traits, MAAGE_TRAIT_ITEM, stack->item_id, choice->tag)) return 0;
    if (choice->variant != MAAGE_RECIPE_VARIANT_ANY && stack->variant != choice->variant) return 0;
    if (!constraints) return 0;
    for (i = 0; i < constraints->count; ++i)
        if (!maage_item_variant_value(items, stack->item_id, stack->variant, constraints->values[i].property, &value) || value != constraints->values[i].value) return 0;
    return 1;
}
static int ingredient_matches(const maage_item_registry *items, const maage_recipe_ingredient *ingredient, const maage_item_stack *stack) {
    size_t i;
    if (!ingredient->choice_count) return stack->count == 0;
    if (stack->count < ingredient->quantity || stack->damage) return 0;
    for (i = 0; i < ingredient->choice_count; ++i)
        if (choice_matches(items, &ingredient->choices[i], stack)) return 1;
    return 0;
}
static int shaped_match(const maage_item_registry *items, const maage_recipe_definition *recipe, const maage_item_stack *grid, unsigned int width, unsigned int ox, unsigned int oy, int mirror, int mapping[9]) {
    unsigned int x, y, rx, ry, cell;
    int ingredient;
    for (y = 0; y < width; ++y)
        for (x = 0; x < width; ++x) {
            cell = y * width + x;
            ingredient = -1;
            if (x >= ox && y >= oy && x - ox < recipe->width && y - oy < recipe->height) {
                rx = x - ox;
                ry = y - oy;
                if (mirror) rx = recipe->width - 1 - rx;
                ingredient = (int) (ry * recipe->width + rx);
            }
            mapping[cell] = ingredient;
            if (ingredient < 0 ? grid[cell].count != 0 : !ingredient_matches(items, &recipe->ingredients[ingredient], &grid[cell])) return 0;
        }
    return 1;
}
/* Bounded augmenting paths give a complete bipartite matching, including
 * duplicate and overlapping alternatives. Each ingredient owns one cell. */
static int shapeless_assign(const maage_item_registry *items, const maage_recipe_definition *recipe, const maage_item_stack *grid, unsigned int cells, int ingredient, int mapping[9], int seen[9]) {
    unsigned int cell;
    for (cell = 0; cell < cells; ++cell) {
        if (seen[cell] || !grid[cell].count || !ingredient_matches(items, &recipe->ingredients[ingredient], &grid[cell])) continue;
        seen[cell] = 1;
        if (mapping[cell] < 0 || shapeless_assign(items, recipe, grid, cells, mapping[cell], mapping, seen)) {
            mapping[cell] = ingredient;
            return 1;
        }
    }
    return 0;
}
maage_status maage_recipe_find(const maage_recipe_registry *registry, const maage_item_stack *grid, unsigned int width, maage_recipe_match *match) {
    const recipe_entry *entry;
    const maage_recipe_definition *recipe;
    const maage_recipe_ingredient *ingredient;
    size_t index, low, high, middle;
    recipe_projection_entry key;
    unsigned int i, ox, oy, occupied = 0;
    int mapping[9], seen[9], found;
    if (!match) return recipe_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_RESOLVE);
    memset(match, 0, sizeof(*match));
    if (!registry || !registry->finalized || !grid || (width != 2 && width != 3)) return recipe_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_RESOLVE);
    for (i = 0; i < width * width; ++i) {
        if (!maage_item_stack_valid(registry->items, &grid[i])) return recipe_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_RESOLVE);
        if (grid[i].count) ++occupied;
    }
    for (index = 0; index < registry->count; ++index) {
        entry = &registry->entries[index];
        recipe = &entry->definition;
        if (entry->size != occupied) continue;
        found = 0;
        if (recipe->kind == MAAGE_RECIPE_SHAPED) {
            if (recipe->width > width || recipe->height > width) continue;
            for (oy = 0; oy <= width - recipe->height && !found; ++oy)
                for (ox = 0; ox <= width - recipe->width && !found; ++ox) {
                    found = shaped_match(registry->items, recipe, grid, width, ox, oy, 0, mapping);
                    if (!found && recipe->mirror) found = shaped_match(registry->items, recipe, grid, width, ox, oy, 1, mapping);
                }
        } else {
            for (i = 0; i < width * width; ++i)
                mapping[i] = -1;
            found = 1;
            for (i = 0; i < recipe->ingredient_count && found; ++i) {
                memset(seen, 0, sizeof(seen));
                found = shapeless_assign(registry->items, recipe, grid, width * width, (int) i, mapping, seen);
            }
        }
        if (!found) continue;
        match->recipe = entry->id;
        match->output = recipe->output;
        if (recipe->projection.property_count) {
            for (i = 0; i < width * width; ++i)
                if (mapping[i] == (int) recipe->projection.ingredient) break;
            if (i == width * width) return recipe_status(MAAGE_STATUS_INVARIANT_VIOLATION, MAAGE_OPERATION_RESOLVE);
            memset(&key, 0, sizeof(key));
            key.item = grid[i].item_id;
            key.variant = grid[i].variant;
            low = 0;
            high = entry->projection_count;
            while (low < high) {
                middle = low + (high - low) / 2;
                if (projection_compare(&entry->projection[middle], &key) < 0) low = middle + 1;
                else high = middle;
            }
            if (low == entry->projection_count || projection_compare(&entry->projection[low], &key)) return recipe_status(MAAGE_STATUS_INVARIANT_VIOLATION, MAAGE_OPERATION_RESOLVE);
            match->output = entry->projection[low].output;
        }
        for (i = 0; i < width * width; ++i)
            if (mapping[i] >= 0) {
                ingredient = &recipe->ingredients[mapping[i]];
                match->consume[i] = ingredient->quantity;
                match->remainders[i] = ingredient->remainder;
            }
        return recipe_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_RESOLVE);
    }
    return recipe_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_RESOLVE);
}
