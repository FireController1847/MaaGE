#ifndef MAAGE_BLOCK_ENTITY_H
#define MAAGE_BLOCK_ENTITY_H
#include "maage/items.h"
#include "maage/models.h"

/* Copied model publication; contains no inventory, callback or live state. */
typedef struct maage_block_entity_view {
    unsigned long identity;
    maage_model_id model;
    maage_block_state_id state;
    int x, y, z, secondary_x, secondary_y, secondary_z;
    maage_block_state_id secondary_state; /* INVALID when the model has one backing block. */
    double translation[3], rotation[3], previous_charge;
    maage_model_animation animation;
} maage_block_entity_view;

/* Portable codec services translate item IDs through the World dictionary.
 * Exactly encoded_size bytes must be consumed. Statuses preserve I/O failures.
 * The service/context is borrowed only during one encode/decode invocation. */
typedef struct maage_entity_codec {
    void *context;
    maage_status (*word)(void *, unsigned long *, unsigned int);
    maage_status (*stack)(void *, maage_item_stack *);
} maage_entity_codec;
typedef struct maage_block_entity_handler {
    unsigned long id, version;
    size_t size, encoded_size, drop_slots;
    unsigned int snapshot_ticks;
    const void *context;
    int (*valid)(const struct maage_block_entity_handler *, const maage_block_state_definition *, const maage_item_registry *, const void *);
    maage_status (*initialize)(const struct maage_block_entity_handler *, const maage_block_state_definition *, void *, unsigned long, int);
    /* Coordinates are world X/Y/Z. present is read-only and allocation-free;
     * count/write publication can sample it twice in the same owner phase. */
    maage_status (*tick)(const struct maage_block_entity_handler *, struct maage_world *, int, int, int, const maage_block_state_definition *, void *, maage_block_state_id *);
    maage_status (*present)(const struct maage_block_entity_handler *, const struct maage_world *, int, int, int, const maage_block_state_definition *, const void *, maage_block_entity_view *);
    int (*can_commit)(const void *, const void *);
    const maage_item_stack *(*drop)(const void *, size_t);
    maage_status (*encode)(const void *, maage_entity_codec *);
    maage_status (*decode)(void *, maage_entity_codec *);
} maage_block_entity_handler;
/* Registry copies handler/name, assigns a distinct numeric type, and owns the
 * returned stable pointer. Context is borrowed and immutable after publication. */
maage_status maage_block_registry_register_entity(maage_block_registry *, const char *, const maage_block_entity_handler *, const maage_block_entity_handler **);
const maage_block_entity_handler *maage_block_registry_entity(const maage_block_registry *, unsigned long);
/* Owner copies; commit delegates validation to the registered handler. */
maage_status maage_world_entity_read(const struct maage_world *, int, int, int, unsigned long, void *, size_t);
maage_status maage_world_entity_commit(struct maage_world *, int, int, int, unsigned long, const void *, size_t);
typedef struct maage_block_entity_update {
    int x, y, z;
    unsigned long type;
    const void *state;
    size_t size;
} maage_block_entity_update;
/* Validate the entire owner transaction before copying any state. No allocation. */
maage_status maage_world_entities_commit(struct maage_world *, const maage_block_entity_update *, size_t);
/* Owner-thread two-pass count/write over loaded columns in the numeric interest square. */
maage_status maage_world_entity_views(const struct maage_world *, int center_x, int center_z, int radius, maage_block_entity_view *, size_t, size_t *);
#endif
