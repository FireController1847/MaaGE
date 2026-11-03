#ifndef MAAGE_ENTITIES_H
#define MAAGE_ENTITIES_H
#include "maage/items.h"
#include "maage/player.h"
#include "maage/entity_types.h"
#include "maage/seed.h"
/* Instance identity, distinct from type, item and block identities. Never reused
 * within a world. Explicit encoding uses 32-bit words, never native layouts. */
typedef unsigned long maage_entity_id;
#define MAAGE_ENTITY_PLAYER_ID 4294967295UL
enum {
    MAAGE_ENTITY_ITEM = 1,
    MAAGE_ENTITY_FALLING_BLOCK = 2,
    MAAGE_ENTITY_LIVING = 3,
    MAAGE_ENTITY_PROJECTILE = 4
};
enum {
    MAAGE_ENTITY_STATE_SHEARED = 1,
    MAAGE_ENTITY_STATE_PERSISTENT = 2,
    MAAGE_ENTITY_STATE_BURNING = 4,
    MAAGE_ENTITY_STATE_EXPLODING = 8,
    MAAGE_ENTITY_STATE_DYING = 16
};
enum {
    MAAGE_ENTITY_INTERACT_USE = 1
};
typedef union maage_entity_payload {
    maage_item_stack item;
    struct {
        maage_item_stack stack; /* Block-item identity/metadata for exact restoration and failed-landing drop. */
        maage_block_state_id state;
        int source[3];
        unsigned long source_revision; /* Pre-removal revision; transient across save/load. */
    } falling;
    struct {
        double yaw, head_yaw, pitch, damage_yaw, walk_distance, fall_distance, goal[3];
        unsigned long variant, flags, target, attacker, idle_ticks, effect_cursor, effect_limit, damage_sequence;
        /* hurt_ticks is resistance; hurt_animation_ticks identifies the separate accepted reaction. */
        unsigned int health, hurt_ticks, hurt_animation_ticks, death_ticks, last_damage, attack_ticks, panic_ticks, fuse_ticks, air, fire_ticks;
        maage_item_stack pending_drop;
    } living;
    struct {
        maage_entity_id owner;
        unsigned int damage;
        maage_item_stack recovery;
    } projectile;
} maage_entity_payload;
/* Storage kind selects only shared record/physics handling. registered_type is
 * an independent growable Content identity; it never encodes an instance ID. */
typedef struct maage_entity {
    maage_entity_id id;
    unsigned int type;
    maage_entity_type_id registered_type;
    double position[3], previous[3], velocity[3]; /* Feet, blocks/tick. */
    unsigned long age;
    unsigned int pickup_delay;
    int grounded;
    maage_entity_payload payload;
    void *state; /* Owned flat module state; never copied into a Session view. */
    size_t state_size;
} maage_entity;
/* Value-only presentation state. No custom state, callbacks, resources or
 * authoritative storage pointers cross the local mailbox. */
typedef union maage_entity_view_payload {
    maage_item_stack item;
    struct {
        maage_item_stack stack;
        maage_block_state_id state;
        int source[3];
        unsigned long source_revision;
    } falling;
    struct {
        double yaw, head_yaw, pitch, damage_yaw, walk_distance;
        unsigned long variant, flags, damage_sequence;
        unsigned int health, hurt_ticks, hurt_animation_ticks, death_ticks, attack_ticks, fuse_ticks;
    } living;
    struct {
        unsigned int damage;
    } projectile;
} maage_entity_view_payload;
typedef struct maage_entity_view {
    maage_entity_id id;
    unsigned int type;
    maage_entity_type_id registered_type;
    double position[3], previous[3], velocity[3];
    unsigned long age;
    unsigned int pickup_delay;
    int grounded;
    maage_entity_view_payload payload;
} maage_entity_view;
typedef struct maage_entity_pickup {
    maage_entity_id entity;
    maage_item_stack stack; /* Copied presentation outcome, never inventory ownership. */
    double position[3];
    unsigned long age;
} maage_entity_pickup;
typedef struct maage_entity_save {
    maage_entity *entities;
    size_t count;
    unsigned long next_id, random, behavior_random, spawn_random, simulation_tick, simulation_cursor;
} maage_entity_save;
typedef struct maage_entities maage_entities;
typedef struct maage_entity_policy {
    size_t tick_budget, spawn_attempts, passive_population, hostile_population;
    unsigned long passive_interval, hostile_interval;
    double minimum_player_distance, maximum_player_distance;
} maage_entity_policy;
struct maage_gameplay;
typedef struct maage_entity_context {
    maage_entities *entities;
    maage_entity *entity;
    maage_world *world;
    const maage_entity_type_definition *definition;
    const maage_item_registry *items;
    maage_player *player;
    struct maage_gameplay *gameplay;
    maage_item_stack *held;
    int creative, handled;
    maage_status_trace *trace;
    void (*emit_sound)(void *, unsigned long, const double[3], double, double);
    void *sound_context;
} maage_entity_context;
/* World owns this store. Unloaded terrain freezes entities in retained World
 * ownership; column jobs never borrow or serialize entity storage. One atomic
 * world checkpoint contains all active and dormant entities and the player. */
maage_entities *maage_world_entities(const maage_world *world);
maage_status maage_entities_create(maage_entities **output);
maage_status maage_entities_destroy(maage_entities *entities);
maage_status maage_entities_reserve(maage_entities *entities, size_t additional);
maage_status maage_entities_bind_types(maage_entities *, const maage_entity_type_registry *);
/* Derive independent entity, behavior and spawn RNG states once from a new
 * World's seed. Restoring a checkpoint replaces all three saved states. */
maage_status maage_entities_seed(maage_entities *, maage_world_seed);
maage_status maage_entity_policy_init(maage_entity_policy *);
maage_status maage_entities_set_policy(maage_entities *, const maage_entity_policy *);
maage_status maage_entities_spawn_type(maage_entities *, maage_world *, maage_entity_type_id, const double[3], unsigned long variant, int persistent, maage_entity_id *);
/* Registered callbacks can compose these allocation-free steering/interaction
 * services. All calls require the Server owner thread. */
maage_status maage_entities_behavior_standard(const maage_entity_type_definition *, maage_entity *, maage_entity_context *);
maage_status maage_entities_shear(maage_entity_context *, maage_entity *, const maage_item_stack *);
maage_status maage_entities_living_tick(maage_entities *, maage_world *, const maage_item_registry *, maage_player *, struct maage_gameplay *, void (*)(void *, unsigned long, const double[3], double, double), void *, maage_status_trace *);
maage_status maage_entities_player_action(maage_entities *, maage_world *, const maage_item_registry *, maage_player *, struct maage_gameplay *, double yaw, double pitch, int attack, int use, int *handled, void (*)(void *, unsigned long, const double[3], double, double), void *, maage_status_trace *);
/* Reserve before authoritative block mutation; spawning afterward cannot allocate. */
maage_status maage_entities_spawn(maage_entities *entities, const maage_item_registry *items, const maage_item_stack *stack, const double position[3], const double velocity[3], unsigned int delay);
/* Owner transaction: reserve, expected-state remove, then publish without allocation. */
maage_status maage_entities_fall(maage_world *world, int x, int y, int z, maage_block_state_id state);
const maage_item_stack *maage_entity_stack(const maage_entity *entity);
maage_status maage_entities_tick(maage_entities *entities, maage_world *world, const maage_item_registry *items, const maage_player *player, maage_inventory *inventory, int collect, unsigned int *collected, maage_entity_pickup *pickups, size_t capacity, size_t *written);
unsigned long maage_entities_random(const maage_entities *entities);
maage_status maage_entities_set_random(maage_entities *entities, unsigned long random);
size_t maage_entities_count(const maage_entities *entities);
const maage_entity *maage_entities_at(const maage_entities *entities, size_t index);
int maage_entity_valid(const maage_entity *entity, const maage_item_registry *items);
int maage_entity_registered_valid(const maage_entity *, const maage_item_registry *, const maage_entity_type_registry *);
int maage_entity_view_valid(const maage_entity_view *, const maage_item_registry *, const maage_entity_type_registry *);
void maage_entity_copy_view(const maage_entity *, maage_entity_view *);
maage_status maage_entities_snapshot(const maage_entities *entities, maage_entity_save *save);
maage_status maage_entities_restore(maage_entities *entities, const maage_item_registry *items, const maage_entity_save *save);
maage_status maage_entity_save_clear(maage_entity_save *save);
/* Shared swept AABB collision for player and entities. Position is feet/center
 * XZ. Returns collided, updating position and clipping velocity on that axis. */
int maage_world_sweep_body(const maage_world *world, double position[3], double velocity[3], double width, double height, int axis, double displacement);
/* Allocation-free slab intersection. Direction is normalized; position is feet
 * and center XZ. Callers share the same 0.1-block selection border. */
int maage_entity_ray_box(const double[3], const double[3], const double[3], double width, double height, double range, double *distance);
#endif
