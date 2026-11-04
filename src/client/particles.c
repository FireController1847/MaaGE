#include "particles.h"
#include "player_model.h"
#include "maage/entities.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

typedef struct particle_mesh {
    maage_block_state_id state;
    float tint[4], uv[4], animation[3];
    maage_status result;
    maage_render_mesh *mesh[16];
    struct particle_mesh *next;
} particle_mesh;
typedef struct client_particle {
    const maage_render_mesh *mesh;
    double position[3], previous[3], velocity[3], size, gravity, drag, ground_drag, born;
    unsigned int age, lifetime;
} client_particle;
struct maage_client_particles {
    maage_renderer *renderer;
    maage_render_texture *atlas;
    const maage_block_registry *blocks;
    maage_client_resources resources;
    particle_mesh *meshes;
    client_particle *data;
    size_t count, capacity;
    double time, remainder;
};
static maage_status particle_status(unsigned char code) {
    return maage_status_make(code, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_PRESENT, 0);
}
static double particle_random(unsigned long *random) {
    *random = (*random * 1664525UL + 1013904223UL) & 4294967295UL;
    return (*random >> 8) / 16777216.0;
}
maage_status maage_client_particles_create(maage_renderer *renderer, maage_render_texture *atlas, const maage_client_resources *resources, const maage_block_registry *blocks, maage_client_particles **output) {
    maage_client_particles *particles;
    if (output) *output = NULL;
    if (!renderer || !atlas || !resources || !blocks || !output) return particle_status(MAAGE_STATUS_INVALID_ARGUMENT);
    particles = (maage_client_particles *) calloc(1, sizeof(*particles));
    *output = particles;
    if (!particles) return particle_status(MAAGE_STATUS_ALLOCATION_FAILED);
    particles->renderer = renderer;
    particles->atlas = atlas;
    particles->resources = *resources;
    particles->blocks = blocks;
    return particle_status(MAAGE_STATUS_SUCCESS);
}
static maage_status particle_appearance(maage_client_particles *particles, maage_block_state_id id, const float tint[4], particle_mesh **output, maage_status_trace *trace) {
    static const unsigned int indices[6] = {0, 1, 2, 0, 2, 3};
    const maage_block_state_definition *state = maage_block_registry_state(particles->blocks, id);
    maage_mesh_vertex vertices[4];
    maage_mesh_data data;
    particle_mesh *mesh;
    unsigned int tile, corner;
    maage_status result;
    for (mesh = particles->meshes; mesh; mesh = mesh->next)
        if (state && !memcmp(mesh->uv, state->faces[MAAGE_FACE_NEGATIVE_Y].uv, sizeof(mesh->uv)) && !memcmp(mesh->animation, state->faces[MAAGE_FACE_NEGATIVE_Y].animation, sizeof(mesh->animation)) && !memcmp(mesh->tint, tint, sizeof(mesh->tint))) {
            *output = mesh;
            return mesh->result;
        }
    if (!state) return particle_status(MAAGE_STATUS_INVALID_ARGUMENT);
    mesh = (particle_mesh *) calloc(1, sizeof(*mesh));
    if (!mesh) return particle_status(MAAGE_STATUS_ALLOCATION_FAILED);
    mesh->state = id;
    memcpy(mesh->uv, state->faces[MAAGE_FACE_NEGATIVE_Y].uv, sizeof(mesh->uv));
    memcpy(mesh->animation, state->faces[MAAGE_FACE_NEGATIVE_Y].animation, sizeof(mesh->animation));
    memcpy(mesh->tint, tint, sizeof(mesh->tint));
    mesh->next = particles->meshes;
    particles->meshes = mesh;
    *output = mesh;
    for (tile = 0; tile < 16; ++tile) {
        const float *uv = state->faces[MAAGE_FACE_NEGATIVE_Y].uv;
        memset(vertices, 0, sizeof(vertices));
        for (corner = 0; corner < 4; ++corner) {
            maage_mesh_vertex *vertex = &vertices[corner];
            vertex->position[0] = corner == 1 || corner == 2 ? 1 : -1;
            vertex->position[1] = corner >= 2 ? 1 : -1;
            vertex->uv[0] = uv[0] + (uv[2] - uv[0]) * ((tile % 4 + (corner == 1 || corner == 2 ? 1 : 0)) / 4.0f);
            vertex->uv[1] = uv[1] + (uv[3] - uv[1]) * ((tile / 4 + (corner < 2 ? 1 : 0)) / 4.0f);
            vertex->color[0] = tint[0] * 0.6f;
            vertex->color[1] = tint[1] * 0.6f;
            vertex->color[2] = tint[2] * 0.6f;
            vertex->color[3] = 1;
            memcpy(vertex->animation, mesh->animation, sizeof(mesh->animation));
            vertex->material = MAAGE_MESH_MATERIAL_CUTOUT;
            vertex->light[0] = 1;
        }
        memset(&data, 0, sizeof(data));
        data.vertices = vertices;
        data.indices = indices;
        data.vertex_count = 4;
        data.index_count = 6;
        result = maage_renderer_upload_geometry(particles->renderer, &data, particles->atlas, &mesh->mesh[tile], trace);
        if (maage_status_failed(result)) {
            for (corner = 0; corner < tile; ++corner)
                maage_render_mesh_destroy(mesh->mesh[corner]);
            memset(mesh->mesh, 0, sizeof(mesh->mesh));
            mesh->result = result;
            return result;
        }
    }
    return mesh->result = particle_status(MAAGE_STATUS_SUCCESS);
}
maage_status maage_client_particles_emit(maage_client_particles *particles, const maage_client_particle_definition *definitions, size_t count, double now) {
    client_particle *grown, *particle;
    size_t capacity, i;
    unsigned int axis;
    if (!particles || (count && !definitions) || !(now >= 0 && now >= particles->time)) return particle_status(MAAGE_STATUS_INVALID_ARGUMENT);
    if (!count) return particle_status(MAAGE_STATUS_NOOP);
    for (i = 0; i < count; ++i) {
        if (!definitions[i].mesh || !definitions[i].lifetime || !(definitions[i].size > 0 && definitions[i].size < 1e6) || !(definitions[i].gravity >= 0 && definitions[i].gravity < 1e6) || !(definitions[i].drag >= 0 && definitions[i].drag <= 1) || !(definitions[i].ground_drag >= 0 && definitions[i].ground_drag <= 1)) return particle_status(MAAGE_STATUS_INVALID_ARGUMENT);
        for (axis = 0; axis < 3; ++axis)
            if (!(definitions[i].position[axis] > -1e30 && definitions[i].position[axis] < 1e30 && definitions[i].velocity[axis] > -1e6 && definitions[i].velocity[axis] < 1e6)) return particle_status(MAAGE_STATUS_INVALID_ARGUMENT);
    }
    if (particles->count > (size_t) - 1 - count) return particle_status(MAAGE_STATUS_BUFFER_OVERFLOW);
    capacity = particles->capacity ? particles->capacity : 256;
    while (capacity < particles->count + count) {
        if (capacity > (size_t) - 1 / 2) return particle_status(MAAGE_STATUS_BUFFER_OVERFLOW);
        capacity *= 2;
    }
    if (capacity > (size_t) - 1 / sizeof(*grown)) return particle_status(MAAGE_STATUS_BUFFER_OVERFLOW);
    if (capacity > particles->capacity) {
        grown = (client_particle *) realloc(particles->data, capacity * sizeof(*grown));
        if (!grown) return particle_status(MAAGE_STATUS_ALLOCATION_FAILED);
        particles->data = grown;
        particles->capacity = capacity;
    }
    for (i = 0; i < count; ++i) {
        particle = &particles->data[particles->count++];
        memset(particle, 0, sizeof(*particle));
        particle->born = now;
        particle->mesh = definitions[i].mesh;
        particle->size = definitions[i].size;
        particle->lifetime = definitions[i].lifetime;
        particle->gravity = definitions[i].gravity;
        particle->drag = definitions[i].drag;
        particle->ground_drag = definitions[i].ground_drag;
        memcpy(particle->position, definitions[i].position, sizeof(particle->position));
        memcpy(particle->previous, particle->position, sizeof(particle->previous));
        memcpy(particle->velocity, definitions[i].velocity, sizeof(particle->velocity));
    }
    if (!particles->time) particles->time = now;
    return particle_status(count ? MAAGE_STATUS_SUCCESS : MAAGE_STATUS_NOOP);
}
maage_status maage_client_particles_break(maage_client_particles *particles, maage_world *world, maage_block_state_id state, int x, int y, int z, unsigned long random, double now, maage_status_trace *trace) {
    particle_mesh *appearance;
    client_particle *particle;
    client_particle generated;
    maage_client_particle_definition fragments[64];
    unsigned int i, axis;
    double length, speed;
    maage_world_observation observation;
    const maage_block_state_definition *definition = maage_block_registry_state(particles->blocks, state);
    const maage_block_face_definition *face;
    const maage_biome_definition *biome = NULL;
    float tint[4];
    maage_status result;
    if (!definition) return particle_status(MAAGE_STATUS_INVALID_ARGUMENT);
    face = &definition->faces[MAAGE_FACE_NEGATIVE_Y];
    if (particles->resources.biomes && maage_world_observe(world, x, y, z, &observation) && observation.biome_id < maage_biome_registry_count(maage_content_biomes(particles->resources.content))) biome = &particles->resources.biomes[observation.biome_id];
    maage_client_item_face_tint(biome ? biome->grass_color : particles->resources.inventory_grass_color, face, face->tint_mode, tint);
    if (biome && face->tint_mode == MAAGE_BLOCK_TINT_FOLIAGE)
        for (axis = 0; axis < 3; ++axis)
            tint[axis] = face->tint[axis] * biome->foliage_color[axis];
    result = particle_appearance(particles, state, tint, &appearance, trace);
    if (maage_status_failed(result)) return result;
    for (i = 0; i < 64; ++i) {
        particle = &generated;
        memset(particle, 0, sizeof(*particle));
        particle->position[0] = x + (i % 4 + 0.5) / 4;
        particle->position[1] = y + ((i / 4) % 4 + 0.5) / 4;
        particle->position[2] = z + (i / 16 + 0.5) / 4;
        length = 0;
        for (axis = 0; axis < 3; ++axis) {
            particle->velocity[axis] = particle->position[axis] - (axis == 0 ? x : (axis == 1 ? y : z)) - 0.5 + (particle_random(&random) * 2 - 1) * 0.4;
            length += particle->velocity[axis] * particle->velocity[axis];
        }
        speed = (particle_random(&random) + particle_random(&random) + 1) * 0.15 * 0.4 / sqrt(length > 0 ? length : 1);
        for (axis = 0; axis < 3; ++axis) {
            particle->velocity[axis] *= speed;
            particle->previous[axis] = particle->position[axis];
        }
        particle->velocity[1] += 0.1;
        particle->size = (particle_random(&random) + 1) * 0.05;
        particle->lifetime = (unsigned int) (4 / (particle_random(&random) * 0.9 + 0.1));
        memset(&fragments[i], 0, sizeof(fragments[i]));
        fragments[i].mesh = appearance->mesh[(unsigned int) (particle_random(&random) * 16)];
        memcpy(fragments[i].position, particle->position, sizeof(particle->position));
        memcpy(fragments[i].velocity, particle->velocity, sizeof(particle->velocity));
        fragments[i].size = particle->size;
        fragments[i].lifetime = particle->lifetime;
        fragments[i].gravity = 0.04;
        fragments[i].drag = 0.98;
        fragments[i].ground_drag = 0.7;
    }
    return maage_client_particles_emit(particles, fragments, 64, now);
}
size_t maage_client_particles_count(const maage_client_particles *particles) {
    return particles ? particles->count : 0;
}
void maage_client_particles_clear(maage_client_particles *particles) {
    if (particles) {
        particles->count = 0;
        particles->time = particles->remainder = 0;
    }
}
maage_status maage_client_particles_draw(maage_client_particles *particles, maage_world *world, const double eye[3], double now, int paused, maage_render_instance *draws, size_t *count) {
    size_t i, retained;
    unsigned int axis;
    client_particle *particle;
    double elapsed = particles->time ? now - particles->time : 0, alpha, position[3], dx, dy, dz;
    unsigned char light;
    particles->time = now;
    if (!paused && elapsed > 0) particles->remainder += elapsed;
    while (particles->count && particles->remainder >= 0.05) {
        /* Emission order is chronological. Skip empty history after a stall
         * rather than aging newly received effects through that history. */
        double skip = floor((particles->data[0].born - (now - particles->remainder + 0.05)) / 0.05);
        if (skip > 0) particles->remainder -= skip * 0.05;
        if (particles->remainder < 0.05) break;
        particles->remainder -= 0.05;
        retained = 0;
        for (i = 0; i < particles->count; ++i) {
            particle = &particles->data[i];
            if (particle->born > now - particles->remainder) {
                particles->data[retained++] = *particle;
                continue;
            }
            if (particle->age++ >= particle->lifetime) continue;
            memcpy(particle->previous, particle->position, sizeof(particle->previous));
            particle->velocity[1] -= particle->gravity;
            particle->position[1] -= 0.1;
            for (axis = 0; axis < 3; ++axis) {
                unsigned int direction = axis == 0 ? 1 : (axis == 1 ? 0 : 2);
                maage_world_sweep_body(world, particle->position, particle->velocity, 0.2, 0.2, direction, particle->velocity[direction]);
                particle->velocity[direction] *= particle->drag;
            }
            particle->position[1] += 0.1;
            if (particle->velocity[1] == 0) {
                particle->velocity[0] *= particle->ground_drag;
                particle->velocity[2] *= particle->ground_drag;
            }
            particles->data[retained++] = *particle;
        }
        particles->count = retained;
    }
    if (!particles->count) particles->remainder = 0;
    alpha = particles->remainder / 0.05;
    for (i = 0; i < particles->count; ++i) {
        maage_render_instance *draw = &draws[(*count)++];
        particle = &particles->data[i];
        memset(draw, 0, sizeof(*draw));
        draw->mesh = particle->mesh;
        for (axis = 0; axis < 3; ++axis)
            position[axis] = particle->previous[axis] + (particle->position[axis] - particle->previous[axis]) * alpha;
        dx = eye[0] - position[0];
        dy = eye[1] - position[1];
        dz = eye[2] - position[2];
        maage_player_matrix_identity(draw->transform);
        maage_player_matrix_translate(draw->transform, position[0], position[1], position[2]);
        maage_player_matrix_rotate(draw->transform, 1, atan2(dx, dz));
        maage_player_matrix_rotate(draw->transform, 0, -atan2(dy, sqrt(dx * dx + dz * dz)));
        maage_player_matrix_scale(draw->transform, particle->size, particle->size, particle->size);
        light = 64;
        (void) maage_world_present_light(world, (int) floor(position[0]), (int) floor(position[1]), (int) floor(position[2]), &light);
        draw->sky_light = (light >> 4) / 15.0f;
        draw->block_light = (light & 15) / 15.0f;
    }
    return particle_status(MAAGE_STATUS_SUCCESS);
}
void maage_client_particles_destroy(maage_client_particles *particles) {
    particle_mesh *mesh, *next;
    unsigned int i;
    if (!particles) return;
    for (mesh = particles->meshes; mesh; mesh = next) {
        next = mesh->next;
        for (i = 0; i < 16; ++i)
            maage_render_mesh_destroy(mesh->mesh[i]);
        free(mesh);
    }
    free(particles->data);
    free(particles);
}
