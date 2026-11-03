#ifndef MAAGE_CREATIVE_H
#define MAAGE_CREATIVE_H
#include "maage/items.h"
#include "maage/presentation.h"

typedef unsigned long maage_creative_tab_id;
typedef unsigned long maage_creative_entry_id;
typedef struct maage_creative_registry maage_creative_registry;
enum {
    MAAGE_CREATIVE_CATALOG = 0,
    MAAGE_CREATIVE_SEARCH,
    MAAGE_CREATIVE_INVENTORY
};
/* Optional presentation-only icon geometry in a 16x16 logical cell. This can
 * depict content which has no obtainable item. UVs address registered textures. */
typedef struct maage_creative_icon_quad {
    maage_texture_id texture;
    float position[4][2], uv[4][2], color[4];
} maage_creative_icon_quad;
typedef struct maage_creative_tab_definition {
    maage_creative_tab_id id;
    const char *title;
    long order; /* Stable ties retain registration order, independently of IDs. */
    unsigned int kind;
    maage_texture_id background;
    maage_item_stack icon;
    const maage_creative_icon_quad *icon_quads;
    size_t icon_quad_count;
} maage_creative_tab_definition;
typedef struct maage_creative_entry {
    maage_creative_entry_id id;
    maage_creative_tab_id tab;
    maage_item_stack stack; /* Explicit pristine variant, count one. */
    long order, search_order;
    int searchable;
    const char *tooltip; /* Optional extra lines, copied at registration. */
} maage_creative_entry;
/* Owned by Content; borrows its item/presentation registries. Names, labels,
 * descriptors and icon geometry are copied. Register tabs in define and numeric
 * item assignments in define/resolve. No implicit metadata enumeration or caps.
 * Search and inventory are optional singleton roles; category tabs are growable.
 * Finalization validates every stack/texture and prepares immutable ordered
 * entry-ID views. IDs are direct O(1); names resolve only at module boundaries. */
maage_status maage_creative_create(const maage_item_registry *, const maage_presentation_registry *, maage_creative_registry **);
maage_status maage_creative_destroy(maage_creative_registry *);
maage_status maage_creative_tab_register(maage_creative_registry *, const char *, maage_creative_tab_definition *);
maage_status maage_creative_tab_resolve(const maage_creative_registry *, const char *, maage_creative_tab_id *);
maage_status maage_creative_entry_register(maage_creative_registry *, maage_creative_entry *);
maage_status maage_creative_finalize(maage_creative_registry *);
int maage_creative_is_finalized(const maage_creative_registry *);
size_t maage_creative_tab_count(const maage_creative_registry *);
size_t maage_creative_entry_count(const maage_creative_registry *);
/* Borrowed definition pointers are stable after finalization; registration may
 * move arrays. Retain numeric IDs across registration calls. */
const maage_creative_tab_definition *maage_creative_tab(const maage_creative_registry *, maage_creative_tab_id);
const maage_creative_tab_definition *maage_creative_tab_at(const maage_creative_registry *, size_t);
const maage_creative_entry *maage_creative_get(const maage_creative_registry *, maage_creative_entry_id);
/* Search has a deduplicated union of explicitly searchable variants, sorted by
 * search_order/registration order. Returned view borrows until registry destroy. */
const maage_creative_entry_id *maage_creative_view(const maage_creative_registry *, maage_creative_tab_id, size_t *);
#endif
