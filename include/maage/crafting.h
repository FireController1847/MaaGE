#ifndef MAAGE_CRAFTING_H
#define MAAGE_CRAFTING_H
#include "maage/items.h"

#define MAAGE_CRAFTING_CELLS 9U
#define MAAGE_RECIPE_METADATA_ANY 65535U
enum { MAAGE_RECIPE_SHAPED = 1, MAAGE_RECIPE_SHAPELESS = 2 };
typedef unsigned long maage_recipe_id;
typedef struct maage_recipe_choice {
    maage_item_id item;
    unsigned int metadata;
} maage_recipe_choice;
/* Each ingredient occupies one cell and consumes quantity units. Shapeless
 * duplicates require distinct cells, as in the reference. Alternatives allow
 * overlapping numeric ingredient sets without greedy matching failures. */
typedef struct maage_recipe_ingredient {
    const maage_recipe_choice *choices;
    size_t choice_count;
    unsigned int quantity;
    maage_item_stack remainder; /* Per occupied cell, returned once per craft. */
} maage_recipe_ingredient;
typedef struct maage_recipe_definition {
    unsigned int kind, width, height, ingredient_count;
    int mirror;
    maage_recipe_ingredient ingredients[MAAGE_CRAFTING_CELLS];
    maage_item_stack output;
} maage_recipe_definition;
typedef struct maage_recipe_match {
    maage_recipe_id recipe;
    maage_item_stack output, remainders[MAAGE_CRAFTING_CELLS];
    unsigned int consume[MAAGE_CRAFTING_CELLS];
} maage_recipe_match;
typedef struct maage_recipe_registry maage_recipe_registry;
/* Owns copied definitions/alternatives; borrows finalized items. No recipe
 * count ceiling. Finalization orders shaped before shapeless, larger before
 * smaller, then registration order. Matching allocates nothing. */
maage_status maage_recipe_registry_create(const maage_item_registry *items,
    maage_recipe_registry **output);
maage_status maage_recipe_registry_register(maage_recipe_registry *registry,
    const maage_recipe_definition *definition, maage_recipe_id *id);
maage_status maage_recipe_registry_finalize(maage_recipe_registry *registry);
maage_status maage_recipe_registry_destroy(maage_recipe_registry *registry);
size_t maage_recipe_registry_count(const maage_recipe_registry *registry);
maage_status maage_recipe_registry_create_builtin(const maage_item_registry *items,
    maage_recipe_registry **output);
/* NOOP and an empty match when no recipe fits. Only 2x2 and 3x3 grids. */
maage_status maage_recipe_find(const maage_recipe_registry *registry,
    const maage_item_stack *grid, unsigned int width, maage_recipe_match *match);

enum { MAAGE_CONTAINER_CLOSED = 0, MAAGE_CONTAINER_PLAYER = 1,
    MAAGE_CONTAINER_TABLE = 2 };
enum { MAAGE_SLOT_INVENTORY = 1, MAAGE_SLOT_CRAFT_INPUT = 2,
    MAAGE_SLOT_CRAFT_RESULT = 3 };
enum { MAAGE_SLOT_PRIMARY = 1, MAAGE_SLOT_SECONDARY = 2,
    MAAGE_SLOT_QUICK_MOVE = 3 };
typedef struct maage_crafting_container {
    unsigned long identity, revision;
    unsigned int kind;
    int closing, x, y, z;
    maage_item_stack inputs[MAAGE_CRAFTING_CELLS], output;
} maage_crafting_container;
maage_status maage_crafting_open(maage_crafting_container *container,
    unsigned int kind, int x, int y, int z);
maage_status maage_crafting_refresh(maage_crafting_container *container,
    const maage_recipe_registry *recipes);
/* Server validates container identity/world access before calling. Revision is
 * checked for result transactions; ordinary ordered clicks may be pipelined.
 * Output and remainders preflight in bounded scratch before any owned mutation. */
maage_status maage_crafting_click(maage_crafting_container *container,
    maage_inventory *inventory, const maage_item_registry *items,
    const maage_recipe_registry *recipes, unsigned long revision,
    unsigned int group, unsigned int slot, unsigned int action);
struct maage_player;
struct maage_world;
/* Returns inputs/cursor to inventory, reserves all overflow drops first, then
 * transfers ownership. Failure retains all unreturned items and blocks clicks. */
maage_status maage_crafting_close(maage_crafting_container *container,
    maage_inventory *inventory, const maage_item_registry *items,
    const struct maage_player *player, struct maage_world *world);
int maage_crafting_accessible(const maage_crafting_container *container,
    const struct maage_player *player, const struct maage_world *world);
#endif
