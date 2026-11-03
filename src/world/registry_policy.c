#include "registry_policy.h"
#include <stdlib.h>
#include <string.h>

const char *content_policy_domain_name(content_domain domain) {
    static const char *const names[] = {"blocks", "items", "biomes", "fluids", "geometry", "containers", "creative tabs", "sound resources", "sounds", "sound materials", "music", "ambience", "block entities", "generation features", "ores", "block bindings", "sound bindings", "presentation bindings", "inventory binding"};
    return names[(unsigned int) domain];
}
static void policy_target(content_policy *policy, const char *name) {
    if (!policy || maage_status_failed(policy->failure)) return;
    strncpy(policy->content_name, name ? name : "<unnamed>", sizeof(policy->content_name) - 1);
    policy->content_name[sizeof(policy->content_name) - 1] = '\0';
}
static maage_status policy_fail(content_policy *policy, unsigned char code, const char *reason, const char *dependency) {
    maage_status result = maage_status_make(code, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0);
    if (!maage_status_failed(policy->failure)) {
        policy->failure = result;
        policy->reason = reason;
        policy->dependency = dependency;
    }
    return result;
}
static unsigned long policy_hash(content_domain domain, const char *name) {
    unsigned long hash = 2166136261UL ^ (unsigned long) domain;
    while (*name)
        hash = ((hash ^ (unsigned char) *name++) * 16777619UL) & 0xffffffffUL;
    return hash;
}
static size_t policy_bucket(const content_policy *policy, content_domain domain, const char *name) {
    size_t bucket = (size_t) (policy_hash(domain, name) % policy->bucket_count), index;
    while ((index = policy->buckets[bucket]) != 0) {
        if (policy->identities[index - 1].domain == domain && !strcmp(policy->identities[index - 1].name, name)) break;
        bucket = (bucket + 1) % policy->bucket_count;
    }
    return bucket;
}
static maage_status policy_grow(content_policy *policy) {
    size_t capacity, buckets, i, bucket, *table;
    content_identity *grown;
    if (policy->count < policy->capacity) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_REGISTER, 0);
    if (policy->capacity > ((size_t) - 1 - 1) / 4) return policy_fail(policy, MAAGE_STATUS_BUFFER_OVERFLOW, "Ownership table overflow", NULL);
    capacity = policy->capacity ? policy->capacity * 2 : 32;
    buckets = capacity * 2 + 1;
    if (capacity > (size_t) - 1 / sizeof(*grown) || buckets > (size_t) - 1 / sizeof(*table)) return policy_fail(policy, MAAGE_STATUS_BUFFER_OVERFLOW, "Ownership table overflow", NULL);
    table = (size_t *) calloc(buckets, sizeof(*table));
    if (!table) return policy_fail(policy, MAAGE_STATUS_ALLOCATION_FAILED, "Ownership table allocation", NULL);
    grown = (content_identity *) realloc(policy->identities, capacity * sizeof(*grown));
    if (!grown) {
        free(table);
        return policy_fail(policy, MAAGE_STATUS_ALLOCATION_FAILED, "Ownership table allocation", NULL);
    }
    policy->identities = grown;
    for (i = 0; i < policy->count; ++i) {
        bucket = (size_t) (policy_hash(grown[i].domain, grown[i].name) % buckets);
        while (table[bucket])
            bucket = (bucket + 1) % buckets;
        table[bucket] = i + 1;
    }
    free(policy->buckets);
    policy->buckets = table;
    policy->bucket_count = buckets;
    policy->capacity = capacity;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_REGISTER, 0);
}
static int policy_namespace(const maage_content_module *module, const char *name) {
    size_t length = strcspn(module->name, ":");
    return !strncmp(name, module->name, length) && name[length] == ':' && name[length + 1];
}
maage_status content_policy_register(content_policy *policy, content_domain domain, const char *name, unsigned long id) {
    content_identity *identity;
    size_t bucket;
    maage_status result;
    if (!policy) return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_REGISTER, 0);
    if (maage_status_failed(policy->failure)) return policy->failure;
    policy_target(policy, name);
    if (policy->sealed || !policy->context->module) return policy_fail(policy, MAAGE_STATUS_INVALID_STATE, "Registration outside a mutable module phase", NULL);
    if (!name || (domain < CONTENT_BLOCK_BINDINGS && !policy_namespace(policy->context->module, name))) return policy_fail(policy, MAAGE_STATUS_ACCESS_DENIED, "Registration outside the module root namespace", NULL);
    result = policy_grow(policy);
    if (maage_status_failed(result)) return result;
    bucket = policy_bucket(policy, domain, name);
    if (policy->buckets[bucket]) {
        identity = &policy->identities[policy->buckets[bucket] - 1];
        /* Singleton engine hooks can be refined by their original owner only. */
        if (domain >= CONTENT_BLOCK_BINDINGS && identity->owner == policy->context->module) return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_REGISTER, 0);
        return policy_fail(policy, MAAGE_STATUS_INVALID_STATE, "Duplicate identity or conflicting singleton binding", identity->owner->name);
    }
    identity = &policy->identities[policy->count];
    identity->name = (char *) malloc(strlen(name) + 1);
    if (!identity->name) return policy_fail(policy, MAAGE_STATUS_ALLOCATION_FAILED, "Ownership identity allocation", NULL);
    strcpy(identity->name, name);
    identity->domain = domain;
    identity->id = id;
    identity->owner = policy->context->module;
    policy->buckets[bucket] = ++policy->count;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_REGISTER, 0);
}
maage_status content_policy_update(content_policy *policy, content_domain domain, const char *name) {
    const content_identity *identity;
    size_t index;
    if (!policy || policy->engine) return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_SET, 0);
    if (maage_status_failed(policy->failure)) return policy->failure;
    policy_target(policy, name);
    if (policy->sealed || !policy->context->module || !name || !policy->bucket_count) return policy_fail(policy, MAAGE_STATUS_INVALID_STATE, "Mutation outside a mutable owned definition", NULL);
    index = policy->buckets[policy_bucket(policy, domain, name)];
    if (!index) return policy_fail(policy, MAAGE_STATUS_NOT_FOUND, "Unregistered mutation target", NULL);
    identity = &policy->identities[index - 1];
    if (identity->owner != policy->context->module) return policy_fail(policy, MAAGE_STATUS_ACCESS_DENIED, "Mutation would replace another module's definition", identity->owner->name);
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_SET, 0);
}
maage_status content_policy_read(content_policy *policy, content_domain domain, const char *name) {
    const content_identity *identity;
    size_t index, i;
    if (!policy || policy->engine || !policy->context->module) return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_RESOLVE, 0);
    if (maage_status_failed(policy->failure)) return policy->failure;
    policy_target(policy, name);
    if (!name || !policy->bucket_count) return maage_status_make(MAAGE_STATUS_NOT_FOUND, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_RESOLVE, 0);
    index = policy->buckets[policy_bucket(policy, domain, name)];
    if (!index) return maage_status_make(MAAGE_STATUS_NOT_FOUND, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_RESOLVE, 0);
    identity = &policy->identities[index - 1];
    if (identity->owner == policy->context->module) return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_RESOLVE, 0);
    if (policy->context->phase != MAAGE_CONTENT_PHASE_BIND_INTERNAL)
        for (i = 0; i < policy->context->module->dependency_count; ++i)
            if (!strcmp(policy->context->module->dependencies[i], identity->owner->name)) return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_RESOLVE, 0);
    return policy_fail(policy, MAAGE_STATUS_ACCESS_DENIED, "External reference requires a declared dependency and external phase", identity->owner->name);
}
const content_identity *content_policy_identity(const content_policy *policy, content_domain domain, const char *name) {
    size_t index;
    if (!policy || !policy->bucket_count || !name) return NULL;
    index = policy->buckets[policy_bucket(policy, domain, name)];
    return index ? &policy->identities[index - 1] : NULL;
}
maage_status content_policy_finalize(content_policy *policy) {
    if (!policy || (policy->engine && !policy->sealed)) return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_FINALIZE, 0);
    return policy_fail(policy, MAAGE_STATUS_INVALID_STATE, "Registry finalization outside FINALIZE", NULL);
}
void content_policy_destroy(content_policy *policy) {
    size_t i;
    for (i = 0; i < policy->count; ++i)
        free(policy->identities[i].name);
    free(policy->identities);
    free(policy->buckets);
}
