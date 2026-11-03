#include "maage/traits.h"
#include "registry_policy.h"
#include <stdlib.h>
#include <string.h>

typedef struct trait_name {
    char *name;
    unsigned long id;
    content_domain domain;
} trait_name;
typedef struct trait_property {
    maage_property_definition definition;
    const char *name;
    size_t value_count;
} trait_property;
typedef struct trait_value {
    maage_property_id property;
    const char *name;
} trait_value;
typedef struct trait_tag {
    maage_tag_definition definition;
    const char *name;
    unsigned char *flat;
    int visiting, complete;
} trait_tag;
typedef struct trait_behavior {
    maage_behavior_definition definition;
    const char *name;
} trait_behavior;
typedef struct trait_subject {
    maage_tag_id *tags;
    size_t tag_count;
    maage_property_set_id constants;
    unsigned char *membership;
    maage_behavior_id *dispatch;
    size_t dispatch_count;
    int assigned;
} trait_subject;
struct maage_traits_registry {
    content_policy *policy;
    trait_name *names;
    size_t name_count, name_capacity, *name_buckets, name_bucket_count;
    trait_property *properties;
    size_t property_count, property_capacity;
    trait_value *values;
    size_t value_count, value_capacity;
    trait_tag *tags;
    size_t tag_count, tag_capacity;
    trait_behavior *behaviors;
    size_t behavior_count, behavior_capacity;
    maage_property_set *sets;
    size_t set_count, set_capacity, *set_buckets, set_bucket_count;
    trait_subject *subjects[2];
    size_t subject_count[2], subject_capacity[2];
    int finalized;
};
static maage_status trait_result(unsigned char code, unsigned char operation) {
    return maage_status_make(code, MAAGE_CONTEXT_REGISTRY, operation, 0);
}
static maage_status trait_fail(maage_traits_registry *registry, unsigned char code, unsigned char operation, const char *name, const char *reason) {
    maage_status result = trait_result(code, operation);
    content_policy *policy = registry ? registry->policy : NULL;
    if (policy && !registry->finalized && !maage_status_failed(policy->failure)) {
        policy->failure = result;
        policy->reason = reason;
        if (name) {
            strncpy(policy->content_name, name, sizeof(policy->content_name) - 1);
            policy->content_name[sizeof(policy->content_name) - 1] = '\0';
        }
        if (policy->context) MAAGE_STATUS_TRACE(policy->context->trace, result, MAAGE_CONTEXT_REGISTRY, operation);
    }
    return result;
}
static void *trait_storage(void *data, size_t *capacity, size_t needed, size_t width) {
    size_t next = *capacity ? *capacity : 8;
    void *grown;
    if (needed <= *capacity) return data;
    while (next < needed) {
        if (next > (size_t) - 1 / 2) return NULL;
        next *= 2;
    }
    if (next > (size_t) - 1 / width) return NULL;
    grown = realloc(data, next * width);
    if (!grown) return NULL;
    memset((unsigned char *) grown + *capacity * width, 0, (next - *capacity) * width);
    *capacity = next;
    return grown;
}
/* Typed assignment preserves ISO C pointer conversion and aliasing rules. */
#define TRAIT_RESERVE(type, name) \
static int name(type **data, size_t *capacity, size_t needed) { \
    void *grown = trait_storage(*data, capacity, needed, sizeof(**data)); \
    if (!grown) return 0; \
    *data = (type *) grown; \
    return 1; \
}
TRAIT_RESERVE(trait_name, trait_reserve_names)
TRAIT_RESERVE(maage_property_set, trait_reserve_sets)
TRAIT_RESERVE(trait_property, trait_reserve_properties)
TRAIT_RESERVE(trait_value, trait_reserve_values)
TRAIT_RESERVE(trait_behavior, trait_reserve_behaviors)
TRAIT_RESERVE(trait_tag, trait_reserve_tags)
TRAIT_RESERVE(trait_subject, trait_reserve_subjects)
#undef TRAIT_RESERVE
static void *trait_copy(const void *data, size_t count, size_t width) {
    void *copy;
    if (!count) return NULL;
    if (!data || count > (size_t) - 1 / width) return NULL;
    copy = malloc(count * width);
    if (copy) memcpy(copy, data, count * width);
    return copy;
}
static int trait_name_valid(const char *name) {
    size_t length = 0;
    int colon = 0;
    unsigned char c;
    if (!name) return 0;
    while ((c = (unsigned char) name[length]) != 0) {
        if (length >= 255) return 0;
        if (c == ':') {
            if (colon || !length || !name[length + 1]) return 0;
            colon = 1;
        } else if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.' || (colon && c == '/'))) return 0;
        ++length;
    }
    return colon;
}
static unsigned long trait_name_hash(content_domain domain, const char *name) {
    unsigned long hash = 2166136261UL ^ (unsigned long) domain;
    while (*name)
        hash = ((hash ^ (unsigned char) *name++) * 16777619UL) & 0xffffffffUL;
    return hash;
}
static size_t trait_name_bucket(const maage_traits_registry *registry, content_domain domain, const char *name) {
    size_t bucket = (size_t) (trait_name_hash(domain, name) % registry->name_bucket_count), index;
    while ((index = registry->name_buckets[bucket]) != 0) {
        if (registry->names[index - 1].domain == domain && !strcmp(registry->names[index - 1].name, name)) break;
        bucket = (bucket + 1) % registry->name_bucket_count;
    }
    return bucket;
}
static int trait_names_grow(maage_traits_registry *registry) {
    size_t count, i, bucket, *buckets;
    if (registry->name_count + 1 < registry->name_bucket_count / 2) return 1;
    if (registry->name_bucket_count > ((size_t) - 1 - 1) / 2) return 0;
    count = registry->name_bucket_count ? registry->name_bucket_count * 2 + 1 : 31;
    if (count > (size_t) - 1 / sizeof(*buckets)) return 0;
    buckets = (size_t *) calloc(count, sizeof(*buckets));
    if (!buckets) return 0;
    for (i = 0; i < registry->name_count; ++i) {
        bucket = (size_t) (trait_name_hash(registry->names[i].domain, registry->names[i].name) % count);
        while (buckets[bucket])
            bucket = (bucket + 1) % count;
        buckets[bucket] = i + 1;
    }
    free(registry->name_buckets);
    registry->name_buckets = buckets;
    registry->name_bucket_count = count;
    return 1;
}
static maage_status trait_name_add(maage_traits_registry *registry, content_domain domain, const char *name, unsigned long id, const char **stored) {
    maage_status result;
    trait_name *slot;
    size_t bucket;
    if (!registry || !trait_name_valid(name)) return trait_fail(registry, MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_REGISTER, name, "Invalid namespaced trait identity");
    if (registry->finalized) return trait_result(MAAGE_STATUS_INVALID_STATE, MAAGE_OPERATION_REGISTER);
    if (!trait_names_grow(registry) || !trait_reserve_names(&registry->names, &registry->name_capacity, registry->name_count + 1)) return trait_fail(registry, MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_REGISTER, name, "Trait identity allocation");
    bucket = trait_name_bucket(registry, domain, name);
    if (registry->name_buckets[bucket]) return trait_fail(registry, MAAGE_STATUS_INVALID_STATE, MAAGE_OPERATION_REGISTER, name, "Duplicate trait identity");
    result = content_policy_register(registry->policy, domain, name, id);
    if (maage_status_failed(result)) return result;
    slot = &registry->names[registry->name_count];
    slot->name = (char *) trait_copy(name, strlen(name) + 1, 1);
    if (!slot->name) return trait_fail(registry, MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_REGISTER, name, "Trait name allocation");
    slot->domain = domain;
    slot->id = id;
    registry->name_buckets[bucket] = ++registry->name_count;
    *stored = slot->name;
    return trait_result(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_REGISTER);
}
static maage_status trait_name_find(const maage_traits_registry *registry, content_domain domain, const char *name, unsigned long *id) {
    size_t index;
    maage_status result;
    if (!registry || !name || !id) return trait_result(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_RESOLVE);
    result = content_policy_read(registry->policy, domain, name);
    if (maage_status_failed(result)) return result;
    if (!registry->name_bucket_count) return trait_result(MAAGE_STATUS_NOT_FOUND, MAAGE_OPERATION_RESOLVE);
    index = registry->name_buckets[trait_name_bucket(registry, domain, name)];
    if (!index) return trait_result(MAAGE_STATUS_NOT_FOUND, MAAGE_OPERATION_RESOLVE);
    *id = registry->names[index - 1].id;
    return trait_result(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_RESOLVE);
}
void content_traits_policy(maage_traits_registry *registry, content_policy *policy) {
    if (registry) registry->policy = policy;
}
maage_status maage_traits_registry_create(maage_traits_registry **output) {
    maage_traits_registry *registry;
    if (!output) return trait_result(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_CREATE);
    *output = NULL;
    registry = (maage_traits_registry *) calloc(1, sizeof(*registry));
    if (!registry) return trait_result(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_CREATE);
    if (!trait_reserve_sets(&registry->sets, &registry->set_capacity, 1)) {
        free(registry);
        return trait_result(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_CREATE);
    }
    registry->set_count = 1;
    *output = registry;
    return trait_result(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_CREATE);
}
maage_status maage_traits_registry_destroy(maage_traits_registry *registry) {
    size_t i, domain;
    if (!registry) return trait_result(MAAGE_STATUS_NOOP, MAAGE_OPERATION_DESTROY);
    for (i = 0; i < registry->name_count; ++i)
        free(registry->names[i].name);
    for (i = 0; i < registry->set_count; ++i)
        free((void *) registry->sets[i].values);
    for (i = 0; i < registry->tag_count; ++i) {
        free((void *) registry->tags[i].definition.required);
        free((void *) registry->tags[i].definition.includes);
        free((void *) registry->tags[i].definition.behaviors);
        free(registry->tags[i].flat);
    }
    for (i = 0; i < registry->behavior_count; ++i)
        free((void *) registry->behaviors[i].definition.after);
    for (domain = 0; domain < 2; ++domain) {
        for (i = 0; i < registry->subject_count[domain]; ++i) {
            free(registry->subjects[domain][i].tags);
            free(registry->subjects[domain][i].membership);
            free(registry->subjects[domain][i].dispatch);
        }
        free(registry->subjects[domain]);
    }
    free(registry->names);
    free(registry->name_buckets);
    free(registry->properties);
    free(registry->values);
    free(registry->tags);
    free(registry->behaviors);
    free(registry->sets);
    free(registry->set_buckets);
    free(registry);
    return trait_result(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_DESTROY);
}
int maage_traits_registry_is_finalized(const maage_traits_registry *registry) {
    return registry && registry->finalized;
}
size_t maage_property_count(const maage_traits_registry *registry) {
    return registry ? registry->property_count : 0;
}
size_t maage_tag_count(const maage_traits_registry *registry) {
    return registry ? registry->tag_count : 0;
}
size_t maage_behavior_count(const maage_traits_registry *registry) {
    return registry ? registry->behavior_count : 0;
}
size_t maage_property_set_count(const maage_traits_registry *registry) {
    return registry ? registry->set_count : 0;
}
const maage_property_definition *maage_property_get(const maage_traits_registry *registry, maage_property_id id) {
    return registry && id < registry->property_count ? &registry->properties[id].definition : NULL;
}
const char *maage_property_name(const maage_traits_registry *registry, maage_property_id id) {
    return registry && id < registry->property_count ? registry->properties[id].name : NULL;
}
const maage_tag_definition *maage_tag_get(const maage_traits_registry *registry, maage_tag_id id) {
    return registry && id < registry->tag_count ? &registry->tags[id].definition : NULL;
}
const char *maage_tag_name(const maage_traits_registry *registry, maage_tag_id id) {
    return registry && id < registry->tag_count ? registry->tags[id].name : NULL;
}
const maage_behavior_definition *maage_behavior_get(const maage_traits_registry *registry, maage_behavior_id id) {
    return registry && id < registry->behavior_count ? &registry->behaviors[id].definition : NULL;
}
const char *maage_behavior_name(const maage_traits_registry *registry, maage_behavior_id id) {
    return registry && id < registry->behavior_count ? registry->behaviors[id].name : NULL;
}
maage_status maage_property_find(const maage_traits_registry *registry, const char *name, maage_property_id *id) {
    return trait_name_find(registry, CONTENT_PROPERTY, name, id);
}
maage_status maage_tag_find(const maage_traits_registry *registry, const char *name, maage_tag_id *id) {
    return trait_name_find(registry, CONTENT_TAG, name, id);
}
maage_status maage_behavior_find(const maage_traits_registry *registry, const char *name, maage_behavior_id *id) {
    return trait_name_find(registry, CONTENT_BEHAVIOR, name, id);
}
maage_status maage_property_register(maage_traits_registry *registry, const char *name, maage_property_definition *definition) {
    maage_status result;
    trait_property *slot;
    if (!registry || !definition || definition->kind < MAAGE_PROPERTY_BOOLEAN || definition->kind > MAAGE_PROPERTY_IDENTITY || (definition->kind == MAAGE_PROPERTY_INTEGER && (definition->minimum > definition->maximum || definition->maximum > 4294967295UL))) return trait_fail(registry, MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_REGISTER, name, "Invalid property type or range");
    if (registry->finalized) return trait_result(MAAGE_STATUS_INVALID_STATE, MAAGE_OPERATION_REGISTER);
    if (registry->property_count >= MAAGE_CONTENT_ID_INVALID) return trait_fail(registry, MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_OPERATION_REGISTER, name, "Property ID domain exhausted");
    if (!trait_reserve_properties(&registry->properties, &registry->property_capacity, registry->property_count + 1)) return trait_fail(registry, MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_REGISTER, name, "Property allocation");
    slot = &registry->properties[registry->property_count];
    result = trait_name_add(registry, CONTENT_PROPERTY, name, (unsigned long) registry->property_count, &slot->name);
    if (maage_status_failed(result)) return result;
    definition->id = (unsigned long) registry->property_count++;
    if (definition->kind == MAAGE_PROPERTY_BOOLEAN) {
        definition->minimum = 0;
        definition->maximum = 1;
    }
    if (definition->kind == MAAGE_PROPERTY_IDENTITY) definition->minimum = definition->maximum = 0;
    slot->definition = *definition;
    return result;
}
maage_status maage_property_value_register(maage_traits_registry *registry, maage_property_id property, const char *name, maage_property_value *value) {
    maage_status result;
    trait_value *slot;
    const maage_property_definition *definition = maage_property_get(registry, property);
    if (!definition || definition->kind != MAAGE_PROPERTY_IDENTITY || !value) return trait_fail(registry, MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_REGISTER, name, "Identity value requires an identity property");
    if (registry->finalized) return trait_result(MAAGE_STATUS_INVALID_STATE, MAAGE_OPERATION_REGISTER);
    result = content_policy_read(registry->policy, CONTENT_PROPERTY, maage_property_name(registry, property));
    if (maage_status_failed(result)) return result;
    if (registry->value_count >= MAAGE_CONTENT_ID_INVALID) return trait_fail(registry, MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_OPERATION_REGISTER, name, "Property value ID domain exhausted");
    if (!trait_reserve_values(&registry->values, &registry->value_capacity, registry->value_count + 1)) return trait_fail(registry, MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_REGISTER, name, "Property value allocation");
    slot = &registry->values[registry->value_count];
    result = trait_name_add(registry, CONTENT_PROPERTY_VALUE, name, (unsigned long) registry->value_count, &slot->name);
    if (maage_status_failed(result)) return result;
    slot->property = property;
    *value = (unsigned long) registry->value_count++;
    ++registry->properties[property].value_count;
    return result;
}
maage_status maage_property_value_find(const maage_traits_registry *registry, maage_property_id property, const char *name, maage_property_value *value) {
    unsigned long id;
    maage_status result;
    if (!value) return trait_result(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_RESOLVE);
    result = trait_name_find(registry, CONTENT_PROPERTY_VALUE, name, &id);
    if (maage_status_failed(result)) return result;
    if (registry->values[id].property != property) return trait_result(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_RESOLVE);
    *value = id;
    return result;
}
const char *maage_property_value_name(const maage_traits_registry *registry, maage_property_id property, maage_property_value value) {
    return registry && value < registry->value_count && registry->values[value].property == property ? registry->values[value].name : NULL;
}
size_t maage_property_value_count(const maage_traits_registry *registry, maage_property_id property) {
    return registry && property < registry->property_count ? registry->properties[property].value_count : 0;
}
int maage_property_value_valid(const maage_traits_registry *registry, maage_property_id property, maage_property_value value) {
    const maage_property_definition *definition = maage_property_get(registry, property);
    if (!definition) return 0;
    if (definition->kind == MAAGE_PROPERTY_IDENTITY) return maage_property_value_name(registry, property, value) != NULL;
    return value >= definition->minimum && value <= definition->maximum;
}
static unsigned long trait_assignment_hash(maage_property_id property, maage_property_value value) {
    unsigned long hash = ((property + 1) * 2654435761UL) & 0xffffffffUL;
    hash ^= (value * 2246822519UL) & 0xffffffffUL;
    hash ^= hash >> 16;
    return (hash * 3266489917UL) & 0xffffffffUL;
}
static unsigned long trait_set_hash(const maage_property_assignment *values, size_t count) {
    unsigned long hash = (unsigned long) count * 374761393UL;
    size_t i;
    for (i = 0; i < count; ++i)
        hash = (hash + trait_assignment_hash(values[i].property, values[i].value)) & 0xffffffffUL;
    return hash;
}
static int trait_assignment_compare(const void *left, const void *right) {
    const maage_property_assignment *a = (const maage_property_assignment *) left, *b = (const maage_property_assignment *) right;
    return a->property < b->property ? -1 : a->property > b->property;
}
static int trait_set_equal(const maage_property_set *set, const maage_property_assignment *values, size_t count) {
    size_t i, j;
    if (set->count != count) return 0;
    for (i = 0; i < count; ++i) {
        for (j = 0; j < count && set->values[j].property != values[i].property; ++j) {
        }
        if (j == count || set->values[j].value != values[i].value) return 0;
    }
    return 1;
}
static int trait_sets_grow(maage_traits_registry *registry) {
    size_t count, i, bucket, *buckets;
    if (registry->set_count + 1 < registry->set_bucket_count / 2) return 1;
    if (registry->set_bucket_count > ((size_t) - 1 - 1) / 2) return 0;
    count = registry->set_bucket_count ? registry->set_bucket_count * 2 + 1 : 31;
    if (count > (size_t) - 1 / sizeof(*buckets)) return 0;
    buckets = (size_t *) calloc(count, sizeof(*buckets));
    if (!buckets) return 0;
    for (i = 1; i < registry->set_count; ++i) {
        bucket = (size_t) (trait_set_hash(registry->sets[i].values, registry->sets[i].count) % count);
        while (buckets[bucket])
            bucket = (bucket + 1) % count;
        buckets[bucket] = i;
    }
    free(registry->set_buckets);
    registry->set_buckets = buckets;
    registry->set_bucket_count = count;
    return 1;
}
static maage_status trait_assignments_valid(const maage_traits_registry *registry, const maage_property_assignment *values, size_t count) {
    size_t i, j;
    if (!registry || (count && !values)) return trait_result(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_RESOLVE);
    for (i = 0; i < count; ++i) {
        if (!maage_property_value_valid(registry, values[i].property, values[i].value)) return trait_result(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_RESOLVE);
        for (j = 0; j < i; ++j)
            if (values[i].property == values[j].property) return trait_result(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_RESOLVE);
    }
    return trait_result(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_RESOLVE);
}
maage_status maage_property_set_resolve(const maage_traits_registry *registry, const maage_property_assignment *values, size_t count, maage_property_set_id *id) {
    size_t bucket, index;
    maage_status result;
    if (!id) return trait_result(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_RESOLVE);
    result = trait_assignments_valid(registry, values, count);
    if (maage_status_failed(result)) return result;
    if (!count) {
        *id = 0;
        return result;
    }
    if (!registry->set_bucket_count) return trait_result(MAAGE_STATUS_NOT_FOUND, MAAGE_OPERATION_RESOLVE);
    bucket = (size_t) (trait_set_hash(values, count) % registry->set_bucket_count);
    while ((index = registry->set_buckets[bucket]) != 0) {
        if (trait_set_equal(&registry->sets[index], values, count)) {
            *id = (unsigned long) index;
            return result;
        }
        bucket = (bucket + 1) % registry->set_bucket_count;
    }
    return trait_result(MAAGE_STATUS_NOT_FOUND, MAAGE_OPERATION_RESOLVE);
}
maage_status content_traits_read_set(const maage_traits_registry *registry, maage_property_set_id id) {
    const maage_property_set *set = maage_property_set_get(registry, id);
    maage_status result;
    size_t i;
    if (!set) return trait_result(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_RESOLVE);
    for (i = 0; i < set->count; ++i) {
        result = content_policy_read(registry->policy, CONTENT_PROPERTY, maage_property_name(registry, set->values[i].property));
        if (maage_status_failed(result)) return result;
        if (maage_property_get(registry, set->values[i].property)->kind == MAAGE_PROPERTY_IDENTITY) {
            result = content_policy_read(registry->policy, CONTENT_PROPERTY_VALUE, maage_property_value_name(registry, set->values[i].property, set->values[i].value));
            if (maage_status_failed(result)) return result;
        }
    }
    return trait_result(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_RESOLVE);
}
maage_status maage_property_set_register(maage_traits_registry *registry, const maage_property_assignment *values, size_t count, maage_property_set_id *id) {
    maage_property_assignment *copy;
    maage_status result;
    size_t i, bucket;
    if (!registry || !id || registry->finalized) return trait_result(MAAGE_STATUS_INVALID_STATE, MAAGE_OPERATION_REGISTER);
    result = trait_assignments_valid(registry, values, count);
    if (maage_status_failed(result)) return trait_fail(registry, MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_REGISTER, NULL, "Invalid or duplicate typed property assignment");
    for (i = 0; i < count; ++i) {
        result = content_policy_read(registry->policy, CONTENT_PROPERTY, maage_property_name(registry, values[i].property));
        if (maage_status_failed(result)) return result;
        if (maage_property_get(registry, values[i].property)->kind == MAAGE_PROPERTY_IDENTITY) {
            result = content_policy_read(registry->policy, CONTENT_PROPERTY_VALUE, maage_property_value_name(registry, values[i].property, values[i].value));
            if (maage_status_failed(result)) return result;
        }
    }
    result = maage_property_set_resolve(registry, values, count, id);
    if (!maage_status_failed(result)) return result;
    if (registry->set_count >= MAAGE_CONTENT_ID_INVALID) return trait_fail(registry, MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_OPERATION_REGISTER, NULL, "Property configuration ID domain exhausted");
    copy = (maage_property_assignment *) trait_copy(values, count, sizeof(*values));
    if (!copy || !trait_sets_grow(registry) || !trait_reserve_sets(&registry->sets, &registry->set_capacity, registry->set_count + 1)) {
        free(copy);
        return trait_fail(registry, MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_REGISTER, NULL, "Property configuration allocation");
    }
    qsort(copy, count, sizeof(*copy), trait_assignment_compare);
    *id = (unsigned long) registry->set_count;
    registry->sets[registry->set_count].values = copy;
    registry->sets[registry->set_count].count = count;
    bucket = (size_t) (trait_set_hash(copy, count) % registry->set_bucket_count);
    while (registry->set_buckets[bucket])
        bucket = (bucket + 1) % registry->set_bucket_count;
    registry->set_buckets[bucket] = registry->set_count++;
    return trait_result(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_REGISTER);
}
const maage_property_set *maage_property_set_get(const maage_traits_registry *registry, maage_property_set_id id) {
    return registry && id < registry->set_count ? &registry->sets[id] : NULL;
}
int maage_property_set_value(const maage_traits_registry *registry, maage_property_set_id id, maage_property_id property, maage_property_value *value) {
    const maage_property_set *set = maage_property_set_get(registry, id);
    size_t low = 0, high, middle;
    if (!set) return 0;
    high = set->count;
    while (low < high) {
        middle = low + (high - low) / 2;
        if (set->values[middle].property < property) low = middle + 1;
        else high = middle;
    }
    if (low == set->count || set->values[low].property != property) return 0;
    if (value) *value = set->values[low].value;
    return 1;
}
maage_status maage_property_set_change(const maage_traits_registry *registry, maage_property_set_id id, maage_property_id property, maage_property_value value, maage_property_set_id *output) {
    const maage_property_set *set = maage_property_set_get(registry, id), *candidate;
    maage_property_value previous;
    unsigned long hash;
    size_t bucket, index, i;
    int matches;
    if (!set || !output || !maage_property_value_valid(registry, property, value) || !maage_property_set_value(registry, id, property, &previous)) return trait_result(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_RESOLVE);
    if (previous == value) {
        *output = id;
        return trait_result(MAAGE_STATUS_NOOP, MAAGE_OPERATION_RESOLVE);
    }
    hash = (trait_set_hash(set->values, set->count) - trait_assignment_hash(property, previous) + trait_assignment_hash(property, value)) & 0xffffffffUL;
    bucket = (size_t) (hash % registry->set_bucket_count);
    while ((index = registry->set_buckets[bucket]) != 0) {
        candidate = &registry->sets[index];
        matches = candidate->count == set->count;
        for (i = 0; matches && i < set->count; ++i)
            matches = candidate->values[i].property == set->values[i].property && candidate->values[i].value == (set->values[i].property == property ? value : set->values[i].value);
        if (matches) {
            *output = (unsigned long) index;
            return trait_result(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_RESOLVE);
        }
        bucket = (bucket + 1) % registry->set_bucket_count;
    }
    return trait_result(MAAGE_STATUS_NOT_FOUND, MAAGE_OPERATION_RESOLVE);
}
maage_status maage_behavior_register(maage_traits_registry *registry, const char *name, maage_behavior_definition *definition) {
    maage_status result;
    trait_behavior *slot;
    maage_behavior_id *after;
    size_t i, j;
    if (!registry || !definition || !definition->callback || !definition->event || (definition->mode != MAAGE_BEHAVIOR_COMPOSE && definition->mode != MAAGE_BEHAVIOR_EXCLUSIVE) || (definition->after_count && !definition->after)) return trait_fail(registry, MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_REGISTER, name, "Invalid behavior contract");
    if (registry->finalized) return trait_result(MAAGE_STATUS_INVALID_STATE, MAAGE_OPERATION_REGISTER);
    for (i = 0; i < definition->after_count; ++i) {
        const maage_behavior_definition *dependency = maage_behavior_get(registry, definition->after[i]);
        if (!dependency || dependency->event != definition->event || dependency->order > definition->order) return trait_fail(registry, MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_REGISTER, name, "Behavior dependency is missing, has a different event or contradicts ordering");
        for (j = 0; j < i; ++j)
            if (definition->after[j] == definition->after[i]) return trait_fail(registry, MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_REGISTER, name, "Duplicate behavior dependency");
        result = content_policy_read(registry->policy, CONTENT_BEHAVIOR, maage_behavior_name(registry, definition->after[i]));
        if (maage_status_failed(result)) return result;
    }
    if (registry->behavior_count >= MAAGE_CONTENT_ID_INVALID) return trait_fail(registry, MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_OPERATION_REGISTER, name, "Behavior ID domain exhausted");
    after = (maage_behavior_id *) trait_copy(definition->after, definition->after_count, sizeof(*after));
    if ((definition->after_count && !after) || !trait_reserve_behaviors(&registry->behaviors, &registry->behavior_capacity, registry->behavior_count + 1)) {
        free(after);
        return trait_fail(registry, MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_REGISTER, name, "Behavior allocation");
    }
    slot = &registry->behaviors[registry->behavior_count];
    result = trait_name_add(registry, CONTENT_BEHAVIOR, name, (unsigned long) registry->behavior_count, &slot->name);
    if (maage_status_failed(result)) {
        free(after);
        return result;
    }
    definition->id = (unsigned long) registry->behavior_count++;
    slot->definition = *definition;
    slot->definition.after = after;
    return result;
}
maage_status maage_tag_register(maage_traits_registry *registry, const char *name, maage_tag_definition *definition) {
    maage_status result;
    trait_tag *slot;
    maage_property_id *required;
    maage_tag_id *includes;
    maage_behavior_id *behaviors;
    size_t i, j;
    if (!registry || !definition || !definition->domains || (definition->domains & ~(MAAGE_TRAIT_BLOCK | MAAGE_TRAIT_ITEM)) || (definition->required_count && !definition->required) || (definition->include_count && !definition->includes) || (definition->behavior_count && !definition->behaviors)) return trait_fail(registry, MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_REGISTER, name, "Invalid tag contract");
    if (registry->finalized) return trait_result(MAAGE_STATUS_INVALID_STATE, MAAGE_OPERATION_REGISTER);
    for (i = 0; i < definition->required_count; ++i) {
        if (!maage_property_get(registry, definition->required[i])) return trait_fail(registry, MAAGE_STATUS_NOT_FOUND, MAAGE_OPERATION_REGISTER, name, "Tag requires an unregistered property");
        for (j = 0; j < i; ++j)
            if (definition->required[j] == definition->required[i]) return trait_fail(registry, MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_REGISTER, name, "Duplicate property requirement");
        result = content_policy_read(registry->policy, CONTENT_PROPERTY, maage_property_name(registry, definition->required[i]));
        if (maage_status_failed(result)) return result;
    }
    for (i = 0; i < definition->include_count; ++i) {
        const maage_tag_definition *parent = maage_tag_get(registry, definition->includes[i]);
        if (!parent || (parent->domains & definition->domains) != definition->domains) return trait_fail(registry, MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_REGISTER, name, "Included tag does not cover the requested domains");
        for (j = 0; j < i; ++j)
            if (definition->includes[j] == definition->includes[i]) return trait_fail(registry, MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_REGISTER, name, "Duplicate included tag");
        result = content_policy_read(registry->policy, CONTENT_TAG, maage_tag_name(registry, definition->includes[i]));
        if (maage_status_failed(result)) return result;
    }
    for (i = 0; i < definition->behavior_count; ++i) {
        if (!maage_behavior_get(registry, definition->behaviors[i])) return trait_fail(registry, MAAGE_STATUS_NOT_FOUND, MAAGE_OPERATION_REGISTER, name, "Tag references an unregistered behavior");
        for (j = 0; j < i; ++j)
            if (definition->behaviors[j] == definition->behaviors[i]) return trait_fail(registry, MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_REGISTER, name, "Duplicate behavior contribution");
        result = content_policy_read(registry->policy, CONTENT_BEHAVIOR, maage_behavior_name(registry, definition->behaviors[i]));
        if (maage_status_failed(result)) return result;
    }
    if (registry->tag_count >= MAAGE_CONTENT_ID_INVALID) return trait_fail(registry, MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_OPERATION_REGISTER, name, "Tag ID domain exhausted");
    required = (maage_property_id *) trait_copy(definition->required, definition->required_count, sizeof(*required));
    includes = (maage_tag_id *) trait_copy(definition->includes, definition->include_count, sizeof(*includes));
    behaviors = (maage_behavior_id *) trait_copy(definition->behaviors, definition->behavior_count, sizeof(*behaviors));
    if ((definition->required_count && !required) || (definition->include_count && !includes) || (definition->behavior_count && !behaviors) || !trait_reserve_tags(&registry->tags, &registry->tag_capacity, registry->tag_count + 1)) {
        free(required);
        free(includes);
        free(behaviors);
        return trait_fail(registry, MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_REGISTER, name, "Tag contract allocation");
    }
    slot = &registry->tags[registry->tag_count];
    result = trait_name_add(registry, CONTENT_TAG, name, (unsigned long) registry->tag_count, &slot->name);
    if (maage_status_failed(result)) {
        free(required);
        free(includes);
        free(behaviors);
        return result;
    }
    definition->id = (unsigned long) registry->tag_count++;
    slot->definition = *definition;
    slot->definition.required = required;
    slot->definition.includes = includes;
    slot->definition.behaviors = behaviors;
    return result;
}
static const char *trait_subject_name(const maage_traits_registry *registry, unsigned int domain, unsigned long id) {
    size_t i;
    content_domain target = domain == MAAGE_TRAIT_BLOCK ? CONTENT_BLOCK : CONTENT_ITEM;
    if (!registry->policy) return NULL;
    for (i = 0; i < registry->policy->count; ++i)
        if (registry->policy->identities[i].domain == target && registry->policy->identities[i].id == id) return registry->policy->identities[i].name;
    return NULL;
}
static const trait_subject *trait_subject_get(const maage_traits_registry *registry, unsigned int domain, unsigned long id) {
    size_t index;
    if (!registry || (domain != MAAGE_TRAIT_BLOCK && domain != MAAGE_TRAIT_ITEM)) return NULL;
    index = domain == MAAGE_TRAIT_BLOCK ? 0 : 1;
    if (id >= registry->subject_count[index] || !registry->subjects[index][id].assigned) return NULL;
    return &registry->subjects[index][id];
}
maage_status maage_traits_assign(maage_traits_registry *registry, unsigned int domain, unsigned long id, const maage_tag_id *tags, size_t count, maage_property_set_id constants) {
    maage_tag_id *copy;
    trait_subject *subject;
    maage_status result;
    size_t index, i, j;
    const maage_property_set *set;
    if (!registry || registry->finalized || (domain != MAAGE_TRAIT_BLOCK && domain != MAAGE_TRAIT_ITEM) || id >= MAAGE_CONTENT_ID_INVALID || (count && !tags) || !maage_property_set_get(registry, constants) || (domain == MAAGE_TRAIT_BLOCK && !count)) return trait_fail(registry, MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_REGISTER, NULL, "Invalid domain, subject, tag assignment or constants");
    result = content_policy_update(registry->policy, domain == MAAGE_TRAIT_BLOCK ? CONTENT_BLOCK : CONTENT_ITEM, trait_subject_name(registry, domain, id));
    if (maage_status_failed(result)) return result;
    for (i = 0; i < count; ++i) {
        const maage_tag_definition *definition = maage_tag_get(registry, tags[i]);
        if (!definition || !(definition->domains & domain)) return trait_fail(registry, MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_REGISTER, trait_subject_name(registry, domain, id), "Tag does not implement the subject's content domain");
        for (j = 0; j < i; ++j)
            if (tags[j] == tags[i]) return trait_fail(registry, MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_REGISTER, trait_subject_name(registry, domain, id), "Duplicate tag membership");
        result = content_policy_read(registry->policy, CONTENT_TAG, maage_tag_name(registry, tags[i]));
        if (maage_status_failed(result)) return result;
    }
    set = maage_property_set_get(registry, constants);
    for (i = 0; i < set->count; ++i) {
        result = content_policy_read(registry->policy, CONTENT_PROPERTY, maage_property_name(registry, set->values[i].property));
        if (maage_status_failed(result)) return result;
        if (maage_property_get(registry, set->values[i].property)->kind == MAAGE_PROPERTY_IDENTITY) {
            result = content_policy_read(registry->policy, CONTENT_PROPERTY_VALUE, maage_property_value_name(registry, set->values[i].property, set->values[i].value));
            if (maage_status_failed(result)) return result;
        }
    }
    index = domain == MAAGE_TRAIT_BLOCK ? 0 : 1;
    if (id > (size_t) - 1 - 1) return trait_fail(registry, MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_OPERATION_REGISTER, NULL, "Subject storage overflow");
    copy = (maage_tag_id *) trait_copy(tags, count, sizeof(*copy));
    if ((count && !copy) || !trait_reserve_subjects(&registry->subjects[index], &registry->subject_capacity[index], (size_t) id + 1)) {
        free(copy);
        return trait_fail(registry, MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_REGISTER, NULL, "Subject contract allocation");
    }
    subject = &registry->subjects[index][id];
    free(subject->tags);
    subject->tags = copy;
    subject->tag_count = count;
    subject->constants = constants;
    subject->assigned = 1;
    if (id >= registry->subject_count[index]) registry->subject_count[index] = (size_t) id + 1;
    return trait_result(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_REGISTER);
}
static int trait_tag_contains(const maage_traits_registry *registry, maage_tag_id id, maage_tag_id target, size_t remaining) {
    const maage_tag_definition *tag = maage_tag_get(registry, id);
    size_t i;
    if (id == target) return tag != NULL;
    if (!tag || !remaining) return 0;
    for (i = 0; i < tag->include_count; ++i)
        if (trait_tag_contains(registry, tag->includes[i], target, remaining - 1)) return 1;
    return 0;
}
int maage_traits_has(const maage_traits_registry *registry, unsigned int domain, unsigned long id, maage_tag_id tag) {
    const trait_subject *subject = trait_subject_get(registry, domain, id);
    size_t i;
    if (!subject || tag >= registry->tag_count) return 0;
    if (subject->membership) return (subject->membership[tag / 8] & (1U << (tag % 8))) != 0;
    for (i = 0; i < subject->tag_count; ++i)
        if (trait_tag_contains(registry, subject->tags[i], tag, registry->tag_count)) return 1;
    return 0;
}
maage_property_set_id maage_traits_constants(const maage_traits_registry *registry, unsigned int domain, unsigned long id) {
    const trait_subject *subject = trait_subject_get(registry, domain, id);
    return subject ? subject->constants : (registry && domain == MAAGE_TRAIT_ITEM && id < MAAGE_CONTENT_ID_INVALID ? 0 : MAAGE_CONTENT_ID_INVALID);
}
int maage_traits_value(const maage_traits_registry *registry, unsigned int domain, unsigned long id, maage_property_set_id state, maage_property_id property, maage_property_value *value) {
    const trait_subject *subject = trait_subject_get(registry, domain, id);
    if (!registry || (domain != MAAGE_TRAIT_BLOCK && domain != MAAGE_TRAIT_ITEM) || id >= MAAGE_CONTENT_ID_INVALID || (domain == MAAGE_TRAIT_BLOCK && !subject)) return 0;
    return maage_property_set_value(registry, state, property, value) || (subject && maage_property_set_value(registry, subject->constants, property, value));
}
maage_status maage_traits_validate(const maage_traits_registry *registry, unsigned int domain, unsigned long id, maage_property_set_id state) {
    const trait_subject *subject = trait_subject_get(registry, domain, id);
    const maage_property_set *set = maage_property_set_get(registry, state);
    const maage_tag_definition *tag;
    size_t i, j;
    maage_property_value constant;
    if (!set || (domain != MAAGE_TRAIT_BLOCK && domain != MAAGE_TRAIT_ITEM) || id >= MAAGE_CONTENT_ID_INVALID || (domain == MAAGE_TRAIT_BLOCK && (!subject || !subject->tag_count))) return trait_result(MAAGE_STATUS_INVALID_STATE, MAAGE_OPERATION_VALIDATE);
    if (!subject) return trait_result(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_VALIDATE);
    for (i = 0; i < set->count; ++i)
        if (maage_property_set_value(registry, subject->constants, set->values[i].property, &constant)) return trait_result(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_VALIDATE);
    for (i = 0; i < registry->tag_count; ++i) {
        if (!maage_traits_has(registry, domain, id, (unsigned long) i)) continue;
        tag = &registry->tags[i].definition;
        for (j = 0; j < tag->required_count; ++j)
            if (!maage_traits_value(registry, domain, id, state, tag->required[j], NULL)) return trait_result(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_VALIDATE);
    }
    return trait_result(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_VALIDATE);
}
static maage_status trait_tag_flatten(maage_traits_registry *registry, maage_tag_id id, size_t bytes) {
    trait_tag *tag = &registry->tags[id];
    maage_status result;
    size_t i, byte;
    if (tag->complete) return trait_result(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_FINALIZE);
    if (tag->visiting) return trait_fail(registry, MAAGE_STATUS_INVALID_STATE, MAAGE_OPERATION_FINALIZE, tag->name, "Tag contract cycle");
    tag->visiting = 1;
    tag->flat = (unsigned char *) calloc(bytes, 1);
    if (!tag->flat) return trait_fail(registry, MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_FINALIZE, tag->name, "Flattened tag contract allocation");
    tag->flat[id / 8] |= (unsigned char) (1U << (id % 8));
    for (i = 0; i < tag->definition.include_count; ++i) {
        result = trait_tag_flatten(registry, tag->definition.includes[i], bytes);
        if (maage_status_failed(result)) return result;
        for (byte = 0; byte < bytes; ++byte)
            tag->flat[byte] |= registry->tags[tag->definition.includes[i]].flat[byte];
    }
    tag->visiting = 0;
    tag->complete = 1;
    return trait_result(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_FINALIZE);
}
static int trait_behavior_after(const maage_traits_registry *registry, maage_behavior_id id, maage_behavior_id target, size_t remaining) {
    const maage_behavior_definition *definition = maage_behavior_get(registry, id);
    size_t i;
    if (!definition || !remaining) return 0;
    for (i = 0; i < definition->after_count; ++i)
        if (definition->after[i] == target || trait_behavior_after(registry, definition->after[i], target, remaining - 1)) return 1;
    return 0;
}
static maage_status trait_subject_compile(maage_traits_registry *registry, unsigned int domain, unsigned long id, size_t bytes) {
    trait_subject *subject = &registry->subjects[domain == MAAGE_TRAIT_BLOCK ? 0 : 1][id];
    const maage_behavior_definition *a, *b;
    const maage_tag_definition *tag;
    unsigned char *selected;
    maage_behavior_id *pending;
    size_t i, j, byte, count = 0, best, position, k;
    int ready;
    const char *name = trait_subject_name(registry, domain, id);
    if (!subject->assigned) return trait_result(MAAGE_STATUS_NOOP, MAAGE_OPERATION_FINALIZE);
    subject->membership = bytes ? (unsigned char *) calloc(bytes, 1) : NULL;
    if (bytes && !subject->membership) return trait_fail(registry, MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_FINALIZE, name, "Membership allocation");
    for (i = 0; i < subject->tag_count; ++i)
        for (byte = 0; byte < bytes; ++byte)
            subject->membership[byte] |= registry->tags[subject->tags[i]].flat[byte];
    selected = registry->behavior_count ? (unsigned char *) calloc(registry->behavior_count, 1) : NULL;
    pending = registry->behavior_count ? (maage_behavior_id *) malloc(registry->behavior_count * sizeof(*pending)) : NULL;
    if (registry->behavior_count && (!selected || !pending)) {
        free(selected);
        free(pending);
        return trait_fail(registry, MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_FINALIZE, name, "Behavior composition allocation");
    }
    for (i = 0; i < registry->tag_count; ++i) {
        if (!maage_traits_has(registry, domain, id, (unsigned long) i)) continue;
        tag = &registry->tags[i].definition;
        for (j = 0; j < tag->behavior_count; ++j)
            if (!selected[tag->behaviors[j]]) {
                selected[tag->behaviors[j]] = 1;
                pending[count++] = tag->behaviors[j];
            }
    }
    for (i = 0; i < count; ++i) {
        a = maage_behavior_get(registry, pending[i]);
        for (j = 0; j < a->after_count; ++j)
            if (!selected[a->after[j]]) {
                free(selected);
                free(pending);
                return trait_fail(registry, MAAGE_STATUS_NOT_FOUND, MAAGE_OPERATION_FINALIZE, name, "Behavior requires a provider absent from the effective contract");
            }
        for (j = 0; j < i; ++j) {
            b = maage_behavior_get(registry, pending[j]);
            if (a->event != b->event) continue;
            if (a->mode == MAAGE_BEHAVIOR_EXCLUSIVE || b->mode == MAAGE_BEHAVIOR_EXCLUSIVE) {
                free(selected);
                free(pending);
                return trait_fail(registry, MAAGE_STATUS_INVALID_STATE, MAAGE_OPERATION_FINALIZE, name, "Conflicting exclusive behavior providers");
            }
            if (a->order == b->order && !trait_behavior_after(registry, a->id, b->id, registry->behavior_count) && !trait_behavior_after(registry, b->id, a->id, registry->behavior_count)) {
                free(selected);
                free(pending);
                return trait_fail(registry, MAAGE_STATUS_INVALID_STATE, MAAGE_OPERATION_FINALIZE, name, "Composable behaviors require explicit ordering");
            }
        }
    }
    subject->dispatch = count ? (maage_behavior_id *) malloc(count * sizeof(*subject->dispatch)) : NULL;
    if (count && !subject->dispatch) {
        free(selected);
        free(pending);
        return trait_fail(registry, MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_FINALIZE, name, "Behavior dispatch allocation");
    }
    if (selected) memset(selected, 0, registry->behavior_count);
    for (position = 0; position < count; ++position) {
        best = count;
        for (i = 0; i < count; ++i) {
            if (selected[pending[i]]) continue;
            a = maage_behavior_get(registry, pending[i]);
            ready = 1;
            for (j = 0; j < a->after_count; ++j)
                if (!selected[a->after[j]]) ready = 0;
            if (!ready) continue;
            if (best == count) best = i;
            else {
                b = maage_behavior_get(registry, pending[best]);
                if (a->event < b->event || (a->event == b->event && a->order < b->order)) best = i;
            }
        }
        if (best == count) {
            free(selected);
            free(pending);
            return trait_fail(registry, MAAGE_STATUS_INVALID_STATE, MAAGE_OPERATION_FINALIZE, name, "Cyclic behavior ordering");
        }
        k = pending[best];
        selected[k] = 1;
        subject->dispatch[position] = (unsigned long) k;
    }
    subject->dispatch_count = count;
    free(selected);
    free(pending);
    return trait_result(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_FINALIZE);
}
maage_status maage_traits_registry_finalize(maage_traits_registry *registry) {
    maage_status result;
    size_t i, domain, bytes;
    if (!registry || registry->finalized) return trait_result(MAAGE_STATUS_INVALID_STATE, MAAGE_OPERATION_FINALIZE);
    result = content_policy_finalize(registry->policy);
    if (maage_status_failed(result)) return result;
    if (registry->tag_count > (size_t) - 1 - 7) return trait_fail(registry, MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_OPERATION_FINALIZE, NULL, "Tag membership size overflow");
    bytes = (registry->tag_count + 7) / 8;
    for (i = 0; i < registry->tag_count; ++i) {
        result = trait_tag_flatten(registry, (unsigned long) i, bytes);
        if (maage_status_failed(result)) return result;
    }
    for (domain = 0; domain < 2; ++domain)
        for (i = 0; i < registry->subject_count[domain]; ++i) {
            result = trait_subject_compile(registry, domain == 0 ? MAAGE_TRAIT_BLOCK : MAAGE_TRAIT_ITEM, (unsigned long) i, bytes);
            if (maage_status_failed(result)) return result;
        }
    registry->finalized = 1;
    return trait_result(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_FINALIZE);
}
int maage_traits_has_behavior(const maage_traits_registry *registry, unsigned int domain, unsigned long id, unsigned long event) {
    const trait_subject *subject = trait_subject_get(registry, domain, id);
    const maage_tag_definition *tag;
    size_t i, j;
    if (!registry || !subject) return 0;
    if (registry->finalized) {
        for (i = 0; i < subject->dispatch_count; ++i)
            if (maage_behavior_get(registry, subject->dispatch[i])->event == event) return 1;
        return 0;
    }
    for (i = 0; i < registry->tag_count; ++i) {
        if (!maage_traits_has(registry, domain, id, (maage_tag_id) i)) continue;
        tag = maage_tag_get(registry, (maage_tag_id) i);
        for (j = 0; j < tag->behavior_count; ++j)
            if (maage_behavior_get(registry, tag->behaviors[j])->event == event) return 1;
    }
    return 0;
}
maage_status maage_traits_dispatch(const maage_traits_registry *registry, unsigned int domain, unsigned long id, unsigned long event, void *call) {
    const trait_subject *subject = trait_subject_get(registry, domain, id);
    const maage_behavior_definition *behavior;
    maage_status result, outcome = trait_result(MAAGE_STATUS_NOOP, MAAGE_OPERATION_TICK);
    size_t low = 0, high, middle, i;
    if (!registry || !registry->finalized || !subject || !call) return trait_result(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_TICK);
    high = subject->dispatch_count;
    while (low < high) {
        middle = low + (high - low) / 2;
        if (maage_behavior_get(registry, subject->dispatch[middle])->event < event) low = middle + 1;
        else high = middle;
    }
    for (i = low; i < subject->dispatch_count; ++i) {
        behavior = maage_behavior_get(registry, subject->dispatch[i]);
        if (behavior->event != event) break;
        result = behavior->callback(behavior->context, call);
        if (maage_status_failed(result)) return result;
        if (!maage_status_is_warning(outcome) && maage_status_code(result) != MAAGE_STATUS_NOOP) outcome = result;
    }
    return outcome;
}
maage_status maage_traits_validate_identity(const maage_traits_registry *a, const maage_traits_registry *b) {
    size_t i, j, domain, bytes;
    const maage_tag_definition *ta, *tb;
    const maage_behavior_definition *ba, *bb;
    const trait_subject *sa, *sb;
    if (!a || !b || !a->finalized || !b->finalized) return trait_result(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_VALIDATE);
    if (a->property_count != b->property_count || a->value_count != b->value_count || a->tag_count != b->tag_count || a->behavior_count != b->behavior_count || a->set_count != b->set_count) return trait_result(MAAGE_STATUS_INVALID_STATE, MAAGE_OPERATION_VALIDATE);
    for (i = 0; i < a->property_count; ++i)
        if (strcmp(a->properties[i].name, b->properties[i].name) || a->properties[i].definition.kind != b->properties[i].definition.kind || a->properties[i].definition.minimum != b->properties[i].definition.minimum || a->properties[i].definition.maximum != b->properties[i].definition.maximum) return trait_result(MAAGE_STATUS_INVALID_STATE, MAAGE_OPERATION_VALIDATE);
    for (i = 0; i < a->value_count; ++i)
        if (a->values[i].property != b->values[i].property || strcmp(a->values[i].name, b->values[i].name)) return trait_result(MAAGE_STATUS_INVALID_STATE, MAAGE_OPERATION_VALIDATE);
    for (i = 0; i < a->set_count; ++i)
        if (!trait_set_equal(&a->sets[i], b->sets[i].values, b->sets[i].count)) return trait_result(MAAGE_STATUS_INVALID_STATE, MAAGE_OPERATION_VALIDATE);
    for (i = 0; i < a->tag_count; ++i) {
        ta = &a->tags[i].definition;
        tb = &b->tags[i].definition;
        if (strcmp(a->tags[i].name, b->tags[i].name) || ta->domains != tb->domains || ta->required_count != tb->required_count || ta->include_count != tb->include_count || ta->behavior_count != tb->behavior_count) return trait_result(MAAGE_STATUS_INVALID_STATE, MAAGE_OPERATION_VALIDATE);
        for (j = 0; j < ta->required_count; ++j)
            if (ta->required[j] != tb->required[j]) return trait_result(MAAGE_STATUS_INVALID_STATE, MAAGE_OPERATION_VALIDATE);
        for (j = 0; j < ta->include_count; ++j)
            if (ta->includes[j] != tb->includes[j]) return trait_result(MAAGE_STATUS_INVALID_STATE, MAAGE_OPERATION_VALIDATE);
        for (j = 0; j < ta->behavior_count; ++j)
            if (ta->behaviors[j] != tb->behaviors[j]) return trait_result(MAAGE_STATUS_INVALID_STATE, MAAGE_OPERATION_VALIDATE);
    }
    for (i = 0; i < a->behavior_count; ++i) {
        ba = &a->behaviors[i].definition;
        bb = &b->behaviors[i].definition;
        if (strcmp(a->behaviors[i].name, b->behaviors[i].name) || ba->event != bb->event || ba->mode != bb->mode || ba->order != bb->order || ba->after_count != bb->after_count) return trait_result(MAAGE_STATUS_INVALID_STATE, MAAGE_OPERATION_VALIDATE);
        for (j = 0; j < ba->after_count; ++j)
            if (ba->after[j] != bb->after[j]) return trait_result(MAAGE_STATUS_INVALID_STATE, MAAGE_OPERATION_VALIDATE);
    }
    bytes = (a->tag_count + 7) / 8;
    for (domain = 0; domain < 2; ++domain) {
        if (a->subject_count[domain] != b->subject_count[domain]) return trait_result(MAAGE_STATUS_INVALID_STATE, MAAGE_OPERATION_VALIDATE);
        for (i = 0; i < a->subject_count[domain]; ++i) {
            sa = &a->subjects[domain][i];
            sb = &b->subjects[domain][i];
            if (sa->assigned != sb->assigned || sa->constants != sb->constants || sa->dispatch_count != sb->dispatch_count) return trait_result(MAAGE_STATUS_INVALID_STATE, MAAGE_OPERATION_VALIDATE);
            if (!sa->assigned) continue;
            if (bytes && memcmp(sa->membership, sb->membership, bytes)) return trait_result(MAAGE_STATUS_INVALID_STATE, MAAGE_OPERATION_VALIDATE);
            for (j = 0; j < sa->dispatch_count; ++j)
                if (sa->dispatch[j] != sb->dispatch[j]) return trait_result(MAAGE_STATUS_INVALID_STATE, MAAGE_OPERATION_VALIDATE);
        }
    }
    return trait_result(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_VALIDATE);
}
