#include "maage/entity_types.h"
#include "maage/world.h"
#include "registry_policy.h"
#include <stdlib.h>
#include <string.h>

typedef struct entity_type_slot {
    maage_entity_type_definition definition;
    char *name;
} entity_type_slot;
typedef struct entity_spawn_entry {
    const maage_entity_type_definition *definition;
    const maage_entity_spawn_rule *rule;
    double upper;
} entity_spawn_entry;
typedef struct entity_spawn_table {
    entity_spawn_entry *entries;
    size_t count, capacity;
    double total;
} entity_spawn_table;
struct maage_entity_type_registry {
    entity_type_slot **slots;
    size_t count, capacity, *buckets, bucket_count;
    content_policy *policy;
    int finalized;
    entity_spawn_table *spawns;
    size_t biome_count;
};
static maage_status type_status(unsigned char code, unsigned char operation) {
    return maage_status_make(code, MAAGE_CONTEXT_REGISTRY, operation, 0);
}
static unsigned long type_hash(const char *name) {
    unsigned long hash = 2166136261UL;
    while (*name)
        hash = ((hash ^ (unsigned char) *name++) * 16777619UL) & 0xffffffffUL;
    return hash;
}
static size_t type_bucket(const maage_entity_type_registry *registry, const char *name) {
    size_t bucket = (size_t) (type_hash(name) % registry->bucket_count), entry;
    while ((entry = registry->buckets[bucket]) != 0) {
        if (!strcmp(registry->slots[entry - 1]->name, name)) break;
        bucket = (bucket + 1) % registry->bucket_count;
    }
    return bucket;
}
static int type_name_valid(const char *name) {
    const char *start = name;
    int colon = 0;
    char c;
    if (!name || !*name || strlen(name) > 255) return 0;
    while ((c = *name++) != '\0') {
        if (c == ':') {
            if (colon || name == start + 1 || !*name) return 0;
            colon = 1;
        } else if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.' || c == '/')) return 0;
    }
    return colon;
}
static char *type_string(const char *source) {
    char *copy;
    size_t size;
    if (!source) return NULL;
    size = strlen(source);
    if (size == (size_t) - 1) return NULL;
    copy = (char *) malloc(size + 1);
    if (copy) memcpy(copy, source, size + 1);
    return copy;
}
static void type_definition_clear(maage_entity_type_definition *definition) {
    size_t i;
    for (i = 0; i < definition->spawn_count; ++i)
        free((void *) definition->spawns[i].biomes);
    free((void *) definition->spawns);
    free((void *) definition->loot);
    free((void *) definition->textures);
    free((void *) definition->display_name);
    memset(definition, 0, sizeof(*definition));
}
static int type_biome_compare(const void *left, const void *right) {
    maage_biome_id a = *(const maage_biome_id *) left, b = *(const maage_biome_id *) right;
    return a < b ? -1 : a > b ? 1 : 0;
}
int maage_entity_spawn_biome_matches(const maage_entity_spawn_rule *rule, maage_biome_id biome) {
    size_t low = 0, high, middle;
    if (!rule) return 0;
    if (!rule->biome_count) return 1;
    high = rule->biome_count;
    while (low < high) {
        middle = low + (high - low) / 2;
        if (rule->biomes[middle] < biome) low = middle + 1;
        else high = middle;
    }
    return low < rule->biome_count && rule->biomes[low] == biome;
}
static maage_status type_definition_copy(maage_entity_type_definition *target, const maage_entity_type_definition *source) {
    maage_entity_spawn_rule *spawns;
    maage_entity_loot *loot;
    unsigned long *textures;
    maage_biome_id *biomes;
    size_t i;
    *target = *source;
    target->display_name = NULL;
    target->spawns = NULL;
    target->spawn_count = 0;
    target->loot = NULL;
    target->textures = NULL;
    if (!source->display_name || !*source->display_name || (source->loot_count && !source->loot) || (source->spawn_count && !source->spawns) || (source->texture_count && !source->textures)) return type_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_REGISTER);
    if (source->spawn_count > (size_t) - 1 / sizeof(*spawns) || source->loot_count > (size_t) - 1 / sizeof(*loot) || source->texture_count > (size_t) - 1 / sizeof(*textures)) return type_status(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_OPERATION_REGISTER);
    target->display_name = type_string(source->display_name);
    if (!target->display_name) goto allocation;
    if (source->loot_count) {
        loot = (maage_entity_loot *) malloc(source->loot_count * sizeof(*loot));
        if (!loot) goto allocation;
        memcpy(loot, source->loot, source->loot_count * sizeof(*loot));
        target->loot = loot;
    }
    if (source->texture_count) {
        textures = (unsigned long *) malloc(source->texture_count * sizeof(*textures));
        if (!textures) goto allocation;
        memcpy(textures, source->textures, source->texture_count * sizeof(*textures));
        target->textures = textures;
    }
    if (source->spawn_count) {
        spawns = (maage_entity_spawn_rule *) calloc(source->spawn_count, sizeof(*spawns));
        if (!spawns) goto allocation;
        target->spawns = spawns;
        for (i = 0; i < source->spawn_count; ++i) {
            spawns[i] = source->spawns[i];
            spawns[i].biomes = NULL;
            target->spawn_count = i + 1;
            if (!spawns[i].biome_count) continue;
            if (!source->spawns[i].biomes || spawns[i].biome_count > (size_t) - 1 / sizeof(*biomes)) {
                type_definition_clear(target);
                return type_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_REGISTER);
            }
            biomes = (maage_biome_id *) malloc(spawns[i].biome_count * sizeof(*biomes));
            if (!biomes) goto allocation;
            memcpy(biomes, source->spawns[i].biomes, spawns[i].biome_count * sizeof(*biomes));
            qsort(biomes, spawns[i].biome_count, sizeof(*biomes), type_biome_compare);
            spawns[i].biomes = biomes;
        }
    }
    return type_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_REGISTER);
allocation:
    type_definition_clear(target);
    return type_status(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_REGISTER);
}
maage_status maage_entity_type_registry_create(maage_entity_type_registry **output) {
    if (!output) return type_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_CREATE);
    *output = (maage_entity_type_registry *) calloc(1, sizeof(**output));
    return type_status(*output ? MAAGE_STATUS_SUCCESS : MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_CREATE);
}
static maage_status type_grow(maage_entity_type_registry *registry) {
    entity_type_slot **slots;
    size_t capacity, bucket_count, *buckets, i, bucket;
    if (registry->count < registry->capacity) return type_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_REGISTER);
    if (registry->capacity > ((size_t) - 1 - 1) / 4) return type_status(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_OPERATION_REGISTER);
    capacity = registry->capacity ? registry->capacity * 2 : 16;
    bucket_count = capacity * 2 + 1;
    if (capacity > (size_t) - 1 / sizeof(*slots) || bucket_count > (size_t) - 1 / sizeof(*buckets)) return type_status(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_OPERATION_REGISTER);
    buckets = (size_t *) calloc(bucket_count, sizeof(*buckets));
    if (!buckets) return type_status(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_REGISTER);
    slots = (entity_type_slot **) realloc(registry->slots, capacity * sizeof(*slots));
    if (!slots) {
        free(buckets);
        return type_status(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_REGISTER);
    }
    registry->slots = slots;
    for (i = 0; i < registry->count; ++i) {
        bucket = (size_t) (type_hash(slots[i]->name) % bucket_count);
        while (buckets[bucket])
            bucket = (bucket + 1) % bucket_count;
        buckets[bucket] = i + 1;
    }
    free(registry->buckets);
    registry->buckets = buckets;
    registry->bucket_count = bucket_count;
    registry->capacity = capacity;
    return type_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_REGISTER);
}
maage_status maage_entity_type_register(maage_entity_type_registry *registry, const char *name, maage_entity_type_definition *definition) {
    entity_type_slot *slot;
    maage_status result;
    size_t bucket;
    if (!registry || registry->finalized || !definition || !type_name_valid(name) || registry->count >= MAAGE_CONTENT_ID_INVALID - 1) return type_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_REGISTER);
    result = content_policy_register(registry->policy, CONTENT_ENTITY_TYPE, name, (unsigned long) registry->count + 1);
    if (maage_status_failed(result)) return result;
    result = type_grow(registry);
    if (maage_status_failed(result)) return result;
    bucket = type_bucket(registry, name);
    if (registry->buckets[bucket]) return type_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_REGISTER);
    slot = (entity_type_slot *) calloc(1, sizeof(*slot));
    if (!slot) return type_status(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_REGISTER);
    slot->name = type_string(name);
    result = type_definition_copy(&slot->definition, definition);
    if (!slot->name || maage_status_failed(result)) {
        free(slot->name);
        type_definition_clear(&slot->definition);
        free(slot);
        return maage_status_failed(result) ? result : type_status(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_REGISTER);
    }
    slot->definition.id = (maage_entity_type_id) registry->count + 1;
    registry->slots[registry->count++] = slot;
    registry->buckets[bucket] = registry->count;
    definition->id = slot->definition.id;
    return type_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_REGISTER);
}
maage_status maage_entity_type_update(maage_entity_type_registry *registry, const maage_entity_type_definition *definition) {
    maage_entity_type_definition copy;
    entity_type_slot *slot;
    maage_status result;
    if (!registry || registry->finalized || !definition || !definition->id || definition->id > registry->count) return type_status(MAAGE_STATUS_INVALID_STATE, MAAGE_OPERATION_SET);
    slot = registry->slots[definition->id - 1];
    result = content_policy_update(registry->policy, CONTENT_ENTITY_TYPE, slot->name);
    if (maage_status_failed(result)) return result;
    result = type_definition_copy(&copy, definition);
    if (maage_status_failed(result)) return result;
    type_definition_clear(&slot->definition);
    slot->definition = copy;
    return type_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_SET);
}
static int type_number(double value, double low, double high) {
    return value >= low && value <= high;
}
static void type_spawns_clear(maage_entity_type_registry *registry) {
    size_t i;
    if (registry->spawns)
        for (i = 0; i < registry->biome_count * 3; ++i)
            free(registry->spawns[i].entries);
    free(registry->spawns);
    registry->spawns = NULL;
    registry->biome_count = 0;
}
static maage_status type_spawns_build(maage_entity_type_registry *registry, const maage_biome_registry *biomes) {
    size_t pass, i, j, k, count, biome;
    const maage_entity_type_definition *definition;
    const maage_entity_spawn_rule *rule;
    entity_spawn_table *table;
    registry->biome_count = maage_biome_registry_count(biomes);
    if (registry->biome_count > (size_t) - 1 / 3 / sizeof(*registry->spawns)) return type_status(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_OPERATION_FINALIZE);
    registry->spawns = (entity_spawn_table *) calloc(registry->biome_count ? registry->biome_count * 3 : 1, sizeof(*registry->spawns));
    if (!registry->spawns) return type_status(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_FINALIZE);
    for (pass = 0; pass < 2; ++pass) {
        for (i = 0; i < registry->count; ++i) {
            definition = &registry->slots[i]->definition;
            for (j = 0; j < definition->spawn_count; ++j) {
                rule = &definition->spawns[j];
                if (!rule->population) continue;
                count = rule->biome_count ? rule->biome_count : registry->biome_count;
                for (k = 0; k < count; ++k) {
                    biome = rule->biome_count ? (size_t) rule->biomes[k] : k;
                    if (biome >= registry->biome_count) goto malformed;
                    table = &registry->spawns[biome * 3 + definition->classification - 1];
                    if (!pass) {
                        if (table->capacity == (size_t) - 1 / sizeof(*table->entries)) goto overflow;
                        ++table->capacity;
                    } else {
                        table->total += (double) rule->weight;
                        table->entries[table->count].definition = definition;
                        table->entries[table->count].rule = rule;
                        table->entries[table->count++].upper = table->total;
                    }
                }
            }
        }
        if (!pass)
            for (i = 0; i < registry->biome_count * 3; ++i) {
                table = &registry->spawns[i];
                if (!table->capacity) continue;
                table->entries = (entity_spawn_entry *) malloc(table->capacity * sizeof(*table->entries));
                if (!table->entries) {
                    type_spawns_clear(registry);
                    return type_status(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_FINALIZE);
                }
            }
    }
    return type_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_FINALIZE);
malformed:
    type_spawns_clear(registry);
    return type_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_FINALIZE);
overflow:
    type_spawns_clear(registry);
    return type_status(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_OPERATION_FINALIZE);
}
maage_status maage_entity_spawn_select(const maage_entity_type_registry *registry, unsigned int classification, maage_biome_id biome, unsigned long random, const maage_entity_type_definition **definition, const maage_entity_spawn_rule **rule) {
    const entity_spawn_table *table;
    size_t low, high, middle;
    double sample;
    if (definition) *definition = NULL;
    if (rule) *rule = NULL;
    if (!registry || !registry->finalized || !definition || !rule || classification < 1 || classification > 3 || biome >= registry->biome_count || random > 4294967295UL) return type_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_SELECT);
    table = &registry->spawns[(size_t) biome * 3 + classification - 1];
    if (!table->count) return type_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_SELECT);
    sample = ((double) random / 4294967296.0) * table->total;
    low = 0;
    high = table->count;
    while (low < high) {
        middle = low + (high - low) / 2;
        if (sample < table->entries[middle].upper) high = middle;
        else low = middle + 1;
    }
    if (low >= table->count) low = table->count - 1;
    *definition = table->entries[low].definition;
    *rule = table->entries[low].rule;
    return type_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_SELECT);
}
maage_status maage_entity_type_finalize(maage_entity_type_registry *registry, const maage_item_registry *items, const maage_block_registry *blocks, const maage_biome_registry *biomes) {
    const maage_entity_type_definition *definition;
    const maage_entity_spawn_rule *spawn;
    const maage_entity_loot *loot;
    const maage_item_definition *item;
    size_t i, j, k;
    unsigned long weight;
    maage_status result;
    if (!registry || registry->finalized || !items || !blocks || !biomes) return type_status(MAAGE_STATUS_INVALID_STATE, MAAGE_OPERATION_FINALIZE);
    result = content_policy_finalize(registry->policy);
    if (maage_status_failed(result)) return result;
    for (i = 0; i < registry->count; ++i) {
        definition = &registry->slots[i]->definition;
        if (definition->classification < MAAGE_ENTITY_CLASS_PASSIVE || definition->classification > MAAGE_ENTITY_CLASS_PROJECTILE || !type_number(definition->width, 0.001, MAAGE_WORLD_HEIGHT) || !type_number(definition->height, 0.001, MAAGE_WORLD_HEIGHT) || !type_number(definition->eye_height, 0, definition->height) || !definition->health || !type_number(definition->speed, 0, 4) || !type_number(definition->attack_damage, 0, 65535) || !type_number(definition->attack_range, 0, MAAGE_WORLD_LIMIT) || !type_number(definition->follow_range, 0, MAAGE_WORLD_LIMIT) || !type_number(definition->jump_velocity, 0, 4) || !type_number(definition->gravity, 0, 4) || !type_number(definition->fall_speed_limit, 0, 4) || !type_number(definition->explosion_radius, 0, MAAGE_WORLD_HEIGHT) || !type_number(definition->projectile_speed, 0, 4) || !type_number(definition->despawn_distance, 0, MAAGE_WORLD_LIMIT) || definition->despawn_ticks > 4294967295UL || (definition->classification != MAAGE_ENTITY_CLASS_PROJECTILE && !definition->behavior)) return type_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_FINALIZE);
        if (definition->projectile_type && (!maage_entity_type_get(registry, definition->projectile_type) || maage_entity_type_get(registry, definition->projectile_type)->classification != MAAGE_ENTITY_CLASS_PROJECTILE)) return type_status(MAAGE_STATUS_NOT_FOUND, MAAGE_OPERATION_FINALIZE);
        if (definition->state_size && (!definition->state_version || !definition->encoded_size || definition->state_version > 4294967295UL || definition->encoded_size > 4294967295UL || !definition->initialize || !definition->valid || !definition->encode || !definition->decode)) return type_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_FINALIZE);
        if (!definition->state_size && (definition->encoded_size || definition->state_version || definition->encode || definition->decode)) return type_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_FINALIZE);
        for (j = 0; j < definition->loot_count; ++j) {
            loot = &definition->loot[j];
            item = maage_item_registry_get(items, loot->stack.item_id);
            if (!item || !maage_item_stack_valid(items, &loot->stack) || !loot->stack.count || loot->min_count > loot->max_count || loot->max_count > 4294967295UL || !loot->chance_denominator || loot->chance_denominator > 4294967295UL || loot->chance_numerator > loot->chance_denominator || (loot->required_flags & loot->excluded_flags) || (loot->group && !loot->weight)) return type_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_FINALIZE);
        }
        for (j = 0; j < definition->loot_count; ++j) {
            if (!definition->loot[j].group) continue;
            weight = 0;
            for (k = 0; k < definition->loot_count; ++k) {
                if (definition->loot[k].group != definition->loot[j].group) continue;
                if (definition->loot[k].weight > 4294967295UL - weight) return type_status(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_OPERATION_FINALIZE);
                weight += definition->loot[k].weight;
            }
        }
        for (j = 0; j < definition->spawn_count; ++j) {
            spawn = &definition->spawns[j];
            if (!spawn->weight || spawn->weight > 4294967295UL || !spawn->group_min || spawn->group_max < spawn->group_min || spawn->min_light > spawn->max_light || spawn->max_light > 15 || spawn->min_y < 0 || spawn->max_y >= MAAGE_WORLD_HEIGHT || spawn->max_y < spawn->min_y || !type_number(spawn->min_player_distance, 0, MAAGE_WORLD_LIMIT) || !type_number(spawn->max_player_distance, spawn->min_player_distance, MAAGE_WORLD_LIMIT) || spawn->time_start >= 24000 || spawn->time_end >= 24000 || (spawn->fluid && spawn->fluid != MAAGE_CONTENT_ID_INVALID && !maage_fluid_get(maage_block_registry_fluids(blocks), spawn->fluid)) || (spawn->require_grass && spawn->surface == MAAGE_CONTENT_ID_INVALID) || (spawn->surface != MAAGE_CONTENT_ID_INVALID && !maage_block_registry_block(blocks, spawn->surface))) return type_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_FINALIZE);
            for (k = 0; k < spawn->biome_count; ++k)
                if (!maage_biome_registry_get(biomes, spawn->biomes[k]) || (k && spawn->biomes[k - 1] == spawn->biomes[k])) return type_status(MAAGE_STATUS_NOT_FOUND, MAAGE_OPERATION_FINALIZE);
        }
    }
    result = type_spawns_build(registry, biomes);
    if (maage_status_failed(result)) return result;
    registry->finalized = 1;
    return type_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_FINALIZE);
}
const maage_entity_type_definition *maage_entity_type_get(const maage_entity_type_registry *registry, maage_entity_type_id id) {
    return registry && id && id <= registry->count ? &registry->slots[id - 1]->definition : NULL;
}
const maage_entity_type_definition *maage_entity_type_at(const maage_entity_type_registry *registry, size_t index) {
    return registry && index < registry->count ? &registry->slots[index]->definition : NULL;
}
size_t maage_entity_type_count(const maage_entity_type_registry *registry) {
    return registry ? registry->count : 0;
}
int maage_entity_type_registry_is_finalized(const maage_entity_type_registry *registry) {
    return registry && registry->finalized;
}
const char *maage_entity_type_name(const maage_entity_type_registry *registry, maage_entity_type_id id) {
    return registry && id && id <= registry->count ? registry->slots[id - 1]->name : NULL;
}
maage_status maage_entity_type_resolve_name(const maage_entity_type_registry *registry, const char *name, maage_entity_type_id *id) {
    size_t bucket;
    maage_status result;
    if (id) *id = 0;
    if (!registry || !name || !id) return type_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_RESOLVE);
    result = content_policy_read(registry->policy, CONTENT_ENTITY_TYPE, name);
    if (maage_status_failed(result)) return result;
    if (!registry->bucket_count) return type_status(MAAGE_STATUS_NOT_FOUND, MAAGE_OPERATION_RESOLVE);
    bucket = type_bucket(registry, name);
    if (!registry->buckets[bucket]) return type_status(MAAGE_STATUS_NOT_FOUND, MAAGE_OPERATION_RESOLVE);
    *id = registry->slots[registry->buckets[bucket] - 1]->definition.id;
    return type_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_RESOLVE);
}
maage_status maage_entity_type_destroy(maage_entity_type_registry *registry) {
    size_t i;
    if (!registry) return type_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_DESTROY);
    for (i = 0; i < registry->count; ++i) {
        type_definition_clear(&registry->slots[i]->definition);
        free(registry->slots[i]->name);
        free(registry->slots[i]);
    }
    type_spawns_clear(registry);
    free(registry->slots);
    free(registry->buckets);
    free(registry);
    return type_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_DESTROY);
}
void content_entity_types_policy(maage_entity_type_registry *registry, content_policy *policy) {
    registry->policy = policy;
}
