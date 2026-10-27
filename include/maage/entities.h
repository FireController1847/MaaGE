#ifndef MAAGE_ENTITIES_H
#define MAAGE_ENTITIES_H
#include "maage/items.h"
#include "maage/player.h"
/* Instance identity, distinct from type, item and block identities. Never reused
 * within a world. Explicit encoding uses 32-bit words, never native layouts. */
typedef unsigned long maage_entity_id;
enum { MAAGE_ENTITY_ITEM = 1 };
typedef struct maage_entity {
    maage_entity_id id;
    unsigned int type;
    double position[3], previous[3], velocity[3]; /* Feet, blocks/tick. */
    unsigned long age;
    unsigned int pickup_delay;
    int grounded;
    union { maage_item_stack item; } payload;
    unsigned char light; /* Copied presentation observation; not persistent. */
} maage_entity;
typedef struct maage_entity_save {
    maage_entity *entities;
    size_t count;
    unsigned long next_id, random;
} maage_entity_save;
typedef struct maage_entities maage_entities;
/* World owns this store. Unloaded terrain freezes entities in retained World
 * ownership; column jobs never borrow or serialize entity storage. One atomic
 * world checkpoint contains all active and dormant entities and the player. */
maage_entities *maage_world_entities(const maage_world *world);
maage_status maage_entities_create(maage_entities **output);
maage_status maage_entities_destroy(maage_entities *entities);
maage_status maage_entities_reserve(maage_entities *entities, size_t additional);
/* Reserve before authoritative block mutation; spawning afterward cannot allocate. */
maage_status maage_entities_spawn(maage_entities *entities, const maage_item_registry *items,
    const maage_item_stack *stack, const double position[3], const double velocity[3],
    unsigned int delay);
maage_status maage_entities_tick(maage_entities *entities, const maage_world *world,
    const maage_item_registry *items, const maage_player *player,
    maage_inventory *inventory, int collect, unsigned int *collected);
unsigned long maage_entities_random(const maage_entities *entities);
maage_status maage_entities_set_random(maage_entities *entities, unsigned long random);
size_t maage_entities_count(const maage_entities *entities);
const maage_entity *maage_entities_at(const maage_entities *entities, size_t index);
int maage_entity_valid(const maage_entity *entity, const maage_item_registry *items);
maage_status maage_entities_snapshot(const maage_entities *entities, maage_entity_save *save);
maage_status maage_entities_restore(maage_entities *entities, const maage_item_registry *items,
    const maage_entity_save *save);
maage_status maage_entity_save_clear(maage_entity_save *save);
/* Shared swept AABB collision for player and entities. Position is feet/center
 * XZ. Returns collided, updating position and clipping velocity on that axis. */
int maage_world_sweep_body(const maage_world *world, double position[3],
    double velocity[3], double width, double height, int axis, double displacement);
#endif
