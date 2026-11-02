#ifndef MAAGE_FLUIDS_H
#define MAAGE_FLUIDS_H
#include "maage/content_ids.h"
#include "maage/status.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned long maage_fluid_id;
#define MAAGE_FLUID_NONE 0UL
#define MAAGE_FLUID_PROPERTY_SWIMMING 1U
#define MAAGE_FLUID_PROPERTY_SUBMERGES 2U
#define MAAGE_FLUID_PROPERTY_RESETS_FALL 4U
struct maage_block_registry;
struct maage_block_state_definition;
struct maage_world;
typedef struct maage_fluid_registry maage_fluid_registry;
/* CONTENT: inspect source/target; replace the source on ordinary contact or
 * the target on a directed flow. INVALID leaves both untouched. A nonzero delay
 * expedites a neighbor-triggered update. For directed flow, allow permits
 * displacement when no replacement is returned. Callbacks are allocation-free. */
typedef maage_status (*maage_fluid_contact)(const void *context, const struct maage_block_state_definition *source, const struct maage_block_state_definition *target, int flowing, int downward, unsigned long *replacement, unsigned int *delay, int *allow);
typedef struct maage_fluid_definition {
    maage_fluid_id id;
    unsigned int flow_levels, tick_delay, level_step, falling_spread_level, source_neighbors, routing_depth;
    const unsigned long *states; /* Normal levels then falling levels; registry copies. */
    size_t state_count;
    int drops_displaced, reverse_faces;
    int swimming, submerges, resets_fall;
    double acceleration, drag, gravity, swim_impulse, current_impulse, wall_exit_impulse;
    unsigned long material; /* Registered sound material; zero uses global bindings. */
    float fog_color[3];
    float fog_density, fov_scale;
    int fog_light_scaled, has_overlay_texture;
    unsigned long overlay_texture; /* Registered texture; interpreted only when enabled. */
    maage_fluid_contact contact;
    const void *context; /* Borrowed immutable module data; outlives registry. */
} maage_fluid_definition;
/* IDs are assigned from one; zero is absence, independent of content identity.
 * Definitions/names/state maps are copied; callbacks/context remain borrowed.
 * Registration may omit the map until resolution; finalize validates every
 * mapped block state, then all runtime lookups are deterministic direct O(1). */
maage_status maage_fluid_registry_create(maage_fluid_registry **output);
maage_status maage_fluid_register(maage_fluid_registry *registry, const char *name, maage_fluid_definition *definition);
maage_status maage_fluid_update(maage_fluid_registry *registry, const maage_fluid_definition *definition);
maage_status maage_fluid_finalize(maage_fluid_registry *registry, const struct maage_block_registry *blocks);
int maage_fluid_is_finalized(const maage_fluid_registry *registry);
size_t maage_fluid_count(const maage_fluid_registry *registry);
const maage_fluid_definition *maage_fluid_get(const maage_fluid_registry *registry, maage_fluid_id id);
maage_status maage_fluid_resolve(const maage_fluid_registry *registry, const char *name, maage_fluid_id *id);
const char *maage_fluid_name(const maage_fluid_registry *registry, maage_fluid_id id);
maage_status maage_fluid_destroy(maage_fluid_registry *registry);
unsigned int maage_fluid_properties(const maage_fluid_definition *definition);
#ifdef __cplusplus
}
#endif
#endif
