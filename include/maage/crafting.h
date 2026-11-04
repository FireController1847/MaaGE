#ifndef MAAGE_CRAFTING_H
#define MAAGE_CRAFTING_H
#include "maage/items.h"

#define MAAGE_CRAFTING_CELLS 9U
#define MAAGE_RECIPE_VARIANT_ANY MAAGE_CONTENT_ID_INVALID
enum {
    MAAGE_RECIPE_SHAPED = 1,
    MAAGE_RECIPE_SHAPELESS = 2
};
typedef unsigned long maage_recipe_id;
enum {
    MAAGE_RECIPE_CHOICE_ITEM = 0,
    MAAGE_RECIPE_CHOICE_TAG = 1
};
typedef struct maage_recipe_choice {
    maage_item_id item;
    maage_item_variant_id variant;
    maage_tag_id tag;
    unsigned int kind;
    maage_property_set_id properties; /* Optional exact typed value constraints. */
} maage_recipe_choice;
/* Each ingredient occupies one cell and consumes quantity units. Shapeless
 * duplicates require distinct cells. Alternatives allow
 * overlapping numeric ingredient sets without greedy matching failures. */
typedef struct maage_recipe_ingredient {
    const maage_recipe_choice *choices;
    size_t choice_count;
    unsigned int quantity;
    maage_item_stack remainder; /* Per occupied cell, returned once per craft. */
} maage_recipe_ingredient;
typedef struct maage_recipe_property_binding {
    maage_property_id source, target;
} maage_recipe_property_binding;
/* Project one matched ingredient's properties onto a unique output candidate.
 * Finalization compiles every eligible input variant; missing or ambiguous
 * output configurations fail composition instead of choosing registration order.
 * Each target property appears once; distinct source properties may be projected. */
typedef struct maage_recipe_projection {
    unsigned int ingredient;
    maage_tag_id output_tag;
    const maage_recipe_property_binding *properties;
    size_t property_count;
} maage_recipe_projection;
typedef struct maage_recipe_definition {
    unsigned int kind, width, height, ingredient_count;
    int mirror;
    maage_recipe_ingredient ingredients[MAAGE_CRAFTING_CELLS];
    maage_item_stack output;
    maage_recipe_projection projection;
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
maage_status maage_recipe_registry_create(const maage_item_registry *items, maage_recipe_registry **output);
maage_status maage_recipe_registry_register(maage_recipe_registry *registry, const maage_recipe_definition *definition, maage_recipe_id *id);
maage_status maage_recipe_registry_finalize(maage_recipe_registry *registry);
maage_status maage_recipe_registry_destroy(maage_recipe_registry *registry);
size_t maage_recipe_registry_count(const maage_recipe_registry *registry);
/* NOOP and an empty match when no recipe fits. Only 2x2 and 3x3 grids. */
maage_status maage_recipe_find(const maage_recipe_registry *registry, const maage_item_stack *grid, unsigned int width, maage_recipe_match *match);

enum {
    MAAGE_CONTAINER_CLOSED = 0
};
/* A bounded copied presentation packet, independent of registered slot count. */
#define MAAGE_CONTAINER_VIEW_SLOTS 54U
enum {
    MAAGE_SLOT_INVENTORY = 1,
    MAAGE_SLOT_CRAFT_INPUT = 2,
    MAAGE_SLOT_CRAFT_RESULT = 3
};
enum {
    MAAGE_SLOT_PRIMARY = 1,
    MAAGE_SLOT_SECONDARY = 2,
    MAAGE_SLOT_QUICK_MOVE = 3
};
typedef struct maage_container_progress_value {
    unsigned long value, total;
    int active;
} maage_container_progress_value;
typedef struct maage_crafting_container {
    unsigned long identity, revision, block_identity, secondary_block_identity;
    unsigned long kind;
    unsigned int grid_width;
    unsigned long view_first;
    unsigned int view_count, progress_count;
    maage_item_stack view[MAAGE_CONTAINER_VIEW_SLOTS];
    maage_container_progress_value progress[MAAGE_CONTAINER_VIEW_SLOTS];
    unsigned long active_remaining, active_duration, process, process_duration;
    int closing, x, y, z;
    int secondary_x, secondary_y, secondary_z; /* Optional independently owned backing block. */
    maage_item_stack inputs[MAAGE_CRAFTING_CELLS], output;
} maage_crafting_container;

maage_status maage_crafting_refresh(maage_crafting_container *container, const maage_recipe_registry *recipes);
/* Server validates container identity/world access before calling. Revision is
 * checked for result transactions; ordinary ordered clicks may be pipelined.
 * Output and remainders preflight in bounded scratch before any owned mutation. */
maage_status maage_crafting_click(maage_crafting_container *container, maage_inventory *inventory, const maage_item_registry *items, const maage_recipe_registry *recipes, unsigned long revision, unsigned int group, unsigned int slot, unsigned int action);
struct maage_player;
struct maage_world;
/* Returns inputs/cursor to inventory, reserves all overflow drops first, then
 * transfers ownership. Failure retains all unreturned items and blocks clicks. */
maage_status maage_crafting_close(maage_crafting_container *container, maage_inventory *inventory, const maage_item_registry *items, const struct maage_player *player, struct maage_world *world);
#endif
