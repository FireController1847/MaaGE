#ifndef MAAGE_ATTRIBUTES_H
#define MAAGE_ATTRIBUTES_H
#include "maage/content_ids.h"
#include "maage/status.h"

typedef unsigned long maage_attribute_id;
typedef struct maage_attribute_registry maage_attribute_registry;
typedef struct maage_attribute_set maage_attribute_set;

/* Optional engine consumers. Content supplies the names, defaults and bounds;
 * arbitrary custom attributes have no role. Numeric IDs are never fixed. */
typedef enum maage_attribute_role {
    MAAGE_ATTRIBUTE_ROLE_NONE = 0,
    MAAGE_ATTRIBUTE_ROLE_MOVEMENT,
    MAAGE_ATTRIBUTE_ROLE_PANIC_MOVEMENT,
    MAAGE_ATTRIBUTE_ROLE_FLIGHT_MOVEMENT,
    MAAGE_ATTRIBUTE_ROLE_JUMP,
    MAAGE_ATTRIBUTE_ROLE_GRAVITY,
    MAAGE_ATTRIBUTE_ROLE_FALL_LIMIT,
    MAAGE_ATTRIBUTE_ROLE_ATTACK_DAMAGE,
    MAAGE_ATTRIBUTE_ROLE_ATTACK_RANGE,
    MAAGE_ATTRIBUTE_ROLE_FOLLOW_RANGE,
    MAAGE_ATTRIBUTE_ROLE_PROJECTILE_SPEED,
    MAAGE_ATTRIBUTE_ROLE_REACH,
    MAAGE_ATTRIBUTE_ROLE_COUNT
} maage_attribute_role;

typedef struct maage_attribute_definition {
    maage_attribute_id id;
    maage_attribute_role role;
    double base, minimum, maximum;
} maage_attribute_definition;

typedef struct maage_attribute_base {
    maage_attribute_id attribute;
    double value;
} maage_attribute_base;

typedef enum maage_attribute_operation {
    MAAGE_ATTRIBUTE_ADD = 1,
    MAAGE_ATTRIBUTE_MULTIPLY
} maage_attribute_operation;

/* (attribute, source, key) uniquely identifies a contribution. Source is a
 * caller-coordinated nonzero numeric identity; key distinguishes contributions
 * from that source. Reapplication replaces it. Multiply uses factors, e.g. 1.3. */
typedef struct maage_attribute_modifier {
    maage_attribute_id attribute;
    unsigned long source, key;
    maage_attribute_operation operation;
    double amount;
} maage_attribute_modifier;

/* Registry owns copied definitions and names. Dense IDs begin at one; a
 * nonzero role has one owner. Finalized definitions outlive borrowing sets. */
maage_status maage_attribute_registry_create(maage_attribute_registry **);
maage_status maage_attribute_registry_destroy(maage_attribute_registry *);
maage_status maage_attribute_register(maage_attribute_registry *, const char *, maage_attribute_definition *);
maage_status maage_attribute_registry_finalize(maage_attribute_registry *);
size_t maage_attribute_count(const maage_attribute_registry *);
const maage_attribute_definition *maage_attribute_get(const maage_attribute_registry *, maage_attribute_id);
const char *maage_attribute_name(const maage_attribute_registry *, maage_attribute_id);
maage_status maage_attribute_resolve_name(const maage_attribute_registry *, const char *, maage_attribute_id *);
maage_attribute_id maage_attribute_role_id(const maage_attribute_registry *, maage_attribute_role);

/* Owner-thread state. Initial base overrides are copied and unique. Runtime
 * overrides/modifiers are transient; their sources own reconstruction.
 * Value = clamp((base + sum(additions)) * product(factors)).
 * Totals are reduced in ascending source/key order. Reads are allocation-free
 * O(1); invalid values or overflowing totals leave existing state intact. */
maage_status maage_attributes_create(const maage_attribute_registry *, const maage_attribute_base *, size_t, maage_attribute_set **);
maage_status maage_attributes_destroy(maage_attribute_set *);
maage_status maage_attributes_get_base(const maage_attribute_set *, maage_attribute_id, double *);
maage_status maage_attributes_set_base(maage_attribute_set *, maage_attribute_id, double);
maage_status maage_attributes_get(const maage_attribute_set *, maage_attribute_id, double *);
maage_status maage_attributes_set_modifier(maage_attribute_set *, const maage_attribute_modifier *);
maage_status maage_attributes_remove_modifier(maage_attribute_set *, maage_attribute_id, unsigned long source, unsigned long key);
/* Optional engine bindings retain the caller's default if absent. Offset is a
 * single evaluation's additive contribution, e.g. the held item's damage. */
double maage_attributes_role_value(const maage_attribute_set *, maage_attribute_role, double fallback);
double maage_attributes_role_offset(const maage_attribute_set *, maage_attribute_role, double offset, double fallback);
maage_status maage_attributes_set_role_base(maage_attribute_set *, maage_attribute_role, double);
#endif
