#ifndef MAAGE_FURNACE_H
#define MAAGE_FURNACE_H
#include "maage/smelting.h"
#include "maage/crafting.h"
#define MAAGE_FURNACE_COOK_TICKS 200U
#define MAAGE_FURNACE_VERSION 1UL
typedef struct maage_furnace_state {
    maage_item_stack slots[3]; /* input, fuel, output */
    unsigned long burn, duration;
    unsigned int cook;
    unsigned long identity; /* Owner-assigned loaded lifetime; not a saved gameplay ID. */
    unsigned long revision; /* Inventory changes only; progress is sampled separately. */
} maage_furnace_state;
int maage_furnace_valid(const maage_furnace_state *state, const maage_item_registry *items);
/* Exactly one owner 20 TPS step, without allocation or World access. */
maage_status maage_furnace_tick(maage_furnace_state *state, const maage_item_registry *items, const maage_smelting_registry *recipes, const maage_fuel_registry *fuels);
struct maage_world;
maage_status maage_world_furnace_get(const struct maage_world *world, int x, int y, int z, maage_furnace_state *state);
/* Inventory-only owner commit: lifetime/timers must match the read state and
 * revision advances once. Processing remains exclusively in World tick. */
maage_status maage_world_furnace_set(struct maage_world *world, int x, int y, int z, const maage_furnace_state *state);
/* Uses the shared stack/cursor primitives; scratch state commits atomically.
 * experience is the extracted quantity's value, with fractional rounding owned
 * by Server gameplay. No accumulated recipe-use history is stored. */
maage_status maage_furnace_click(maage_furnace_state *state, maage_inventory *inventory, const maage_item_registry *items, const maage_smelting_registry *recipes, const maage_fuel_registry *fuels, unsigned long revision, unsigned int group, unsigned int slot, unsigned int action, double *experience);
#endif
