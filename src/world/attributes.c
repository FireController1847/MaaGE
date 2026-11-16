#include "maage/attributes.h"
#include "registry_policy.h"
#include "maage/world.h"
#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct attribute_entry {
    maage_attribute_definition definition;
    char *name;
} attribute_entry;
typedef struct attribute_value {
    double base, additive, multiplier;
} attribute_value;
struct maage_attribute_registry {
    attribute_entry *entries;
    size_t count, capacity, *buckets, bucket_count;
    maage_attribute_id roles[MAAGE_ATTRIBUTE_ROLE_COUNT];
    content_policy *policy;
    int finalized;
};
struct maage_attribute_set {
    const maage_attribute_registry *registry;
    attribute_value *values;
    maage_attribute_modifier *modifiers;
    size_t count, capacity;
};

static maage_status attribute_status(unsigned char code, unsigned char operation) {
    return maage_status_make(code, MAAGE_CONTEXT_REGISTRY, operation, 0);
}
static int attribute_finite(double value) {
    return value >= -DBL_MAX && value <= DBL_MAX;
}
static int attribute_name_valid(const char *name) {
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
static unsigned long attribute_hash(const char *name) {
    unsigned long hash = 2166136261UL;
    while (*name) hash = ((hash ^ (unsigned char) *name++) * 16777619UL) & 0xffffffffUL;
    return hash;
}
static size_t attribute_bucket(const maage_attribute_registry *registry, const char *name) {
    size_t bucket = (size_t) (attribute_hash(name) % registry->bucket_count), entry;
    while ((entry = registry->buckets[bucket]) != 0) {
        if (!strcmp(registry->entries[entry - 1].name, name)) break;
        bucket = (bucket + 1) % registry->bucket_count;
    }
    return bucket;
}
static maage_status attribute_grow(maage_attribute_registry *registry) {
    attribute_entry *entries;
    size_t capacity, bucket_count, *buckets, i, bucket;
    if (registry->count < registry->capacity) return attribute_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_REGISTER);
    if (registry->capacity > ((size_t) - 1 - 1) / 4) return attribute_status(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_OPERATION_REGISTER);
    capacity = registry->capacity ? registry->capacity * 2 : 16;
    bucket_count = capacity * 2 + 1;
    if (capacity > (size_t) - 1 / sizeof(*entries) || bucket_count > (size_t) - 1 / sizeof(*buckets)) return attribute_status(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_OPERATION_REGISTER);
    buckets = (size_t *) calloc(bucket_count, sizeof(*buckets));
    if (!buckets) return attribute_status(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_REGISTER);
    entries = (attribute_entry *) realloc(registry->entries, capacity * sizeof(*entries));
    if (!entries) {
        free(buckets);
        return attribute_status(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_REGISTER);
    }
    registry->entries = entries;
    for (i = 0; i < registry->count; ++i) {
        bucket = (size_t) (attribute_hash(entries[i].name) % bucket_count);
        while (buckets[bucket]) bucket = (bucket + 1) % bucket_count;
        buckets[bucket] = i + 1;
    }
    free(registry->buckets);
    registry->buckets = buckets;
    registry->bucket_count = bucket_count;
    registry->capacity = capacity;
    return attribute_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_REGISTER);
}
maage_status maage_attribute_registry_create(maage_attribute_registry **output) {
    if (!output) return attribute_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_CREATE);
    *output = (maage_attribute_registry *) calloc(1, sizeof(**output));
    return attribute_status(*output ? MAAGE_STATUS_SUCCESS : MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_CREATE);
}
maage_status maage_attribute_registry_destroy(maage_attribute_registry *registry) {
    size_t i;
    if (!registry) return attribute_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_DESTROY);
    for (i = 0; i < registry->count; ++i) free(registry->entries[i].name);
    free(registry->entries);
    free(registry->buckets);
    free(registry);
    return attribute_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_DESTROY);
}
static int attribute_role_valid(const maage_attribute_definition *definition) {
    double maximum = 4, minimum = 0;
    if (definition->role == MAAGE_ATTRIBUTE_ROLE_NONE) return 1;
    if (definition->role == MAAGE_ATTRIBUTE_ROLE_ATTACK_DAMAGE) maximum = 65535;
    if (definition->role == MAAGE_ATTRIBUTE_ROLE_ATTACK_RANGE || definition->role == MAAGE_ATTRIBUTE_ROLE_FOLLOW_RANGE) maximum = MAAGE_WORLD_LIMIT;
    if (definition->role == MAAGE_ATTRIBUTE_ROLE_REACH) {
        minimum = 0.01;
        maximum = 64;
    }
    return definition->minimum >= minimum && definition->maximum <= maximum;
}
maage_status maage_attribute_register(maage_attribute_registry *registry, const char *name, maage_attribute_definition *definition) {
    maage_status result;
    size_t bucket;
    attribute_entry *entry;
    if (!registry || registry->finalized || !definition || !attribute_name_valid(name) || definition->role < MAAGE_ATTRIBUTE_ROLE_NONE || definition->role >= MAAGE_ATTRIBUTE_ROLE_COUNT || !attribute_finite(definition->minimum) || !attribute_finite(definition->maximum) || !(definition->base >= definition->minimum && definition->base <= definition->maximum) || !attribute_role_valid(definition) || registry->count >= MAAGE_CONTENT_ID_INVALID - 1) return attribute_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_REGISTER);
    result = content_policy_register(registry->policy, CONTENT_ATTRIBUTE, name, (unsigned long) registry->count + 1);
    if (maage_status_failed(result)) return result;
    if (definition->role && registry->roles[definition->role]) return attribute_status(MAAGE_STATUS_INVALID_STATE, MAAGE_OPERATION_REGISTER);
    result = attribute_grow(registry);
    if (maage_status_failed(result)) return result;
    bucket = attribute_bucket(registry, name);
    if (registry->buckets[bucket]) return attribute_status(MAAGE_STATUS_INVALID_STATE, MAAGE_OPERATION_REGISTER);
    entry = &registry->entries[registry->count];
    entry->name = (char *) malloc(strlen(name) + 1);
    if (!entry->name) return attribute_status(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_REGISTER);
    strcpy(entry->name, name);
    definition->id = (maage_attribute_id) registry->count + 1;
    entry->definition = *definition;
    registry->buckets[bucket] = ++registry->count;
    if (definition->role) registry->roles[definition->role] = definition->id;
    return attribute_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_REGISTER);
}
maage_status maage_attribute_registry_finalize(maage_attribute_registry *registry) {
    maage_status result;
    if (!registry || registry->finalized) return attribute_status(MAAGE_STATUS_INVALID_STATE, MAAGE_OPERATION_FINALIZE);
    result = content_policy_finalize(registry->policy);
    if (maage_status_failed(result)) return result;
    registry->finalized = 1;
    return attribute_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_FINALIZE);
}
size_t maage_attribute_count(const maage_attribute_registry *registry) {
    return registry ? registry->count : 0;
}
const maage_attribute_definition *maage_attribute_get(const maage_attribute_registry *registry, maage_attribute_id id) {
    return registry && id && id <= registry->count ? &registry->entries[id - 1].definition : NULL;
}
const char *maage_attribute_name(const maage_attribute_registry *registry, maage_attribute_id id) {
    return registry && id && id <= registry->count ? registry->entries[id - 1].name : NULL;
}
maage_status maage_attribute_resolve_name(const maage_attribute_registry *registry, const char *name, maage_attribute_id *id) {
    maage_status result;
    size_t bucket;
    if (id) *id = 0;
    if (!registry || !name || !id) return attribute_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_RESOLVE);
    result = content_policy_read(registry->policy, CONTENT_ATTRIBUTE, name);
    if (maage_status_failed(result)) return result;
    if (!registry->bucket_count) return attribute_status(MAAGE_STATUS_NOT_FOUND, MAAGE_OPERATION_RESOLVE);
    bucket = attribute_bucket(registry, name);
    if (!registry->buckets[bucket]) return attribute_status(MAAGE_STATUS_NOT_FOUND, MAAGE_OPERATION_RESOLVE);
    *id = (maage_attribute_id) registry->buckets[bucket];
    return attribute_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_RESOLVE);
}
maage_attribute_id maage_attribute_role_id(const maage_attribute_registry *registry, maage_attribute_role role) {
    return registry && role > MAAGE_ATTRIBUTE_ROLE_NONE && role < MAAGE_ATTRIBUTE_ROLE_COUNT ? registry->roles[role] : 0;
}
void content_attributes_policy(maage_attribute_registry *registry, content_policy *policy) {
    registry->policy = policy;
}

static int attribute_base_valid(const maage_attribute_definition *definition, double base) {
    return definition && base >= definition->minimum && base <= definition->maximum;
}
static int attribute_add(double a, double b, double *result) {
    if ((b > 0 && a > DBL_MAX - b) || (b < 0 && a < -DBL_MAX - b)) return 0;
    *result = a + b;
    return 1;
}
static int attribute_multiply(double a, double b, double *result) {
    if (fabs(b) > 1 && fabs(a) > DBL_MAX / fabs(b)) return 0;
    *result = a * b;
    return 1;
}
static double attribute_effective(const maage_attribute_definition *definition, const attribute_value *value, double offset) {
    double sum, result;
    if (!value->multiplier) result = 0;
    else {
        if (!attribute_add(value->base, offset, &sum)) sum = offset < 0 ? -DBL_MAX : DBL_MAX;
        if (!attribute_add(sum, value->additive, &sum)) sum = value->additive < 0 ? -DBL_MAX : DBL_MAX;
        if (!attribute_multiply(sum, value->multiplier, &result)) result = (sum < 0) != (value->multiplier < 0) ? -DBL_MAX : DBL_MAX;
    }
    if (result < definition->minimum) return definition->minimum;
    if (result > definition->maximum) return definition->maximum;
    return result;
}
maage_status maage_attributes_create(const maage_attribute_registry *registry, const maage_attribute_base *bases, size_t count, maage_attribute_set **output) {
    maage_attribute_set *set;
    size_t i, j;
    if (output) *output = NULL;
    if (!registry || !registry->finalized || !output || (count && !bases)) return attribute_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_CREATE);
    for (i = 0; i < count; ++i) {
        if (!attribute_base_valid(maage_attribute_get(registry, bases[i].attribute), bases[i].value)) return attribute_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_CREATE);
        for (j = 0; j < i; ++j)
            if (bases[j].attribute == bases[i].attribute) return attribute_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_CREATE);
    }
    if (registry->count > (size_t) - 1 / sizeof(*set->values)) return attribute_status(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_OPERATION_CREATE);
    set = (maage_attribute_set *) calloc(1, sizeof(*set));
    if (!set) return attribute_status(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_CREATE);
    set->registry = registry;
    if (registry->count) {
        set->values = (attribute_value *) calloc(registry->count, sizeof(*set->values));
        if (!set->values) {
            free(set);
            return attribute_status(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_CREATE);
        }
    }
    for (i = 0; i < registry->count; ++i) {
        set->values[i].base = registry->entries[i].definition.base;
        set->values[i].multiplier = 1;
    }
    for (i = 0; i < count; ++i) set->values[bases[i].attribute - 1].base = bases[i].value;
    *output = set;
    return attribute_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_CREATE);
}
maage_status maage_attributes_destroy(maage_attribute_set *set) {
    if (!set) return attribute_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_DESTROY);
    free(set->modifiers);
    free(set->values);
    free(set);
    return attribute_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_DESTROY);
}
maage_status maage_attributes_get_base(const maage_attribute_set *set, maage_attribute_id id, double *output) {
    if (!set || !output || !maage_attribute_get(set->registry, id)) return attribute_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_QUERY);
    *output = set->values[id - 1].base;
    return attribute_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_QUERY);
}
maage_status maage_attributes_set_base(maage_attribute_set *set, maage_attribute_id id, double base) {
    if (!set || !attribute_base_valid(maage_attribute_get(set->registry, id), base)) return attribute_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_SET);
    if (set->values[id - 1].base == base) return attribute_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_SET);
    set->values[id - 1].base = base;
    return attribute_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_SET);
}
maage_status maage_attributes_get(const maage_attribute_set *set, maage_attribute_id id, double *output) {
    const maage_attribute_definition *definition = set ? maage_attribute_get(set->registry, id) : NULL;
    if (!definition || !output) return attribute_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_QUERY);
    *output = attribute_effective(definition, &set->values[id - 1], 0);
    return attribute_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_QUERY);
}
double maage_attributes_role_offset(const maage_attribute_set *set, maage_attribute_role role, double offset, double fallback) {
    maage_attribute_id id = set ? maage_attribute_role_id(set->registry, role) : 0;
    if (!id || !attribute_finite(offset)) return fallback;
    return attribute_effective(maage_attribute_get(set->registry, id), &set->values[id - 1], offset);
}
double maage_attributes_role_value(const maage_attribute_set *set, maage_attribute_role role, double fallback) {
    return maage_attributes_role_offset(set, role, 0, fallback);
}
maage_status maage_attributes_set_role_base(maage_attribute_set *set, maage_attribute_role role, double base) {
    maage_attribute_id id = set ? maage_attribute_role_id(set->registry, role) : 0;
    if (!id) return attribute_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_SET);
    return maage_attributes_set_base(set, id, base);
}

static int attribute_modifier_compare(const maage_attribute_modifier *a, const maage_attribute_modifier *b) {
    if (a->attribute != b->attribute) return a->attribute < b->attribute ? -1 : 1;
    if (a->source != b->source) return a->source < b->source ? -1 : 1;
    if (a->key != b->key) return a->key < b->key ? -1 : 1;
    return 0;
}
static size_t attribute_modifier_lower(const maage_attribute_set *set, const maage_attribute_modifier *modifier) {
    size_t low = 0, high = set->count, middle;
    while (low < high) {
        middle = low + (high - low) / 2;
        if (attribute_modifier_compare(&set->modifiers[middle], modifier) < 0) low = middle + 1;
        else high = middle;
    }
    return low;
}
static int attribute_accumulate(const maage_attribute_modifier *modifier, double *additive, double *multiplier) {
    return modifier->operation == MAAGE_ATTRIBUTE_ADD ? attribute_add(*additive, modifier->amount, additive) : attribute_multiply(*multiplier, modifier->amount, multiplier);
}
/* Evaluate the proposed change before mutating ownership or cached totals. */
static int attribute_totals(const maage_attribute_set *set, const maage_attribute_modifier *key, size_t index, int exists, int insert, double *additive, double *multiplier) {
    maage_attribute_modifier first = *key;
    size_t i;
    first.source = first.key = 0;
    *additive = 0;
    *multiplier = 1;
    i = attribute_modifier_lower(set, &first);
    while (i < set->count && set->modifiers[i].attribute == key->attribute) {
        if (i == index && insert && !attribute_accumulate(key, additive, multiplier)) return 0;
        if (!(i == index && exists) && !attribute_accumulate(&set->modifiers[i], additive, multiplier)) return 0;
        ++i;
    }
    return !(i == index && insert) || attribute_accumulate(key, additive, multiplier);
}
static maage_status attribute_modifier_reserve(maage_attribute_set *set) {
    size_t capacity;
    maage_attribute_modifier *modifiers;
    if (set->count < set->capacity) return attribute_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_SET);
    if (set->capacity > (size_t) - 1 / 2) return attribute_status(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_OPERATION_SET);
    capacity = set->capacity ? set->capacity * 2 : 4;
    if (capacity > (size_t) - 1 / sizeof(*modifiers)) return attribute_status(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_OPERATION_SET);
    modifiers = (maage_attribute_modifier *) realloc(set->modifiers, capacity * sizeof(*modifiers));
    if (!modifiers) return attribute_status(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_SET);
    set->modifiers = modifiers;
    set->capacity = capacity;
    return attribute_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_SET);
}
maage_status maage_attributes_set_modifier(maage_attribute_set *set, const maage_attribute_modifier *modifier) {
    size_t index;
    int exists;
    double additive, multiplier;
    maage_status result;
    if (!set || !modifier || !maage_attribute_get(set->registry, modifier->attribute) || !modifier->source || modifier->source > MAAGE_CONTENT_ID_INVALID || modifier->key > MAAGE_CONTENT_ID_INVALID || (modifier->operation != MAAGE_ATTRIBUTE_ADD && modifier->operation != MAAGE_ATTRIBUTE_MULTIPLY) || !attribute_finite(modifier->amount)) return attribute_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_SET);
    index = attribute_modifier_lower(set, modifier);
    exists = index < set->count && !attribute_modifier_compare(&set->modifiers[index], modifier);
    if (exists && set->modifiers[index].operation == modifier->operation && set->modifiers[index].amount == modifier->amount) return attribute_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_SET);
    if (!attribute_totals(set, modifier, index, exists, 1, &additive, &multiplier)) return attribute_status(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_OPERATION_SET);
    if (!exists) {
        result = attribute_modifier_reserve(set);
        if (maage_status_failed(result)) return result;
        if (set->count != index) memmove(&set->modifiers[index + 1], &set->modifiers[index], (set->count - index) * sizeof(*set->modifiers));
        ++set->count;
    }
    set->modifiers[index] = *modifier;
    set->values[modifier->attribute - 1].additive = additive;
    set->values[modifier->attribute - 1].multiplier = multiplier;
    return attribute_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_SET);
}
maage_status maage_attributes_remove_modifier(maage_attribute_set *set, maage_attribute_id id, unsigned long source, unsigned long key) {
    maage_attribute_modifier modifier;
    size_t index;
    double additive, multiplier;
    if (!set || !maage_attribute_get(set->registry, id) || !source || source > MAAGE_CONTENT_ID_INVALID || key > MAAGE_CONTENT_ID_INVALID) return attribute_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_REMOVE);
    modifier.attribute = id;
    modifier.source = source;
    modifier.key = key;
    modifier.operation = MAAGE_ATTRIBUTE_ADD;
    modifier.amount = 0;
    index = attribute_modifier_lower(set, &modifier);
    if (index >= set->count || attribute_modifier_compare(&set->modifiers[index], &modifier)) return attribute_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_REMOVE);
    if (!attribute_totals(set, &modifier, index, 1, 0, &additive, &multiplier)) return attribute_status(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_OPERATION_REMOVE);
    if (index + 1 < set->count) memmove(&set->modifiers[index], &set->modifiers[index + 1], (set->count - index - 1) * sizeof(*set->modifiers));
    --set->count;
    set->values[id - 1].additive = additive;
    set->values[id - 1].multiplier = multiplier;
    return attribute_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_REMOVE);
}
