#include "maage/smelting.h"
#include <stdlib.h>
#include <string.h>

typedef struct smelting_entry {
    maage_item_id item;
    maage_item_variant_id variant;
    maage_smelting_recipe recipe;
    maage_fuel_definition fuel;
    double experience;
} smelting_entry;
typedef struct smelting_table {
    const maage_item_registry *items;
    smelting_entry *entries;
    size_t count, capacity;
    int finalized, failed;
} smelting_table;
struct maage_smelting_registry {
    smelting_table inputs, outputs;
};
struct maage_fuel_registry {
    smelting_table table;
};
static maage_status smelting_status(unsigned char code, unsigned char operation) {
    return maage_status_make(code, MAAGE_CONTEXT_REGISTRY, operation, 0);
}
static maage_status smelting_read_item(const maage_item_registry *items, maage_item_id item) {
    maage_item_id resolved;
    const char *name = maage_item_registry_name(items, item);
    if (!name) return smelting_status(MAAGE_STATUS_NOT_FOUND, MAAGE_OPERATION_REGISTER);
    return maage_item_registry_resolve_name(items, name, &resolved);
}
static size_t smelting_hash(maage_item_id item, maage_item_variant_id variant, size_t capacity) {
    return (size_t) ((item * 2654435761UL) ^ ((unsigned long) variant * 2246822519UL)) & (capacity - 1);
}
static smelting_entry *smelting_lookup(const smelting_table *table, maage_item_id item, maage_item_variant_id variant) {
    size_t index;
    if (!table || !table->capacity || !item) return NULL;
    index = smelting_hash(item, variant, table->capacity);
    while (table->entries[index].item) {
        if (table->entries[index].item == item && table->entries[index].variant == variant) return &table->entries[index];
        index = (index + 1) & (table->capacity - 1);
    }
    return NULL;
}
static maage_status smelting_insert(smelting_table *table, const smelting_entry *entry) {
    smelting_entry *grown;
    size_t capacity, i, index;
    if (table->finalized) return smelting_status(MAAGE_STATUS_INVALID_STATE, MAAGE_OPERATION_REGISTER);
    if (smelting_lookup(table, entry->item, entry->variant)) return smelting_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_REGISTER);
    if (!table->capacity || table->count >= table->capacity / 2) {
        capacity = table->capacity ? table->capacity * 2 : 16;
        if (capacity < table->capacity || capacity > (size_t) - 1 / sizeof(*grown)) return smelting_status(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_OPERATION_REGISTER);
        grown = (smelting_entry *) calloc(capacity, sizeof(*grown));
        if (!grown) return smelting_status(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_REGISTER);
        for (i = 0; i < table->capacity; ++i) {
            if (!table->entries[i].item) continue;
            index = smelting_hash(table->entries[i].item, table->entries[i].variant, capacity);
            while (grown[index].item)
                index = (index + 1) & (capacity - 1);
            grown[index] = table->entries[i];
        }
        free(table->entries);
        table->entries = grown;
        table->capacity = capacity;
    }
    index = smelting_hash(entry->item, entry->variant, table->capacity);
    while (table->entries[index].item)
        index = (index + 1) & (table->capacity - 1);
    table->entries[index] = *entry;
    ++table->count;
    return smelting_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_REGISTER);
}
static int smelting_key_valid(const smelting_table *table, maage_item_id item, maage_item_variant_id variant) {
    const maage_item_definition *definition = maage_item_registry_get(table->items, item);
    return definition && (variant == MAAGE_SMELTING_VARIANT_ANY || variant < definition->variant_count);
}
static maage_status smelting_expand(smelting_table *table, smelting_entry *entry) {
    size_t i;
    maage_status result;
    if (table->finalized || table->failed) return smelting_status(MAAGE_STATUS_INVALID_STATE, MAAGE_OPERATION_REGISTER);
    for (i = 0; i < table->capacity; ++i)
        if (table->entries[i].item == entry->item && (table->entries[i].variant == entry->variant || table->entries[i].variant == MAAGE_SMELTING_VARIANT_ANY || entry->variant == MAAGE_SMELTING_VARIANT_ANY)) return smelting_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_REGISTER);
    result = smelting_insert(table, entry);
    if (maage_status_failed(result)) table->failed = 1;
    return result;
}
maage_status maage_smelting_registry_create(const maage_item_registry *items, maage_smelting_registry **output) {
    if (output) *output = NULL;
    if (!output || !items) return smelting_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_CREATE);
    *output = (maage_smelting_registry *) calloc(1, sizeof(**output));
    if (!*output) return smelting_status(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_CREATE);
    (*output)->inputs.items = (*output)->outputs.items = items;
    return smelting_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_CREATE);
}
maage_status maage_smelting_register(maage_smelting_registry *registry, const maage_smelting_recipe *recipe) {
    smelting_entry entry, *output;
    maage_status result;
    if (!registry || !recipe || !smelting_key_valid(&registry->inputs, recipe->input, recipe->variant) || !recipe->output.count || !maage_item_stack_valid(registry->inputs.items, &recipe->output) || !(recipe->experience >= 0 && recipe->experience <= 4294967295.0 / 64.0)) return smelting_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_REGISTER);
    if (registry->inputs.finalized) return smelting_status(MAAGE_STATUS_INVALID_STATE, MAAGE_OPERATION_REGISTER);
    result = smelting_read_item(registry->inputs.items, recipe->input);
    if (maage_status_failed(result)) return result;
    result = smelting_read_item(registry->inputs.items, recipe->output.item_id);
    if (maage_status_failed(result)) return result;
    output = smelting_lookup(&registry->outputs, recipe->output.item_id, recipe->output.variant);
    if (output && output->experience != recipe->experience) return smelting_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_REGISTER);
    memset(&entry, 0, sizeof(entry));
    entry.item = recipe->input;
    entry.variant = recipe->variant;
    entry.recipe = *recipe;
    result = smelting_expand(&registry->inputs, &entry);
    if (maage_status_failed(result) || output) return result;
    memset(&entry, 0, sizeof(entry));
    entry.item = recipe->output.item_id;
    entry.variant = recipe->output.variant;
    entry.experience = recipe->experience;
    result = smelting_insert(&registry->outputs, &entry);
    if (maage_status_failed(result)) registry->inputs.failed = registry->outputs.failed = 1;
    return result;
}
maage_status maage_smelting_finalize(maage_smelting_registry *registry) {
    if (!registry || registry->inputs.finalized || registry->inputs.failed || registry->outputs.failed || !maage_item_registry_is_finalized(registry->inputs.items)) return smelting_status(MAAGE_STATUS_INVALID_STATE, MAAGE_OPERATION_FINALIZE);
    registry->inputs.finalized = registry->outputs.finalized = 1;
    return smelting_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_FINALIZE);
}
maage_status maage_smelting_destroy(maage_smelting_registry *registry) {
    if (!registry) return smelting_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_DESTROY);
    free(registry->inputs.entries);
    free(registry->outputs.entries);
    free(registry);
    return smelting_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_DESTROY);
}
const maage_smelting_recipe *maage_smelting_find(const maage_smelting_registry *registry, maage_item_id item, maage_item_variant_id variant) {
    smelting_entry *entry;
    if (!registry || !registry->inputs.finalized || variant == MAAGE_SMELTING_VARIANT_ANY || !smelting_key_valid(&registry->inputs, item, variant)) return NULL;
    entry = smelting_lookup(&registry->inputs, item, variant);
    if (!entry && registry && registry->inputs.finalized) entry = smelting_lookup(&registry->inputs, item, MAAGE_SMELTING_VARIANT_ANY);
    return entry ? &entry->recipe : NULL;
}
double maage_smelting_experience(const maage_smelting_registry *registry, maage_item_id item, maage_item_variant_id variant) {
    smelting_entry *entry = registry && registry->outputs.finalized ? smelting_lookup(&registry->outputs, item, variant) : NULL;
    return entry ? entry->experience : 0;
}
maage_status maage_fuel_registry_create(const maage_item_registry *items, maage_fuel_registry **output) {
    if (output) *output = NULL;
    if (!output || !items) return smelting_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_CREATE);
    *output = (maage_fuel_registry *) calloc(1, sizeof(**output));
    if (!*output) return smelting_status(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_CREATE);
    (*output)->table.items = items;
    return smelting_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_CREATE);
}
maage_status maage_fuel_register(maage_fuel_registry *registry, const maage_fuel_definition *fuel) {
    smelting_entry entry;
    const maage_item_definition *item;
    maage_status result;
    if (!registry || !fuel || !smelting_key_valid(&registry->table, fuel->item, fuel->variant) || !fuel->ticks || fuel->ticks > 4294967295UL || !maage_item_stack_valid(registry->table.items, &fuel->remainder)) return smelting_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_REGISTER);
    result = smelting_read_item(registry->table.items, fuel->item);
    if (maage_status_failed(result)) return result;
    if (fuel->remainder.count) {
        result = smelting_read_item(registry->table.items, fuel->remainder.item_id);
        if (maage_status_failed(result)) return result;
    }
    item = maage_item_registry_get(registry->table.items, fuel->item);
    if (fuel->remainder.count && (item->max_stack != 1 || fuel->remainder.count != 1)) return smelting_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_REGISTER);
    memset(&entry, 0, sizeof(entry));
    entry.item = fuel->item;
    entry.variant = fuel->variant;
    entry.fuel = *fuel;
    return smelting_expand(&registry->table, &entry);
}
maage_status maage_fuel_finalize(maage_fuel_registry *registry) {
    if (!registry || registry->table.finalized || registry->table.failed || !maage_item_registry_is_finalized(registry->table.items)) return smelting_status(MAAGE_STATUS_INVALID_STATE, MAAGE_OPERATION_FINALIZE);
    registry->table.finalized = 1;
    return smelting_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_FINALIZE);
}
maage_status maage_fuel_destroy(maage_fuel_registry *registry) {
    if (!registry) return smelting_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_DESTROY);
    free(registry->table.entries);
    free(registry);
    return smelting_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_DESTROY);
}
const maage_fuel_definition *maage_fuel_find(const maage_fuel_registry *registry, maage_item_id item, maage_item_variant_id variant) {
    smelting_entry *entry;
    if (!registry || !registry->table.finalized || variant == MAAGE_SMELTING_VARIANT_ANY || !smelting_key_valid(&registry->table, item, variant)) return NULL;
    entry = smelting_lookup(&registry->table, item, variant);
    if (!entry && registry && registry->table.finalized) entry = smelting_lookup(&registry->table, item, MAAGE_SMELTING_VARIANT_ANY);
    return entry ? &entry->fuel : NULL;
}
