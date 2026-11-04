#ifndef MAAGE_ENTITY_TYPES_H
#define MAAGE_ENTITY_TYPES_H
#include "maage/items.h"
#include "maage/biome.h"
#include "maage/block_entity.h"

typedef unsigned long maage_entity_type_id;
typedef struct maage_entity_type_registry maage_entity_type_registry;
struct maage_entity;
struct maage_entity_context;
struct maage_entity_type_definition;
enum {
    MAAGE_ENTITY_CLASS_PASSIVE = 1,
    MAAGE_ENTITY_CLASS_HOSTILE,
    MAAGE_ENTITY_CLASS_PROJECTILE
};
enum {
    MAAGE_ENTITY_CAN_SWIM = 1,
    MAAGE_ENTITY_CLIMBS = 2,
    MAAGE_ENTITY_SLOW_FALL = 4,
    MAAGE_ENTITY_BURNS_DAYLIGHT = 8
};
enum {
    MAAGE_ENTITY_BEHAVIOR_WANDER = 1,
    MAAGE_ENTITY_BEHAVIOR_PURSUE = 2,
    MAAGE_ENTITY_BEHAVIOR_MELEE = 4,
    MAAGE_ENTITY_BEHAVIOR_RANGED = 8,
    MAAGE_ENTITY_BEHAVIOR_EXPLODE = 16
};
enum {
    MAAGE_ENTITY_LOOT_BURNING = 1,
    MAAGE_ENTITY_LOOT_SHEARED = 2,
    MAAGE_ENTITY_LOOT_PLAYER = 4
};
typedef struct maage_entity_loot {
    maage_item_stack stack;
    unsigned int min_count, max_count;
    unsigned long chance_numerator, chance_denominator;
    unsigned long required_flags, excluded_flags;
    unsigned long group, weight; /* Nonzero group selects one weighted eligible entry. */
} maage_entity_loot;
typedef struct maage_entity_spawn_rule {
    const maage_biome_id *biomes; /* Empty accepts every registered biome. Copied. */
    size_t biome_count;
    unsigned long weight;
    unsigned int group_min, group_max, min_light, max_light;
    int min_y, max_y, require_sky, require_grass; /* require_grass requires an explicit surface binding. */
    maage_fluid_id fluid;                         /* Zero land; INVALID any fluid; otherwise an exact numeric fluid. */
    double min_player_distance, max_player_distance;
    unsigned long time_start, time_end; /* Both zero accepts the whole day; wrapping ranges allowed. */
    unsigned int population;            /* Per-type policy within the active simulation area; zero disables this rule. */
    maage_block_id surface;             /* INVALID accepts any solid surface. */
    maage_status (*validate)(const struct maage_entity_type_definition *, const struct maage_world *, const double[3], int *);
} maage_entity_spawn_rule;
typedef maage_status (*maage_entity_behavior)(const struct maage_entity_type_definition *, struct maage_entity *, struct maage_entity_context *);
typedef maage_status (*maage_entity_interaction)(const struct maage_entity_type_definition *, struct maage_entity *, struct maage_entity_context *, unsigned int);
typedef struct maage_entity_type_definition {
    maage_entity_type_id id;
    const char *display_name;
    unsigned int classification, flags, behaviors;
    double width, height, eye_height;
    unsigned int health;
    double panic_speed; /* Absolute steering target in blocks/tick; zero uses ordinary movement speed. */
    double speed, attack_damage, attack_range, follow_range, jump_velocity, gravity, fall_speed_limit;
    unsigned int attack_cooldown, invulnerability_ticks, fuse_ticks;
    double explosion_radius, projectile_speed, despawn_distance;
    unsigned long despawn_ticks;
    maage_entity_type_id projectile_type; /* Zero when unused. */
    unsigned long model_id, texture_id;   /* Zero model explicitly omits Client geometry. */
    const unsigned long *textures;        /* Optional variant appearances, copied; IDs remain presentation-only. */
    size_t texture_count;
    unsigned long ambient_sound, hurt_sound, death_sound, attack_sound;
    const maage_entity_loot *loot;
    size_t loot_count;
    maage_status (*loot_stack)(const struct maage_entity_type_definition *, const struct maage_entity *, maage_item_stack *);
    unsigned int experience;
    const maage_entity_spawn_rule *spawns;
    size_t spawn_count;
    const void *behavior_context; /* Borrowed immutable module data. */
    maage_entity_behavior behavior;
    maage_entity_interaction interact;
    /* Optional module-owned state layout. Instances own independent flat byte
     * storage; no embedded pointers. Codecs use explicit numeric word/stack I/O.
     * Definition/code/context outlive World, snapshots, and all borrowing jobs. */
    size_t state_size, encoded_size;
    unsigned long state_version;
    maage_status (*initialize)(const struct maage_entity_type_definition *, struct maage_entity *);
    int (*valid)(const struct maage_entity_type_definition *, const struct maage_entity *);
    maage_status (*encode)(const void *, maage_entity_codec *);
    maage_status (*decode)(void *, maage_entity_codec *);
} maage_entity_type_definition;
/* Dense automatically assigned IDs begin at one. Registry owns copied names,
 * definitions, loot, spawn rules/biomes and variant arrays. Runtime lookup is
 * direct O(1); names resolve only at registration/import boundaries. Updates
 * preserve identity and follow Content ownership/dependency rules. */
maage_status maage_entity_type_registry_create(maage_entity_type_registry **);
maage_status maage_entity_type_register(maage_entity_type_registry *, const char *, maage_entity_type_definition *);
maage_status maage_entity_type_update(maage_entity_type_registry *, const maage_entity_type_definition *);
maage_status maage_entity_type_finalize(maage_entity_type_registry *, const maage_item_registry *, const maage_block_registry *, const maage_biome_registry *);
maage_status maage_entity_type_destroy(maage_entity_type_registry *);
/* Finalization indexes weighted rules by numeric biome and classification.
 * Selection is allocation-free O(log eligible rules); caller validates the
 * selected environment/time/collision policy and may reject that attempt. */
int maage_entity_spawn_biome_matches(const maage_entity_spawn_rule *, maage_biome_id);
maage_status maage_entity_spawn_select(const maage_entity_type_registry *, unsigned int classification, maage_biome_id, unsigned long random, const maage_entity_type_definition **, const maage_entity_spawn_rule **);
const maage_entity_type_definition *maage_entity_type_get(const maage_entity_type_registry *, maage_entity_type_id);
const maage_entity_type_definition *maage_entity_type_at(const maage_entity_type_registry *, size_t);
size_t maage_entity_type_count(const maage_entity_type_registry *);
int maage_entity_type_registry_is_finalized(const maage_entity_type_registry *);
const char *maage_entity_type_name(const maage_entity_type_registry *, maage_entity_type_id);
maage_status maage_entity_type_resolve_name(const maage_entity_type_registry *, const char *, maage_entity_type_id *);
#endif
