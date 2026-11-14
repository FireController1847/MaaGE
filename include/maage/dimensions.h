#ifndef MAAGE_DIMENSIONS_H
#define MAAGE_DIMENSIONS_H
#include "maage/generation.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef unsigned long maage_dimension_id;
typedef struct maage_dimension_registry maage_dimension_registry;
/* Numeric owner request. Arrival can return NOOP while its required columns
 * are unavailable; the Server streams the destination without moving authority. */
typedef struct maage_dimension_travel {
    maage_dimension_id destination;
    double position[3];
    unsigned long kind;
    unsigned int delay;
    unsigned int cooldown; /* Optional elapsed-tick delay, never refreshed by contact. */
    int arrival_radius; /* Minimum destination streaming radius in chunks; zero uses normal view. */
    unsigned long presentation;
    int survival_screen_effect; /* Suppress only the screen effect in Creative, not sounds. */
    int credits;  /* Request the per-player, per-session-World credits milestone on arrival. */
    int at_spawn; /* Resolve the destination's spawn before streaming. */
} maage_dimension_travel;
typedef struct maage_dimension_definition {
    maage_dimension_id id, respawn; /* Zero respawns in this dimension. */
    const char *name, *display_name;
    const void *context; /* Immutable, borrowed through all Worlds/jobs. */
    int primary, skylight, weather, celestial, water_evaporates;
    double coordinate_scale, ambient_light;
    double fog_distance; /* Zero uses the Client viewing distance. */
    float fog[3];
    maage_status (*generate)(const void *, maage_chunk *, maage_world_seed, const maage_block_registry *, const maage_biome_registry *, const maage_generation_policy *, const maage_job_token *, maage_status_trace *);
    maage_status (*spawn)(const void *, maage_world_seed, const maage_block_registry *, const maage_biome_registry *, const maage_generation_policy *, double[3]);
    maage_status (*contact)(const void *, maage_world *, const double[3], int, maage_dimension_travel *);
    maage_status (*arrive)(const void *, maage_world *, maage_dimension_travel *);
} maage_dimension_definition;
/* Register in DEFINE/BIND through content. Names/labels are copied; zero is
 * invalid. IDs are allocated, finalized access is direct O(1). */
maage_status maage_dimension_registry_create(maage_dimension_registry **);
maage_status maage_dimension_registry_destroy(maage_dimension_registry *);
maage_status maage_dimension_register(maage_dimension_registry *, const char *, maage_dimension_definition *);
maage_status maage_dimension_finalize(maage_dimension_registry *);
size_t maage_dimension_count(const maage_dimension_registry *);
const maage_dimension_definition *maage_dimension_get(const maage_dimension_registry *, maage_dimension_id);
maage_status maage_dimension_find(const maage_dimension_registry *, const char *, maage_dimension_id *);
maage_dimension_id maage_dimension_primary(const maage_dimension_registry *);
const maage_dimension_definition *maage_world_dimension(const maage_world *);
#ifdef __cplusplus
}
#endif
#endif
