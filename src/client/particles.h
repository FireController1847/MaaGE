#ifndef MAAGE_CLIENT_PARTICLES_H
#define MAAGE_CLIENT_PARTICLES_H
#include "item_models.h"
#include "maage/world.h"
typedef struct maage_client_particles maage_client_particles;
/* Billboard meshes are borrowed until their particles expire or are cleared.
 * Emit copies definitions; only emission grows retained CPU storage. */
typedef struct maage_client_particle_definition {
    const maage_render_mesh *mesh;
    double position[3], velocity[3], size, gravity, drag, ground_drag;
    unsigned int lifetime;
} maage_client_particle_definition;
maage_status maage_client_particles_emit(maage_client_particles *, const maage_client_particle_definition *, size_t, double);
maage_status maage_client_particles_create(maage_renderer *, maage_render_texture *, const maage_client_resources *, const maage_block_registry *, maage_client_particles **);
maage_status maage_client_particles_break(maage_client_particles *, maage_world *, maage_block_state_id, int, int, int, unsigned long, double, maage_status_trace *);
size_t maage_client_particles_count(const maage_client_particles *);
void maage_client_particles_clear(maage_client_particles *);
/* Renderer-owner calls only. Draw storage holds the incoming count plus
 * particles_count entries; simulation can only reduce that count. */
maage_status maage_client_particles_draw(maage_client_particles *, maage_world *, const double[3], double, int, maage_render_instance *, size_t *);
void maage_client_particles_destroy(maage_client_particles *);
#endif
