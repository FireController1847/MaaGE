#ifndef MAAGE_CONTAINERS_H
#define MAAGE_CONTAINERS_H
#include "maage/crafting.h"
#include "maage/blocks.h"

struct maage_world;
struct maage_player;
struct maage_gameplay;
typedef unsigned long maage_container_id;
typedef struct maage_container_registry maage_container_registry;
enum {
    MAAGE_CONTAINER_SLOT_INPUT = 1,
    MAAGE_CONTAINER_SLOT_OUTPUT = 2
};
enum {
    MAAGE_CONTAINER_SLOT_INSERT = 1,
    MAAGE_CONTAINER_SLOT_EXTRACT = 2,
    MAAGE_CONTAINER_SLOT_REVISION = 4
};
/* Slots are registered independently of the copied transport window. Larger
 * interfaces page through the same bounded value-only view; group/slot retain
 * their absolute numeric transaction identities across pages. Storage identifies
 * the existing recipe scratch only; custom samplers own their backing storage. */
typedef struct maage_container_slot_definition {
    unsigned int group, slot, storage, flags;
    int x, y;
} maage_container_slot_definition;
typedef struct maage_container_progress_definition {
    maage_texture_id texture;
    float uv[4];
    int x, y, width, height;
    unsigned int steps, extra_pixel;
    int vertical, require_active;
} maage_container_progress_definition;
typedef struct maage_container_presentation {
    maage_texture_id background, creative_background, creative_scrollbar;
    unsigned int texture_edge, width, height, creative_width, creative_height;
    const char *title, *inventory_title;
    int title_x, title_y, title_centered, inventory_x, inventory_y;
    int player_preview, show_inventory;
    int inventory_slot_x, inventory_slot_y, hotbar_y;
} maage_container_presentation;
typedef struct maage_container_definition {
    maage_container_id id;
    const maage_container_slot_definition *slots;
    size_t slot_count;
    const maage_container_progress_definition *progress;
    size_t progress_count;
    maage_container_presentation presentation;
    unsigned int grid_width;        /* Zero for custom transactions, otherwise recipe 2x2/3x3. */
    const void *context;            /* Borrowed immutable module state, available to every callback. */
    int personal, creative_catalog; /* Capabilities, not content identities. */
    /* Callbacks execute only on the Server/World owner. Mutations preflight all
     * inventory/results and backing-store capacity before publishing ownership.
     * A failed action must retain uncommitted items. Actions preserve container
     * identity, coordinates and page, and advance revisions without wrapping.
     * closed() is retryable and receives closing == 1; failed closure blocks
     * further actions while the owner retries. Retain ownership until complete. */
    maage_status (*opened)(const void *, struct maage_world *, maage_crafting_container *);
    maage_status (*closed)(const void *, struct maage_world *, struct maage_gameplay *, const struct maage_player *);
    int (*accessible)(const void *, const struct maage_world *, const maage_crafting_container *);
    maage_status (*policy)(const void *, const struct maage_world *, const struct maage_gameplay *, const maage_container_slot_definition *, unsigned int);
    /* NULL action uses canonical recipe input/result groups, matching scratch
     * storage and grid indices. Custom actions may define their own slot groups. */
    maage_status (*action)(const void *, struct maage_world *, struct maage_gameplay *, unsigned long, unsigned int, unsigned int, unsigned int);
    /* Fill view[] and progress[] at the shared page origin view_first;
     * view_count/progress_count describe each independent window. No borrowed pointers
     * may enter the snapshot. Invoked on the authoritative owner thread. */
    maage_status (*sample)(const void *, const struct maage_world *, maage_crafting_container *);
} maage_container_definition;
/* Definitions, descriptors and label strings are copied. Callback code and
 * module backing state must outlive registries/World. Registration is growable;
 * runtime IDs are direct O(1), names resolve only at registration boundaries. */
maage_status maage_container_registry_create(maage_container_registry **);
maage_status maage_container_registry_destroy(maage_container_registry *);
maage_status maage_container_register(maage_container_registry *, const char *, maage_container_definition *);
maage_status maage_container_finalize(maage_container_registry *);
int maage_container_is_finalized(const maage_container_registry *);
size_t maage_container_count(const maage_container_registry *);
const maage_container_definition *maage_container_get(const maage_container_registry *, maage_container_id);
const char *maage_container_name(const maage_container_registry *, maage_container_id);
maage_status maage_container_resolve(const maage_container_registry *, const char *, maage_container_id *);
/* Bind the front-end's ordinary inventory action explicitly, before finalizing. */
maage_status maage_container_bind_inventory(maage_container_registry *, maage_container_id);
maage_container_id maage_container_inventory(const maage_container_registry *);
maage_status maage_container_open(const maage_container_registry *, maage_crafting_container *, maage_container_id, int, int, int, struct maage_world *);
maage_status maage_container_sample(const maage_container_registry *, const struct maage_world *, maage_crafting_container *);
maage_status maage_container_page(const maage_container_registry *, maage_crafting_container *, unsigned long);
int maage_container_accessible(const maage_container_registry *, const maage_crafting_container *, const struct maage_player *, const struct maage_world *);
maage_status maage_container_action(const maage_container_registry *, struct maage_world *, struct maage_gameplay *, const struct maage_player *, const maage_recipe_registry *, unsigned long, unsigned int, unsigned int, unsigned int);
maage_status maage_container_close(const maage_container_registry *, struct maage_world *, struct maage_gameplay *, const struct maage_player *);
#endif
