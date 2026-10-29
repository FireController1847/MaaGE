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
    static const maage_item_id materials[5] = {MAAGE_BLOCK_PLANKS, MAAGE_BLOCK_COBBLESTONE,
                                               MAAGE_ITEM_IRON_INGOT, MAAGE_ITEM_DIAMOND, MAAGE_ITEM_GOLD_INGOT};
    static const maage_item_id blocks[8] = {MAAGE_BLOCK_GOLD_BLOCK, MAAGE_BLOCK_IRON_BLOCK,
                                            MAAGE_BLOCK_DIAMOND_BLOCK, MAAGE_BLOCK_LAPIS_BLOCK, MAAGE_BLOCK_EMERALD_BLOCK,
                                            MAAGE_BLOCK_REDSTONE_BLOCK, MAAGE_BLOCK_COAL_BLOCK, MAAGE_BLOCK_SNOW_BLOCK};
    static const maage_item_id resources[8] = {MAAGE_ITEM_GOLD_INGOT, MAAGE_ITEM_IRON_INGOT,
                                               MAAGE_ITEM_DIAMOND, MAAGE_ITEM_LAPIS, MAAGE_ITEM_EMERALD, MAAGE_ITEM_REDSTONE,
                                               MAAGE_ITEM_COAL, MAAGE_ITEM_SNOWBALL};
    static const char *const patterns[3] = {"AAA B  B ", "ABB", "AAAB B"};
    static const unsigned int widths[3] = {3, 1, 2};
    maage_recipe_choice choices[3];
    maage_recipe_registry *registry;
    maage_status result;
    unsigned int i, family;
    if (!output) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_INITIALIZE, 0);
    *output = NULL;
    result = maage_recipe_registry_create(items, &registry);
    if (maage_status_failed(result)) return result;
    memset(choices, 0, sizeof(choices));
    for (i = 0; i < 3; ++i) {
        choices[0].item = MAAGE_BLOCK_LOG;
        choices[0].metadata = i;
        result = builtin_recipe(registry, MAAGE_RECIPE_SHAPED, 1, 1, "A", choices, MAAGE_BLOCK_PLANKS, 4, i);
        if (maage_status_failed(result)) goto failed;
    }
    choices[0].item = MAAGE_BLOCK_PLANKS;
    choices[0].metadata = MAAGE_RECIPE_METADATA_ANY;
    result = builtin_recipe(registry, MAAGE_RECIPE_SHAPED, 1, 2, "AA", choices, MAAGE_ITEM_STICK, 4, 0);
    if (maage_status_failed(result)) goto failed;
    result = builtin_recipe(registry, MAAGE_RECIPE_SHAPED, 2, 2, "AAAA", choices, MAAGE_BLOCK_CRAFTING_TABLE, 1, 0);
    if (maage_status_failed(result)) goto failed;
    result = builtin_recipe(registry, MAAGE_RECIPE_SHAPED, 3, 2, "A A A ", choices, MAAGE_ITEM_BOWL, 4, 0);
    if (maage_status_failed(result)) goto failed;
    choices[1].item = MAAGE_ITEM_STICK;
    for (i = 0; i < 5; ++i) {
        choices[0].item = materials[i];
        choices[0].metadata = i == 0 ? MAAGE_RECIPE_METADATA_ANY : 0;
        for (family = 0; family < 3; ++family) {
            result = builtin_recipe(registry, MAAGE_RECIPE_SHAPED, widths[family], 3, patterns[family], choices, MAAGE_ITEM_TOOL_BASE + i * 3 + family, 1, 0);
            if (maage_status_failed(result)) goto failed;
        }
    }
    choices[0].item = MAAGE_ITEM_COAL;
    choices[0].metadata = MAAGE_RECIPE_METADATA_ANY;
    result = builtin_recipe(registry, MAAGE_RECIPE_SHAPED, 1, 2, "AB", choices, MAAGE_BLOCK_TORCH, 4, 0);
    if (maage_status_failed(result)) goto failed;
    choices[0].item = MAAGE_BLOCK_SAND;
    choices[0].metadata = 0;
    result = builtin_recipe(registry, MAAGE_RECIPE_SHAPED, 2, 2, "AAAA", choices, MAAGE_BLOCK_SANDSTONE, 1, 0);
    if (maage_status_failed(result)) goto failed;
    choices[0].item = MAAGE_ITEM_IRON_INGOT;
    result = builtin_recipe(registry, MAAGE_RECIPE_SHAPED, 2, 2, " AA ", choices, MAAGE_ITEM_SHEARS, 1, 0);
    if (maage_status_failed(result)) goto failed;
    choices[0].item = MAAGE_BLOCK_BROWN_MUSHROOM;
    choices[1].item = MAAGE_BLOCK_RED_MUSHROOM;
    choices[2].item = MAAGE_ITEM_BOWL;
    result = builtin_recipe(registry, MAAGE_RECIPE_SHAPELESS, 3, 1, "ABC", choices, MAAGE_ITEM_MUSHROOM_STEW, 1, 0);
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
    choices[0].item = MAAGE_ITEM_GOLD_NUGGET;
    result = builtin_recipe(registry, MAAGE_RECIPE_SHAPED, 3, 3, "AAAAAAAAA", choices, MAAGE_ITEM_GOLD_INGOT, 1, 0);
    if (maage_status_failed(result)) goto failed;
    choices[0].item = MAAGE_ITEM_GOLD_INGOT;
    result = builtin_recipe(registry, MAAGE_RECIPE_SHAPELESS, 1, 1, "A", choices, MAAGE_ITEM_GOLD_NUGGET, 9, 0);
    if (maage_status_failed(result)) goto failed;
    result = maage_recipe_registry_finalize(registry);
    if (maage_status_failed(result)) goto failed;
    *output = registry;
    return result;
failed:
    maage_recipe_registry_destroy(registry);
    return result;
}
