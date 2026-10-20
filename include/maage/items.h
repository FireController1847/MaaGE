#ifndef MAAGE_ITEMS_H
#define MAAGE_ITEMS_H
#include "maage/blocks.h"

#define MAAGE_INVENTORY_SLOTS 36U
#define MAAGE_HOTBAR_SLOTS 9U
typedef unsigned long maage_item_id;
enum { MAAGE_TOOL_NONE = 0, MAAGE_TOOL_PICKAXE, MAAGE_TOOL_SHOVEL, MAAGE_TOOL_AXE };
struct maage_item_definition;
typedef int (*maage_item_placement)(const struct maage_item_definition *item,
    unsigned int metadata, maage_block_face face, double yaw,
    maage_block_state_id *state);
typedef struct maage_item_definition {
    maage_item_id id;
    int has_block;
    maage_block_id block_id;
    maage_block_state_id placement_state_id, icon_state_id;
    unsigned int max_stack, durability, tool_class, tool_level;
    double mining_speed;
    maage_texture_id texture_id;
    const char *display_name;
    maage_item_placement placement; /* NULL uses placement_state_id, metadata 0. */
} maage_item_definition;
typedef struct maage_item_stack {
    maage_item_id item_id;
    unsigned int count, metadata, damage;
} maage_item_stack;
typedef struct maage_inventory {
    maage_item_stack slots[MAAGE_INVENTORY_SLOTS], cursor;
    unsigned int selected;
} maage_inventory;
typedef struct maage_item_registry maage_item_registry;

/* Definitions and both names are copied. Finalization makes direct numeric
 * lookup immutable; strings are used only at registration/UI boundaries. */
maage_item_registry *maage_item_registry_create(size_t capacity);
void maage_item_registry_destroy(maage_item_registry *registry);
maage_registry_result maage_item_registry_register(maage_item_registry *registry,
    const char *external_name, const maage_item_definition *definition);
maage_registry_result maage_item_registry_finalize(maage_item_registry *registry);
int maage_item_registry_is_finalized(const maage_item_registry *registry);
const maage_item_definition *maage_item_registry_get(const maage_item_registry *registry,
    maage_item_id id);
size_t maage_item_registry_count(const maage_item_registry *registry);
const maage_item_definition *maage_item_registry_at(const maage_item_registry *registry,
    size_t index);
maage_item_registry *maage_item_registry_create_builtin(const maage_block_registry *blocks);
int maage_item_stack_valid(const maage_item_registry *registry, const maage_item_stack *stack);
const maage_item_stack *maage_inventory_selected(const maage_inventory *inventory);
/* Returns uninserted count. Existing compatible stacks merge before empty slots. */
unsigned int maage_inventory_add(maage_inventory *inventory,
    const maage_item_registry *registry, const maage_item_stack *stack);
/* Primary picks up/merges/swaps; secondary picks up half or places one. */
int maage_inventory_click(maage_inventory *inventory,
    const maage_item_registry *registry, unsigned int slot, int secondary);
int maage_inventory_creative(maage_inventory *inventory,
    const maage_item_registry *registry, maage_item_id item);
int maage_item_placement_state(const maage_item_definition *item,
    unsigned int metadata, maage_block_face face, double yaw,
    maage_block_state_id *state);
#endif
