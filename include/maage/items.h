#ifndef MAAGE_ITEMS_H
#define MAAGE_ITEMS_H
#include "maage/blocks.h"

#define MAAGE_INVENTORY_SLOTS 36U
#define MAAGE_HOTBAR_SLOTS 9U
typedef unsigned long maage_item_id;
enum {
    MAAGE_TOOL_NONE = 0,
    MAAGE_TOOL_PICKAXE,
    MAAGE_TOOL_SHOVEL,
    MAAGE_TOOL_AXE,
    MAAGE_TOOL_SHEARS,
    MAAGE_TOOL_HOE
};
enum {
    MAAGE_ITEM_MODEL_BLOCK = 0,
    MAAGE_ITEM_MODEL_SPRITE = 1
};
/* Item identities are allocated independently of blocks and states. */
struct maage_item_definition;
typedef maage_status (*maage_item_placement)(const struct maage_item_definition *item, unsigned int metadata, maage_block_face face, double yaw, maage_block_state_id *state);
typedef struct maage_item_definition {
    const void *behavior_context; /* Borrowed immutable module data. */
    maage_item_id id;
    int has_block;
    maage_block_id block_id;
    maage_block_state_id placement_state_id, icon_state_id;
    unsigned int max_stack, durability, tool_class, tool_level, max_metadata;
    /* Nonzero durability is the inclusive maximum usable damage value. */
    double mining_speed;
    unsigned int food_hunger, food_ticks; /* food_ticks == 0 is not edible. */
    double food_saturation;
    int food_always_edible;
    maage_item_id container_item; /* Optional consumption remainder (food/fuel); max_stack must be one. */
    maage_block_state_id (*icon_state)(const struct maage_item_definition *, unsigned int);
    maage_texture_id (*icon_texture)(const struct maage_item_definition *, unsigned int);
    const char *(*variant_name)(const struct maage_item_definition *, unsigned int);
    unsigned int model;         /* Shared numeric presentation category. */
    unsigned int texture_index; /* Builtin item-atlas tile, initialization only. */
    maage_texture_id texture_id;
    const char *display_name;
    maage_status (*use_placement)(const struct maage_item_definition *, const struct maage_world *, const struct maage_world_target *, maage_block_state_id *, int *);
    int placement_air_only;
    int use_durability;      /* Successful use damages rather than consumes the stack. */
    unsigned int break_wear; /* 0 none, 1 ordinary hardness, 2 matching block class. */
    int fuel_container;
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

/* Definitions and both names are copied. Finalization makes sparse-paged,
 * 32-bit numeric lookup immutable and O(1), independent of highest ID;
 * strings are used only at registration/UI boundaries. Registration returns
 * the next ID in definition.id; zero is reserved for an empty stack. */
maage_status maage_item_registry_create(size_t capacity, maage_item_registry **output);
maage_status maage_item_registry_destroy(maage_item_registry *registry);
maage_status maage_item_registry_register(maage_item_registry *registry, const char *external_name, maage_item_definition *definition);
maage_status maage_item_registry_finalize(maage_item_registry *registry);
int maage_item_registry_is_finalized(const maage_item_registry *registry);
const maage_item_definition *maage_item_registry_get(const maage_item_registry *registry, maage_item_id id);
size_t maage_item_registry_count(const maage_item_registry *registry);

/* First registered placeable item for this block, or zero if absent. */
maage_item_id maage_item_registry_block_item(const maage_item_registry *registry, maage_block_id block);
const maage_item_definition *maage_item_registry_at(const maage_item_registry *registry, size_t index);
/* For persistence/import only, never the fixed-step simulation. */
const char *maage_item_registry_name(const maage_item_registry *registry, maage_item_id id);
maage_status maage_item_registry_resolve_name(const maage_item_registry *registry, const char *name, maage_item_id *id);

int maage_item_stack_valid(const maage_item_registry *registry, const maage_item_stack *stack);
const maage_item_stack *maage_inventory_selected(const maage_inventory *inventory);
/* Remaining is initialized to input count; PARTIAL retains uninserted items.
 * Existing compatible stacks merge before empty slots. Empty input is NOOP. */
maage_status maage_inventory_add(maage_inventory *inventory, const maage_item_registry *registry, const maage_item_stack *stack, unsigned int *remaining);
maage_status maage_item_stack_click(maage_item_stack *source, maage_item_stack *cursor, const maage_item_registry *registry, int secondary);
/* Primary picks up/merges/swaps; secondary picks up half or places one. */
maage_status maage_inventory_click(maage_inventory *inventory, const maage_item_registry *registry, unsigned int slot, int secondary);
maage_status maage_inventory_creative(maage_inventory *inventory, const maage_item_registry *registry, maage_item_id item, unsigned int metadata);
maage_status maage_item_placement_state(const maage_item_definition *item, unsigned int metadata, maage_block_face face, double yaw, maage_block_state_id *state);
/* Pure numeric rules; randomness is caller-owned and committed with mutation. */
int maage_item_can_harvest(const maage_item_definition *tool, const maage_block_definition *block);
double maage_item_mining_speed(const maage_item_definition *tool, const maage_block_definition *block);
double maage_item_mining_damage(const maage_item_definition *tool, const maage_block_definition *block, int grounded, int submerged);
#define MAAGE_DROP_STACKS 2U
typedef struct maage_item_drops {
    maage_item_stack stacks[MAAGE_DROP_STACKS];
    size_t count;
} maage_item_drops;
maage_status maage_item_generate_drops(const maage_block_registry *blocks, const maage_item_registry *items, maage_block_state_id state, const maage_item_definition *tool, unsigned long *random, maage_item_drops *drops);
#endif
