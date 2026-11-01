#include "maage/crafting.h"
#include <string.h>

/* Pattern symbols are registration-time data only. Every recipe is copied
 * into the same numeric registry before the Server starts. */
static maage_status builtin_recipe(maage_recipe_registry *registry, unsigned int kind, unsigned int width, unsigned int height, const char *pattern, const maage_recipe_choice choices[3], maage_item_id output, unsigned int count, unsigned int metadata) {
    maage_recipe_definition recipe;
    unsigned int i, symbol;
    memset(&recipe, 0, sizeof(recipe));
    recipe.kind = kind;
    recipe.width = width;
    recipe.height = height;
    recipe.mirror = 1;
    recipe.ingredient_count = width * height;
    recipe.output.item_id = output;
    recipe.output.count = count;
    recipe.output.metadata = metadata;
    for (i = 0; i < width * height; ++i) {
        if (pattern[i] == ' ') continue;
        symbol = (unsigned int) (pattern[i] - 'A');
        recipe.ingredients[i].choices = &choices[symbol];
        recipe.ingredients[i].choice_count = 1;
        recipe.ingredients[i].quantity = 1;
    }
    return maage_recipe_registry_register(registry, &recipe, NULL);
}
maage_status maage_recipe_registry_create_builtin(const maage_item_registry *items, maage_recipe_registry **output) {
    const maage_builtin_ids *ids = maage_item_registry_builtin_ids(items);
    maage_item_id materials[5];
    maage_item_id blocks[8];
    maage_item_id resources[8];
    static const char *const patterns[3] = {"AAA B  B ", "ABB", "AAAB B"};
    static const unsigned int widths[3] = {3, 1, 2};
    maage_recipe_choice choices[3];
    maage_recipe_registry *registry;
    maage_status result;
    unsigned int i, family;
    if (output) *output = NULL;
    if (!ids || !output) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_INITIALIZE, 0);
    resources[0] = ids->item_gold_ingot;
    resources[1] = ids->item_iron_ingot;
    resources[2] = ids->item_diamond;
    resources[3] = ids->item_lapis;
    resources[4] = ids->item_emerald;
    resources[5] = ids->item_redstone;
    resources[6] = ids->item_coal;
    resources[7] = ids->item_snowball;
    blocks[0] = ids->item_gold_block;
    blocks[1] = ids->item_iron_block;
    blocks[2] = ids->item_diamond_block;
    blocks[3] = ids->item_lapis_block;
    blocks[4] = ids->item_emerald_block;
    blocks[5] = ids->item_redstone_block;
    blocks[6] = ids->item_coal_block;
    blocks[7] = ids->item_snow_block;
    materials[0] = ids->item_planks;
    materials[1] = ids->item_cobblestone;
    materials[2] = ids->item_iron_ingot;
    materials[3] = ids->item_diamond;
    materials[4] = ids->item_gold_ingot;
    result = maage_recipe_registry_create(items, &registry);
    if (maage_status_failed(result)) return result;
    memset(choices, 0, sizeof(choices));
    for (i = 0; i < 4; ++i) {
        choices[0].item = ids->item_log;
        choices[0].metadata = i;
        result = builtin_recipe(registry, MAAGE_RECIPE_SHAPED, 1, 1, "A", choices, ids->item_planks, 4, i);
        if (maage_status_failed(result)) goto failed;
    }
    for (i = 0; i < 2; ++i) {
        choices[0].item = ids->item_log2;
        choices[0].metadata = i;
        result = builtin_recipe(registry, MAAGE_RECIPE_SHAPED, 1, 1, "A", choices, ids->item_planks, 4, i + 4);
        if (maage_status_failed(result)) goto failed;
    }
    choices[0].item = ids->item_planks;
    choices[0].metadata = MAAGE_RECIPE_METADATA_ANY;
    result = builtin_recipe(registry, MAAGE_RECIPE_SHAPED, 1, 2, "AA", choices, ids->item_stick, 4, 0);
    if (maage_status_failed(result)) goto failed;
    result = builtin_recipe(registry, MAAGE_RECIPE_SHAPED, 2, 2, "AAAA", choices, ids->item_crafting_table, 1, 0);
    if (maage_status_failed(result)) goto failed;
    choices[0].item = ids->item_cobblestone;
    choices[0].metadata = 0;
    result = builtin_recipe(registry, MAAGE_RECIPE_SHAPED, 3, 3, "AAAA AAAA", choices, ids->item_furnace, 1, 0);
    if (maage_status_failed(result)) goto failed;
    choices[0].item = ids->item_planks;
    choices[0].metadata = MAAGE_RECIPE_METADATA_ANY;
    result = builtin_recipe(registry, MAAGE_RECIPE_SHAPED, 3, 2, "A A A ", choices, ids->item_bowl, 4, 0);
    if (maage_status_failed(result)) goto failed;
    choices[1].item = ids->item_stick;
    for (i = 0; i < 5; ++i) {
        choices[0].item = materials[i];
        choices[0].metadata = i == 0 ? MAAGE_RECIPE_METADATA_ANY : 0;
        for (family = 0; family < 3; ++family) {
            result = builtin_recipe(registry, MAAGE_RECIPE_SHAPED, widths[family], 3, patterns[family], choices, ids->tools[i][family], 1, 0);
            if (maage_status_failed(result)) goto failed;
        }
    }
    choices[0].item = ids->item_coal;
    choices[0].metadata = MAAGE_RECIPE_METADATA_ANY;
    result = builtin_recipe(registry, MAAGE_RECIPE_SHAPED, 1, 2, "AB", choices, ids->item_torch, 4, 0);
    if (maage_status_failed(result)) goto failed;
    choices[0].item = ids->item_sand;
    choices[0].metadata = 0;
    result = builtin_recipe(registry, MAAGE_RECIPE_SHAPED, 2, 2, "AAAA", choices, ids->item_sandstone, 1, 0);
    if (maage_status_failed(result)) goto failed;
    choices[0].item = ids->item_iron_ingot;
    result = builtin_recipe(registry, MAAGE_RECIPE_SHAPED, 2, 2, " AA ", choices, ids->item_shears, 1, 0);
    if (maage_status_failed(result)) goto failed;
    choices[0].item = ids->item_brown_mushroom;
    choices[1].item = ids->item_red_mushroom;
    choices[2].item = ids->item_bowl;
    result = builtin_recipe(registry, MAAGE_RECIPE_SHAPELESS, 3, 1, "ABC", choices, ids->item_mushroom_stew, 1, 0);
    if (maage_status_failed(result)) goto failed;
    for (i = 0; i < 8; ++i) {
        choices[0].item = resources[i];
        choices[0].metadata = 0;
        result = builtin_recipe(registry, MAAGE_RECIPE_SHAPED, i == 7 ? 2U : 3U, i == 7 ? 2U : 3U,
                                "AAAAAAAAA", choices, blocks[i], 1, 0);
        if (maage_status_failed(result)) goto failed;
        if (i == 7) continue; /* Snow returns snowballs by harvesting. */
        choices[0].item = blocks[i];
        result = builtin_recipe(registry, MAAGE_RECIPE_SHAPELESS, 1, 1, "A", choices, resources[i], 9, 0);
        if (maage_status_failed(result)) goto failed;
    }
    choices[0].item = ids->item_gold_nugget;
    result = builtin_recipe(registry, MAAGE_RECIPE_SHAPED, 3, 3, "AAAAAAAAA", choices, ids->item_gold_ingot, 1, 0);
    if (maage_status_failed(result)) goto failed;
    choices[0].item = ids->item_gold_ingot;
    result = builtin_recipe(registry, MAAGE_RECIPE_SHAPELESS, 1, 1, "A", choices, ids->item_gold_nugget, 9, 0);
    if (maage_status_failed(result)) goto failed;
    result = maage_recipe_registry_finalize(registry);
    if (maage_status_failed(result)) goto failed;
    *output = registry;
    return result;
failed:
    maage_recipe_registry_destroy(registry);
    return result;
}
