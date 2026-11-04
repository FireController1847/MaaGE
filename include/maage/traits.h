#ifndef MAAGE_TRAITS_H
#define MAAGE_TRAITS_H
#include "maage/content_ids.h"
#include "maage/status.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned long maage_property_id;
typedef unsigned long maage_property_value;
typedef unsigned long maage_property_set_id;
typedef unsigned long maage_tag_id;
typedef unsigned long maage_behavior_id;
typedef struct maage_traits_registry maage_traits_registry;

enum {
    MAAGE_PROPERTY_BOOLEAN = 1,
    MAAGE_PROPERTY_INTEGER,
    MAAGE_PROPERTY_IDENTITY
};
enum {
    MAAGE_TRAIT_BLOCK = 1,
    MAAGE_TRAIT_ITEM = 2
};
enum {
    MAAGE_BEHAVIOR_COMPOSE = 1,
    MAAGE_BEHAVIOR_EXCLUSIVE = 2
};
enum {
    MAAGE_BEHAVIOR_RANDOM_TICK = 1,
    MAAGE_BEHAVIOR_SCHEDULED_TICK,
    MAAGE_BEHAVIOR_SURVIVES,
    MAAGE_BEHAVIOR_CAN_PLACE,
    MAAGE_BEHAVIOR_DROPS,
    MAAGE_BEHAVIOR_USE,
    MAAGE_BEHAVIOR_LAND,
    MAAGE_BEHAVIOR_CHANGED,
    MAAGE_BEHAVIOR_IMPORT
};
typedef struct maage_property_definition {
    maage_property_id id;
    unsigned int kind;
    maage_property_value minimum, maximum; /* INTEGER inclusive; BOOLEAN is always 0..1. */
} maage_property_definition;
typedef struct maage_property_assignment {
    maage_property_id property;
    maage_property_value value;
} maage_property_assignment;
typedef struct maage_property_set {
    const maage_property_assignment *values; /* Sorted unique property IDs; registry-owned. */
    size_t count;
} maage_property_set;
typedef maage_status (*maage_behavior_callback)(const void *context, void *call);
typedef struct maage_behavior_definition {
    maage_behavior_id id;
    unsigned long event;  /* Numeric event contract; built-in lifecycle events above. */
    unsigned int domains; /* Applicable subject domains; built-in lifecycle events are block-only. */
    unsigned int mode;
    long order; /* Ascending, then explicit dependency order. Equal unordered handlers conflict. */
    const maage_behavior_id *after;
    size_t after_count;
    const void *context; /* Borrowed immutable provider context, independent of adopting content. */
    maage_behavior_callback callback;
} maage_behavior_definition;
typedef struct maage_tag_definition {
    maage_tag_id id;
    unsigned int domains; /* Bit mask; membership always names its domain explicitly. */
    const maage_property_id *required;
    size_t required_count;
    const maage_tag_id *includes; /* Earlier registered tags; contracts apply only in shared domains. */
    size_t include_count;
    const maage_behavior_id *behaviors;
    size_t behavior_count;
} maage_tag_definition;

/* Owns copied names, contracts, immutable configurations and compiled dispatch.
 * Empty property configuration is ID zero. No runtime query allocates or resolves strings.
 * Names are boundary-only and obey the enclosing content module's ownership/dependencies.
 * Retain numeric IDs while composing: definition/set descriptors may move until
 * finalization. Their copied value arrays and all finalized pointers remain borrowed
 * until registry destruction. Provider contexts outlive every registry reader. */
maage_status maage_traits_registry_create(maage_traits_registry **output);
maage_status maage_traits_registry_destroy(maage_traits_registry *registry);
maage_status maage_traits_registry_finalize(maage_traits_registry *registry);
int maage_traits_registry_is_finalized(const maage_traits_registry *registry);
maage_status maage_property_register(maage_traits_registry *, const char *, maage_property_definition *);
maage_status maage_property_find(const maage_traits_registry *, const char *, maage_property_id *);
const maage_property_definition *maage_property_get(const maage_traits_registry *, maage_property_id);
const char *maage_property_name(const maage_traits_registry *, maage_property_id);
size_t maage_property_count(const maage_traits_registry *);
maage_status maage_property_value_register(maage_traits_registry *, maage_property_id, const char *, maage_property_value *);
maage_status maage_property_value_find(const maage_traits_registry *, maage_property_id, const char *, maage_property_value *);
const char *maage_property_value_name(const maage_traits_registry *, maage_property_id, maage_property_value);
size_t maage_property_value_count(const maage_traits_registry *, maage_property_id);
int maage_property_value_valid(const maage_traits_registry *, maage_property_id, maage_property_value);
maage_status maage_property_set_register(maage_traits_registry *, const maage_property_assignment *, size_t, maage_property_set_id *);
const maage_property_set *maage_property_set_get(const maage_traits_registry *, maage_property_set_id);
size_t maage_property_set_count(const maage_traits_registry *);
int maage_property_set_value(const maage_traits_registry *, maage_property_set_id, maage_property_id, maage_property_value *);
/* Resolve only an already registered configuration; sparse invalid combinations fail. */
maage_status maage_property_set_resolve(const maage_traits_registry *, const maage_property_assignment *, size_t, maage_property_set_id *);
maage_status maage_property_set_change(const maage_traits_registry *, maage_property_set_id, maage_property_id, maage_property_value, maage_property_set_id *);
maage_status maage_tag_register(maage_traits_registry *, const char *, maage_tag_definition *);
maage_status maage_tag_find(const maage_traits_registry *, const char *, maage_tag_id *);
const maage_tag_definition *maage_tag_get(const maage_traits_registry *, maage_tag_id);
const char *maage_tag_name(const maage_traits_registry *, maage_tag_id);
size_t maage_tag_count(const maage_traits_registry *);
maage_status maage_behavior_register(maage_traits_registry *, const char *, maage_behavior_definition *);
maage_status maage_behavior_find(const maage_traits_registry *, const char *, maage_behavior_id *);
const maage_behavior_definition *maage_behavior_get(const maage_traits_registry *, maage_behavior_id);
const char *maage_behavior_name(const maage_traits_registry *, maage_behavior_id);
size_t maage_behavior_count(const maage_traits_registry *);
/* Assignment replaces only the current module's own subject. Constants are shared by all states. */
maage_status maage_traits_assign(maage_traits_registry *, unsigned int domain, unsigned long subject, const maage_tag_id *, size_t, maage_property_set_id constants);
int maage_traits_has(const maage_traits_registry *, unsigned int domain, unsigned long subject, maage_tag_id);
maage_property_set_id maage_traits_constants(const maage_traits_registry *, unsigned int domain, unsigned long subject);
int maage_traits_value(const maage_traits_registry *, unsigned int domain, unsigned long subject, maage_property_set_id state, maage_property_id, maage_property_value *);
maage_status maage_traits_validate(const maage_traits_registry *, unsigned int domain, unsigned long subject, maage_property_set_id state);
int maage_traits_has_behavior(const maage_traits_registry *, unsigned int domain, unsigned long subject, unsigned long event);
/* Built-in events require a block subject and maage_block_behavior_call payload.
 * Custom event payloads follow their registered providers' domain-specific contract. */
maage_status maage_traits_dispatch(const maage_traits_registry *, unsigned int domain, unsigned long subject, unsigned long event, void *call);
/* Canonical peer comparison includes schemas, contracts, configurations, membership and dispatch identity. */
maage_status maage_traits_validate_identity(const maage_traits_registry *, const maage_traits_registry *);
#ifdef __cplusplus
}
#endif
#endif
