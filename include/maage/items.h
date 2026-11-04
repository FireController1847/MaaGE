#ifndef MAAGE_ITEMS_H
#define MAAGE_ITEMS_H
#include "maage/blocks.h"
#include "maage/traits.h"

#define MAAGE_INVENTORY_SLOTS 36U
#define MAAGE_HOTBAR_SLOTS 9U
typedef unsigned long maage_item_id;
typedef unsigned long maage_item_variant_id;
typedef struct maage_item_variant_definition {
    maage_property_set_id properties;
    maage_block_state_id icon_state_id, placement_state_id;
    maage_texture_id texture_id;
    const char *display_name;
} maage_item_variant_definition;
enum {
    MAAGE_ITEM_MODEL_BLOCK = 0,
    MAAGE_ITEM_MODEL_SPRITE = 1,
    MAAGE_ITEM_MODEL_REGISTERED = 2
};
typedef struct maage_item_model_transform {
    double translation[3], rotation[3], scale; /* Model units to centered item; zero scale means one. */
} maage_item_model_transform;
typedef struct maage_item_sprite_layer {
    maage_texture_id texture;
    float tint[4];
} maage_item_sprite_layer;
/* Item identities are allocated independently of blocks and states. */
struct maage_item_definition;
typedef maage_status (*maage_item_placement)(const struct maage_item_definition *item, maage_item_variant_id variant, maage_block_face face, double yaw, maage_block_state_id *state);
typedef struct maage_item_definition {
    const void *behavior_context; /* Borrowed immutable module data. */
    maage_item_id id;
    int has_block;
    maage_block_id block_id;
    maage_block_state_id placement_state_id, icon_state_id;
    unsigned int max_stack, durability, tool_level;
    maage_tag_id tool_tag;
    int has_tool_tag;                              /* Presentation/use classification; effectiveness uses registered tag membership. */
    const maage_traits_registry *traits;           /* Set by the registry, borrowed immutable composition. */
    const maage_item_variant_definition *variants; /* Copied; each configuration is validated before finalization. */
    size_t variant_count;                          /* Zero registers the single empty-property default variant. */
    /* Nonzero durability is the inclusive maximum usable damage value. */
    double mining_speed;
    unsigned int attack_damage;           /* Zero uses the ordinary one-point unarmed attack. */
    unsigned int food_hunger, food_ticks; /* food_ticks == 0 is not edible. */
    double food_saturation;
    int food_always_edible;
    maage_item_id container_item; /* Optional consumption remainder (food/fuel); max_stack must be one. */
    /* Construction-only appearance sources. Modules bind copied rows before finalization;
     * finalized definitions clear these pointers and presentation reads rows directly. */
    maage_block_state_id (*icon_state)(const struct maage_item_definition *, maage_item_variant_id);
    maage_texture_id (*icon_texture)(const struct maage_item_definition *, maage_item_variant_id);
    const char *(*variant_name)(const struct maage_item_definition *, maage_item_variant_id);
    /* Optional immutable tooltip lines, borrowed from module-owned data. */
    const char *(*tooltip)(const struct maage_item_definition *, maage_item_variant_id);
    unsigned int model; /* Shared numeric presentation category. */
    unsigned long registered_model, spawn_type;
    maage_item_model_transform presentation;
    unsigned int texture_index; /* Builtin item-atlas tile, initialization only. */
    maage_texture_id texture_id;
    const maage_item_sprite_layer *sprite_layers; /* Ordered, copied by registration. Empty uses texture_id. */
    size_t sprite_layer_count;
    const char *display_name;
    maage_status (*use_placement)(const struct maage_item_definition *, const struct maage_world *, const struct maage_world_target *, maage_block_state_id *, int *);
    int placement_air_only;
    int use_durability;      /* Successful use damages rather than consumes the stack. */
    unsigned int break_wear; /* 0 none, 1 ordinary hardness, 2 matching block class. */
    int fuel_container;
    maage_item_placement placement; /* NULL uses the selected variant's placement_state_id. */
} maage_item_definition;
typedef struct maage_item_stack {
    maage_item_id item_id;
    unsigned int count;
    maage_item_variant_id variant;
    unsigned int damage;
} maage_item_stack;
typedef struct maage_inventory {
    maage_item_stack slots[MAAGE_INVENTORY_SLOTS], cursor;
    unsigned int selected;
} maage_inventory;
typedef struct maage_item_registry maage_item_registry;

/* Definitions and both names are copied. Finalization makes sparse-paged,
 * 32-bit numeric lookup immutable and O(1), independent of highest ID;
 * strings are used only at registration/UI boundaries. Registration returns
 * the next ID in definition.id; zero is reserved for an empty stack.
 * Bind traits before registration. Every item starts with an empty, replaceable
 * item-domain trait assignment; item tags are optional. */
maage_status maage_item_registry_create(size_t capacity, maage_item_registry **output);
maage_status maage_item_registry_destroy(maage_item_registry *registry);
maage_status maage_item_registry_register(maage_item_registry *registry, const char *external_name, maage_item_definition *definition);
/* Module-owned presentation binding before finalization; leaves placement,
 * inventory identity and gameplay definitions unchanged. */
maage_status maage_item_registry_bind_model(maage_item_registry *, maage_item_id, unsigned long, const maage_item_model_transform *);
/* Borrowed traits outlive items and all readers. Variant IDs are local to their item. */
maage_status maage_item_registry_bind_traits(maage_item_registry *, maage_traits_registry *);
const maage_traits_registry *maage_item_registry_traits(const maage_item_registry *);
maage_status maage_item_registry_bind_blocks(maage_item_registry *, const maage_block_registry *);
const maage_block_registry *maage_item_registry_blocks(const maage_item_registry *);
/* Owner-only binding before finalization; a row at variant_count appends.
 * Rows may move while binding. Retain IDs until finalization. Placeable rows
 * reference their declared block and registered appearance states. */
maage_status maage_item_registry_variant_bind(maage_item_registry *, maage_item_id, maage_item_variant_id, const maage_item_variant_definition *);
const maage_item_variant_definition *maage_item_variant_get(const maage_item_registry *, maage_item_id, maage_item_variant_id);
maage_status maage_item_variant_resolve(const maage_item_registry *, maage_item_id, maage_property_set_id, maage_item_variant_id *);
int maage_item_variant_value(const maage_item_registry *, maage_item_id, maage_item_variant_id, maage_property_id, maage_property_value *);
maage_status maage_item_variant_change(const maage_item_registry *, maage_item_id, maage_item_variant_id, maage_property_id, maage_property_value, maage_item_variant_id *);
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
 * Existing compatible stacks merge before empty slots. Validate all destination
 * stacks before mutation. Empty input is NOOP. */
maage_status maage_inventory_add(maage_inventory *inventory, const maage_item_registry *registry, const maage_item_stack *stack, unsigned int *remaining);
maage_status maage_item_stack_click(maage_item_stack *source, maage_item_stack *cursor, const maage_item_registry *registry, int secondary);
/* Primary picks up/merges/swaps; secondary picks up half or places one. */
maage_status maage_inventory_click(maage_inventory *inventory, const maage_item_registry *registry, unsigned int slot, int secondary);
/* Merge into [first,end), excluding the source if it belongs to that range.
 * Validate all involved stacks before mutation; retain any untransferred source.
 * Existing compatible stacks precede empty slots. */
maage_status maage_inventory_transfer(maage_item_stack *, maage_inventory *, const maage_item_registry *, unsigned int first, unsigned int end);
enum {
    MAAGE_INVENTORY_CLICK = 1,
    MAAGE_INVENTORY_QUICK_MOVE,
    MAAGE_INVENTORY_HOTBAR_SWAP,
    MAAGE_INVENTORY_CLONE,
    MAAGE_INVENTORY_COLLECT,
    MAAGE_INVENTORY_DISTRIBUTE,
    MAAGE_INVENTORY_QUICK_MOVE_MATCHING
};
typedef struct maage_inventory_operation {
    unsigned int kind, slot, hotbar, secondary;
    maage_item_stack match;                     /* Validated value for a shift-double-click transfer. */
    unsigned char slots[MAAGE_INVENTORY_SLOTS]; /* Distribution membership, no borrowed storage. */
} maage_inventory_operation;
/* Atomic value-only player-slot transactions. hotbar_only restricts accessible
 * slots and makes quick-move delete a catalog hotbar stack (Creative only). Clone requires
 * creative. Distribution and collection share these rules with GUI previews. */
maage_status maage_inventory_operate(maage_inventory *, const maage_item_registry *, const maage_inventory_operation *, int creative, int hotbar_only);
maage_status maage_item_placement_state(const maage_item_definition *item, maage_item_variant_id variant, maage_block_face face, double yaw, maage_block_state_id *state);
/* Pure numeric rules; randomness is caller-owned and committed with mutation. */
int maage_item_has_tag(const maage_item_definition *, maage_tag_id);
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
