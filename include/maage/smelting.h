#ifndef MAAGE_SMELTING_H
#define MAAGE_SMELTING_H
#include "maage/items.h"
#define MAAGE_SMELTING_VARIANT_ANY MAAGE_CONTENT_ID_INVALID
typedef struct maage_smelting_registry maage_smelting_registry;
typedef struct maage_fuel_registry maage_fuel_registry;
typedef struct maage_smelting_recipe {
    maage_item_id input;
    maage_item_variant_id variant;
    maage_item_stack output;
    double experience; /* Per extracted result item, using the output identity. */
} maage_smelting_recipe;
typedef struct maage_fuel_definition {
    maage_item_id item;
    maage_item_variant_id variant;
    unsigned long ticks;
    maage_item_stack remainder;
} maage_fuel_definition;
/* Registries own copied definitions and borrow finalized items. Registration
 * rejects overlapping numeric keys; finalization publishes immutable hash tables.
 * Lookups allocate nothing, including damaged wooden tools. */
maage_status maage_smelting_registry_create(const maage_item_registry *items, maage_smelting_registry **output);
maage_status maage_smelting_register(maage_smelting_registry *registry, const maage_smelting_recipe *recipe);
maage_status maage_smelting_finalize(maage_smelting_registry *registry);
maage_status maage_smelting_destroy(maage_smelting_registry *registry);
const maage_smelting_recipe *maage_smelting_find(const maage_smelting_registry *registry, maage_item_id item, maage_item_variant_id variant);
double maage_smelting_experience(const maage_smelting_registry *registry, maage_item_id output, maage_item_variant_id variant);
maage_status maage_fuel_registry_create(const maage_item_registry *items, maage_fuel_registry **output);
maage_status maage_fuel_register(maage_fuel_registry *registry, const maage_fuel_definition *fuel);
maage_status maage_fuel_finalize(maage_fuel_registry *registry);
maage_status maage_fuel_destroy(maage_fuel_registry *registry);
const maage_fuel_definition *maage_fuel_find(const maage_fuel_registry *registry, maage_item_id item, maage_item_variant_id variant);
#endif
