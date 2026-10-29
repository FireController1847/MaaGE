#include "world_internal.h"
#include "maage/jobs.h"
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

struct maage_world_mesh {
    maage_mesh_data data;
    maage_mesh_vertex *vertices;
    unsigned int *indices;
    maage_block_position *owners;
};

static const int maage_face_neighbor[MAAGE_FACE_COUNT][3] = {
    {-1, 0, 0}, {1, 0, 0}, {0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}};
/* Bottom-left, bottom-right, top-right, top-left viewed from outside. */
static const int maage_face_corners[MAAGE_FACE_COUNT][4][3] = {
    {{0, 0, 0}, {0, 0, 1}, {0, 1, 1}, {0, 1, 0}},
    {{1, 0, 1}, {1, 0, 0}, {1, 1, 0}, {1, 1, 1}},
    {{0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1}},
    {{0, 1, 1}, {1, 1, 1}, {1, 1, 0}, {0, 1, 0}},
    {{1, 0, 0}, {0, 0, 0}, {0, 1, 0}, {1, 1, 0}},
    {{0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}}};
typedef struct maage_mesh_illumination {
    float light[4][2], occlusion[4];
} maage_mesh_illumination;

/* The four diagonal inputs are essential: clamping corners to cardinal
 * neighbors gives different AO/tints on opposite sides of a column seam. */
static const maage_chunk *maage_mesh_neighbor(const maage_chunk *chunk,
                                              const maage_chunk *neighbors[8], int *x, int *z) {
    int dx = 0, dz = 0;
    if (*x < 0) {
        *x += MAAGE_CHUNK_EDGE;
        dx = -1;
    } else if (*x >= MAAGE_CHUNK_EDGE) {
        *x -= MAAGE_CHUNK_EDGE;
        dx = 1;
    }
    if (*z < 0) {
        *z += MAAGE_CHUNK_EDGE;
        dz = -1;
    } else if (*z >= MAAGE_CHUNK_EDGE) {
        *z -= MAAGE_CHUNK_EDGE;
        dz = 1;
    }
    if (dx && dz) return neighbors[4 + (dx > 0) + 2 * (dz > 0)];
    if (dx) return neighbors[dx > 0];
    if (dz) return neighbors[2 + (dz > 0)];
    return chunk;
}

static unsigned char maage_mesh_sample(const maage_block_registry *registry,
                                       const maage_chunk *chunk, const maage_chunk *neighbors[8],
                                       int x, int y, int z, unsigned char fallback, int *occludes) {
    const maage_chunk *sample;
    const maage_block_state_definition *state;
    const maage_block_definition *block;
    *occludes = 0;
    if (y >= MAAGE_WORLD_HEIGHT) return 240;
    if (y < 0) {
        *occludes = 1;
        return 0;
    }
    sample = maage_mesh_neighbor(chunk, neighbors, &x, &z);
    if (!sample) return fallback;
    state = maage_block_registry_state(registry, maage_chunk_get(sample, x, y, z));
    block = state ? maage_block_registry_block(registry, state->block_id) : NULL;
    *occludes = block && block->opaque;
    return maage_chunk_light(sample, x, y, z);
}

static void maage_mesh_lighting(const maage_block_registry *registry,
                                const maage_chunk *chunk, const maage_chunk *neighbors[8],
                                int x, int y, int z, unsigned int face, int boundary, int smooth,
                                unsigned int emission, maage_mesh_illumination *output) {
    unsigned char base, a, b, diagonal, own;
    int n[3], first[3], second[3], corner[3], axis, u, v, i, j;
    int center_opaque, side_a, side_b, diagonal_opaque;
    float ao;
    own = maage_chunk_light(chunk, x, y, z);
    n[0] = x;
    n[1] = y;
    n[2] = z;
    if (boundary)
        for (i = 0; i < 3; ++i)
            n[i] += maage_face_neighbor[face][i];
    base = maage_mesh_sample(registry, chunk, neighbors,
                             n[0], n[1], n[2], own, &center_opaque);
    if (emission && (base & 15) < emission) base = (unsigned char) ((base & 240) | emission);
    axis = face / 2;
    u = (axis + 1) % 3;
    v = (axis + 2) % 3;
    for (i = 0; i < 4; ++i) {
        a = b = diagonal = base;
        ao = 1.0f;
        if (smooth) {
            for (j = 0; j < 3; ++j)
                first[j] = second[j] = corner[j] = n[j];
            first[u] += maage_face_corners[face][i][u] ? 1 : -1;
            second[v] += maage_face_corners[face][i][v] ? 1 : -1;
            corner[u] = first[u];
            corner[v] = second[v];
            a = maage_mesh_sample(registry, chunk, neighbors,
                                  first[0], first[1], first[2], base, &side_a);
            b = maage_mesh_sample(registry, chunk, neighbors,
                                  second[0], second[1], second[2], base, &side_b);
            if (side_a && side_b) {
                diagonal = a;
                diagonal_opaque = 1;
            } else
                diagonal = maage_mesh_sample(registry, chunk, neighbors,
                                             corner[0], corner[1], corner[2], base, &diagonal_opaque);
            /* Full opaque neighbors contribute 0.2, open cells 1.0.
             * Zero packed brightness uses the face sample, avoiding light
             * interpolation through the interiors of solid occluders. */
            ao = 1.0f - 0.2f * (center_opaque + side_a + side_b + diagonal_opaque);
            if (!a) a = base;
            if (!b) b = base;
            if (!diagonal) diagonal = base;
        }
        output->light[i][0] = ((base >> 4) + (a >> 4) + (b >> 4) + (diagonal >> 4)) / 60.0f;
        output->light[i][1] = ((base & 15) + (a & 15) + (b & 15) + (diagonal & 15)) / 60.0f;
        output->occlusion[i] = ao;
    }
}

static void maage_mesh_biome_colors(const maage_biome_registry *biomes,
                                    const maage_chunk *chunk, const maage_chunk *neighbors[8], int x, int z,
                                    float colors[4][3]) {
    const maage_chunk *sample;
    const maage_biome_definition *biome;
    int dx, dz, sx, sz, component;
    for (component = 0; component < 3; ++component) {
        colors[MAAGE_BLOCK_TINT_NONE][component] = 1;
        colors[MAAGE_BLOCK_TINT_GRASS][component] = 0;
        colors[MAAGE_BLOCK_TINT_FOLIAGE][component] = 0;
    }
    biome = maage_biome_registry_get(biomes, chunk->biomes[z * 16 + x]);
    for (component = 0; component < 3; ++component)
        colors[MAAGE_BLOCK_TINT_GRASS_LOCAL][component] = biome ? biome->grass_color[component] : 1;
    for (dz = -1; dz <= 1; ++dz)
        for (dx = -1; dx <= 1; ++dx) {
            sx = x + dx;
            sz = z + dz;
            sample = maage_mesh_neighbor(chunk, neighbors, &sx, &sz);
            biome = maage_biome_registry_get(biomes,
                                             sample ? sample->biomes[sz * 16 + sx] : chunk->biomes[z * 16 + x]);
            for (component = 0; component < 3; ++component) {
                colors[MAAGE_BLOCK_TINT_GRASS][component] += (biome ? biome->grass_color[component] : 1) / 9.0f;
                colors[MAAGE_BLOCK_TINT_FOLIAGE][component] += (biome ? biome->foliage_color[component] : 1) / 9.0f;
            }
        }
}

/* Shared corner heights depend on the same four cells in either adjoining
 * column. Source/falling cells anchor the surface; open space lowers edges,
 * and a fluid above any contributor fills the corner to the next voxel. */
static float maage_mesh_fluid_height(const maage_block_registry *registry,
                                     const maage_chunk *chunk, const maage_chunk *neighbors[8],
                                     int x, int y, int z, unsigned int kind) {
    const maage_chunk *sample;
    const maage_block_state_definition *state, *above;
    const maage_block_definition *block;
    int dx, dz, sx, sz;
    unsigned int weight, total = 0, level;
    float depth = 0;
    for (dz = -1; dz <= 0; ++dz)
        for (dx = -1; dx <= 0; ++dx) {
            sx = x + dx;
            sz = z + dz;
            sample = maage_mesh_neighbor(chunk, neighbors, &sx, &sz);
            if (!sample) continue;
            above = maage_block_registry_state(registry, maage_chunk_get(sample, sx, y + 1, sz));
            if (above && above->fluid_kind == kind) return 1.0f;
            state = maage_block_registry_state(registry, maage_chunk_get(sample, sx, y, sz));
            if (state && state->fluid_kind == kind) {
                level = state->fluid_level & 8 ? 0 : state->fluid_level;
                weight = !level ? 11U : 1U;
                depth += weight * (level + 1) / 9.0f;
                total += weight;
            } else {
                block = state ? maage_block_registry_block(registry, state->block_id) : NULL;
                if (block && !block->solid) {
                    depth += 1;
                    ++total;
                }
            }
        }
    return total ? 1.0f - depth / total : 1.0f;
}

static void maage_mesh_fluid_flow(const maage_block_registry *registry,
                                  const maage_chunk *chunk, const maage_chunk *neighbors[8],
                                  int x, int y, int z, const maage_block_state_definition *current, float flow[2]) {
    static const unsigned int faces[4] = {0, 1, 4, 5};
    const maage_chunk *sample;
    const maage_block_state_definition *state;
    const maage_block_definition *block;
    int side, sx, sz, level, difference;
    float length;
    flow[0] = flow[1] = 0;
    level = current->fluid_level & 8 ? 0 : (int) current->fluid_level;
    for (side = 0; side < 4; ++side) {
        sx = x + maage_face_neighbor[faces[side]][0];
        sz = z + maage_face_neighbor[faces[side]][2];
        sample = maage_mesh_neighbor(chunk, neighbors, &sx, &sz);
        if (!sample) continue;
        state = maage_block_registry_state(registry, maage_chunk_get(sample, sx, y, sz));
        if (!state) continue;
        if (state->fluid_kind == current->fluid_kind)
            difference = (state->fluid_level & 8 ? 0 : (int) state->fluid_level) - level;
        else {
            block = maage_block_registry_block(registry, state->block_id);
            if (!block || block->solid) continue;
            state = maage_block_registry_state(registry, maage_chunk_get(sample, sx, y - 1, sz));
            if (!state || state->fluid_kind != current->fluid_kind) continue;
            difference = (state->fluid_level & 8 ? 0 : (int) state->fluid_level) - level + 8;
        }
        flow[0] += maage_face_neighbor[faces[side]][0] * difference;
        flow[1] += maage_face_neighbor[faces[side]][2] * difference;
    }
    length = (float) sqrt(flow[0] * flow[0] + flow[1] * flow[1]);
    if (length > 0) {
        flow[0] /= length;
        flow[1] /= length;
    }
}

static void maage_mesh_face(maage_mesh_vertex *vertices, unsigned int *indices,
                            size_t face_index, int x, int y, int z, unsigned int face,
                            const maage_block_face_definition *definition, const float uv[4],
                            const maage_block_shape *shape, unsigned int model, unsigned int metadata,
                            int log_axis, float material, const float tint[3], const float animation[3],
                            const float fluid_heights[4], const float fluid_flow[2],
                            const maage_mesh_illumination *illumination) {
    static const unsigned int offsets[2][6] = {{0, 1, 2, 0, 2, 3}, {0, 1, 3, 1, 2, 3}};
    static const float shade[MAAGE_FACE_COUNT] = {0.6f, 0.6f, 0.5f, 1.0f, 0.8f, 0.8f};
    static const int cross_corners[4][4][3] = {
        {{0, 0, 0}, {1, 0, 1}, {1, 1, 1}, {0, 1, 0}},
        {{1, 0, 1}, {0, 0, 0}, {0, 1, 0}, {1, 1, 1}},
        {{1, 0, 0}, {0, 0, 1}, {0, 1, 1}, {1, 1, 0}},
        {{0, 0, 1}, {1, 0, 0}, {1, 1, 0}, {0, 1, 1}}};
    static const float cross_normals[4][3] = {
        {-0.70710678f, 0, 0.70710678f}, {0.70710678f, 0, -0.70710678f}, {-0.70710678f, 0, -0.70710678f}, {0.70710678f, 0, 0.70710678f}};
    maage_mesh_vertex *vertex;
    unsigned int base, corner, component, alternate, source;
    const int *point;
    float brightness, local[3], u, v, ao, weights, light[2], t, shear;
    int axis, first, second;
    brightness = model == MAAGE_BLOCK_MESH_CUBE ? shade[face] : 1.0f;
    base = (unsigned int) (face_index * 4);
    axis = face / 2;
    first = (axis + 1) % 3;
    second = (axis + 2) % 3;
    for (corner = 0; corner < 4; ++corner) {
        vertex = &vertices[face_index * 4 + corner];
        point = model == MAAGE_BLOCK_MESH_CROSS ? cross_corners[face][corner] : maage_face_corners[face][corner];
        for (component = 0; component < 3; ++component)
            local[component] = (float) (shape->minimum[component] + point[component] *
                                                                        (shape->maximum[component] - shape->minimum[component]));
        if (fluid_heights && point[1])
            local[1] = fluid_heights[point[0] + 2 * point[2]] - 0.001f;
        ao = illumination->occlusion[corner];
        light[0] = illumination->light[corner][0];
        light[1] = illumination->light[corner][1];
        u = corner == 1 || corner == 2 ? 1.0f : 0.0f;
        v = corner < 2 ? 1.0f : 0.0f;
        if (model == MAAGE_BLOCK_MESH_CUBE) {
            if (face < 2) {
                u = face ? 1.0f - local[2] : local[2];
                v = 1.0f - local[1];
            } else if (face < 4) {
                u = local[0];
                v = face == 2 ? 1.0f - local[2] : local[2];
            } else {
                u = face == 4 ? 1.0f - local[0] : local[0];
                v = 1.0f - local[1];
            }
            if (log_axis >= 0 && axis != log_axis) {
                t = u;
                if (log_axis == 0) {
                    u = v;
                    v = 1.0f - t;
                } else if (log_axis == 2 && axis == 0) {
                    u = v;
                    v = 1.0f - t;
                }
            }
            if (fluid_flow) {
                if (face == MAAGE_FACE_POSITIVE_Y) {
                    u = 0.5f + (2 * local[0] - 1) * 0.25f * fluid_flow[1] -
                        (2 * local[2] - 1) * 0.25f * fluid_flow[0];
                    v = 0.5f + (2 * local[2] - 1) * 0.25f * fluid_flow[1] +
                        (2 * local[0] - 1) * 0.25f * fluid_flow[0];
                } else {
                    u *= 0.5f;
                    v *= 0.5f;
                }
            }
            /* Partial faces interpolate the same four full-cell samples. */
            ao = light[0] = light[1] = 0;
            for (source = 0; source < 4; ++source) {
                weights = maage_face_corners[face][source][first] ? local[first] : 1.0f - local[first];
                weights *= maage_face_corners[face][source][second] ? local[second] : 1.0f - local[second];
                ao += weights * illumination->occlusion[source];
                light[0] += weights * illumination->light[source][0];
                light[1] += weights * illumination->light[source][1];
            }
        } else if (model == MAAGE_BLOCK_MESH_TORCH) {
            u = (7.0f + 2.0f * u) / 16.0f;
            v = face == MAAGE_FACE_POSITIVE_Y ? (6.0f + 2.0f * v) / 16.0f : (face == MAAGE_FACE_NEGATIVE_Y ? (13.0f + 2.0f * v) / 16.0f : (6.0f + 10.0f * v) / 16.0f);
            if (metadata >= 1 && metadata <= 4) {
                shear = -0.1f - 0.4f * (1.0f - local[1]);
                local[metadata <= 2 ? 0 : 2] += metadata & 1 ? shear : -shear;
                local[1] += 0.2f;
            }
        }
        vertex->position[0] = (float) x + local[0];
        vertex->position[1] = (float) y + local[1];
        vertex->position[2] = (float) z + local[2];
        for (component = 0; component < 3; ++component)
            vertex->normal[component] = model == MAAGE_BLOCK_MESH_CROSS ? cross_normals[face][component] : (float) maage_face_neighbor[face][component];
        vertex->uv[0] = uv[0] + u * (uv[2] - uv[0]);
        vertex->uv[1] = uv[1] + v * (uv[3] - uv[1]);
        for (component = 0; component < 3; ++component) {
            vertex->color[component] = definition->tint[component] * tint[component] * brightness * ao;
            vertex->animation[component] = animation ? animation[component] : 0;
        }
        vertex->color[3] = definition->tint[3];
        vertex->light[0] = light[0];
        vertex->light[1] = light[1];
        vertex->material = material;
    }
    alternate = illumination->occlusion[0] + illumination->occlusion[2] >
                illumination->occlusion[1] + illumination->occlusion[3];
    for (corner = 0; corner < 6; ++corner)
        indices[face_index * 6 + corner] = base + offsets[alternate][corner];
}

static void maage_mesh_reverse_face(maage_mesh_vertex *vertices, unsigned int *indices,
                                    size_t source, size_t destination) {
    unsigned int corner, component, offset;
    for (corner = 0; corner < 4; ++corner) {
        vertices[destination * 4 + corner] = vertices[source * 4 + corner];
        for (component = 0; component < 3; ++component)
            vertices[destination * 4 + corner].normal[component] *= -1;
    }
    for (corner = 0; corner < 6; ++corner) {
        offset = corner % 3 == 1 ? corner + 1 : (corner % 3 == 2 ? corner - 1 : corner);
        indices[destination * 6 + corner] = (unsigned int) (destination * 4) +
                                            indices[source * 6 + offset] - (unsigned int) (source * 4);
    }
}

/* Preserve exposed foliage and two interior cutout planes per direction. */
static int maage_mesh_smart_leaf_hidden(const maage_block_registry *registry,
                                        const maage_chunk *chunk, const maage_chunk *neighbors[8],
                                        int x, int y, int z, unsigned int face) {
    const maage_chunk *sample;
    const maage_block_state_definition *state;
    int distance, nx, ny, nz;
    for (distance = 2; distance <= 3; ++distance) {
        nx = x + distance * maage_face_neighbor[face][0];
        ny = y + distance * maage_face_neighbor[face][1];
        nz = z + distance * maage_face_neighbor[face][2];
        if (ny < 0 || ny >= MAAGE_WORLD_HEIGHT) return 0;
        sample = maage_mesh_neighbor(chunk, neighbors, &nx, &nz);
        if (!sample) return 0;
        state = maage_block_registry_state(registry, maage_chunk_get(sample, nx, ny, nz));
        if (!state || state->block_id != MAAGE_BLOCK_LEAVES) return 0;
    }
    return 1;
}

static size_t maage_mesh_generate(const maage_block_registry *registry,
                                  const maage_biome_registry *biomes, const maage_chunk *chunk,
                                  const maage_chunk *neighbors[8], maage_mesh_vertex *vertices,
                                  unsigned int *indices, maage_block_position *owners, maage_leaves_mode mode,
                                  const maage_job_token *token, unsigned int *failure) {
    const maage_block_state_definition *state, *neighbor, *above;
    const maage_block_definition *block, *current;
    const maage_block_face_definition *definition;
    const float *uv, *animation, *flow;
    maage_block_shape shape, face_shape;
    maage_mesh_illumination illumination;
    const maage_chunk *adjacent;
    maage_block_state_id id;
    float colors[4][3], fluid_heights[4], fluid_flow[2], neutral[3] = {1, 1, 1}, material;
    int x, y, z, section, nx, ny, nz, log_axis;
    unsigned int face, axis, face_limit, model, tint_mode;
    int crossed, boundary, snowed, colors_ready, overlay, smooth, reverse;
    size_t face_count = 0;
    for (section = 0; section < 16; ++section) {
        if (token && maage_job_cancelled(token)) {
            *failure = MAAGE_STATUS_INTERRUPTED;
            return (size_t) - 1;
        }
        if (!chunk->sections[section]) continue;
        for (y = section * MAAGE_CHUNK_EDGE; y < (section + 1) * MAAGE_CHUNK_EDGE; ++y) {
            if (token && maage_job_cancelled(token)) {
                *failure = MAAGE_STATUS_INTERRUPTED;
                return (size_t) - 1;
            }
            for (z = 0; z < MAAGE_CHUNK_EDGE; ++z)
                for (x = 0; x < MAAGE_CHUNK_EDGE; ++x) {
                    id = maage_chunk_get(chunk, x, y, z);
                    state = maage_block_registry_state(registry, id);
                    if (!state) return (size_t) - 1;
                    if (state->block_id == MAAGE_BLOCK_AIR) continue;
                    current = maage_block_registry_block(registry, state->block_id);
                    if (!current) return (size_t) - 1;
                    model = current->mesh_shape;
                    crossed = model == MAAGE_BLOCK_MESH_CROSS;
                    face_limit = crossed ? 4 : MAAGE_FACE_COUNT;
                    material = MAAGE_MESH_MATERIAL_OPAQUE;
                    if (state->fluid_kind == MAAGE_FLUID_WATER || state->block_id == MAAGE_BLOCK_ICE)
                        material = MAAGE_MESH_MATERIAL_TRANSLUCENT;
                    else if (model != MAAGE_BLOCK_MESH_CUBE || state->block_id == MAAGE_BLOCK_CACTUS ||
                             (state->block_id == MAAGE_BLOCK_LEAVES && mode != MAAGE_LEAVES_FAST))
                        material = MAAGE_MESH_MATERIAL_CUTOUT;
                    for (axis = 0; axis < 3; ++axis) {
                        shape.minimum[axis] = 0;
                        shape.maximum[axis] = 1;
                    }
                    if (current->selectable && model == MAAGE_BLOCK_MESH_CUBE) shape = current->selection_shape;
                    if (crossed) {
                        shape.minimum[0] = shape.minimum[2] = 0.05;
                        shape.maximum[0] = shape.maximum[2] = 0.95;
                    } else if (model == MAAGE_BLOCK_MESH_TORCH) {
                        shape.minimum[0] = shape.minimum[2] = 7.0 / 16.0;
                        shape.maximum[0] = shape.maximum[2] = 9.0 / 16.0;
                        shape.maximum[1] = 10.0 / 16.0;
                    } else if (state->block_id == MAAGE_BLOCK_CACTUS) {
                        shape.minimum[0] = shape.minimum[2] = 0;
                        shape.maximum[0] = shape.maximum[2] = 1;
                    }
                    if (vertices && state->fluid_kind) {
                        for (axis = 0; axis < 4; ++axis)
                            fluid_heights[axis] = maage_mesh_fluid_height(registry, chunk, neighbors,
                                                                          x + (axis & 1), y, z + (axis >> 1), state->fluid_kind);
                        maage_mesh_fluid_flow(registry, chunk, neighbors, x, y, z, state, fluid_flow);
                    }
                    above = maage_block_registry_state(registry, maage_chunk_get(chunk, x, y + 1, z));
                    snowed = above && above->block_id == MAAGE_BLOCK_SNOW;
                    colors_ready = 0;
                    log_axis = state->block_id == MAAGE_BLOCK_LOG ? ((state->metadata & 12) == 4 ? 0 : ((state->metadata & 12) == 8 ? 2 : 1)) : -1;
                    for (face = 0; face < face_limit; ++face) {
                        face_shape = shape;
                        nx = x + (crossed ? 0 : maage_face_neighbor[face][0]);
                        ny = y + (crossed ? 0 : maage_face_neighbor[face][1]);
                        nz = z + (crossed ? 0 : maage_face_neighbor[face][2]);
                        adjacent = maage_mesh_neighbor(chunk, neighbors, &nx, &nz);
                        id = adjacent && ny >= 0 && ny < MAAGE_WORLD_HEIGHT ? maage_chunk_get(adjacent, nx, ny, nz) : MAAGE_STATE_AIR;
                        neighbor = maage_block_registry_state(registry, id);
                        block = neighbor ? maage_block_registry_block(registry, neighbor->block_id) : NULL;
                        if (!block) return (size_t) - 1;
                        axis = face / 2;
                        boundary = model == MAAGE_BLOCK_MESH_CUBE &&
                                   (face & 1 ? shape.maximum[axis] == 1 : shape.minimum[axis] == 0);
                        if (state->block_id == MAAGE_BLOCK_CACTUS && axis != 1) {
                            face_shape.minimum[axis] = face_shape.maximum[axis] = face & 1 ? 0.9375 : 0.0625;
                            boundary = 0;
                        }
                        if (boundary && (block->opaque ||
                                         (mode == MAAGE_LEAVES_FAST && neighbor->block_id == MAAGE_BLOCK_LEAVES))) continue;
                        if (boundary && state->block_id == MAAGE_BLOCK_ICE && neighbor->block_id == MAAGE_BLOCK_ICE) continue;
                        if (state->block_id == MAAGE_BLOCK_LEAVES && neighbor->block_id == MAAGE_BLOCK_LEAVES && boundary) {
                            if (mode == MAAGE_LEAVES_FAST) continue;
                            if (mode == MAAGE_LEAVES_SMART && maage_mesh_smart_leaf_hidden(registry,
                                                                                           chunk, neighbors, x, y, z, face)) continue;
                        }
                        if (state->fluid_kind && state->fluid_kind == neighbor->fluid_kind) continue;
                        definition = &state->faces[crossed ? MAAGE_FACE_POSITIVE_Z : face];
                        overlay = definition->overlay_uv[2] > definition->overlay_uv[0] && !snowed;
                        reverse = state->fluid_kind == MAAGE_FLUID_LAVA && face != MAAGE_FACE_NEGATIVE_Y;
                        if (vertices) {
                            tint_mode = definition->tint_mode;
                            uv = definition->uv;
                            animation = definition->animation;
                            flow = NULL;
                            if (state->fluid_kind && definition->flow_uv[2] > definition->flow_uv[0] &&
                                (axis != 1 || (face == MAAGE_FACE_POSITIVE_Y &&
                                               (fluid_flow[0] != 0 || fluid_flow[1] != 0)))) {
                                uv = definition->flow_uv;
                                animation = definition->flow_animation;
                                flow = fluid_flow;
                            }
                            if (snowed && definition->snow_uv[2] > definition->snow_uv[0]) {
                                uv = definition->snow_uv;
                                tint_mode = MAAGE_BLOCK_TINT_NONE;
                            }
                            if (mode == MAAGE_LEAVES_FAST && state->block_id == MAAGE_BLOCK_LEAVES &&
                                state->leaf_fast_uv[2] > state->leaf_fast_uv[0]) uv = state->leaf_fast_uv;
                            if (!colors_ready && (tint_mode || overlay)) {
                                maage_mesh_biome_colors(biomes, chunk, neighbors, x, z, colors);
                                colors_ready = 1;
                            }
                            smooth = model == MAAGE_BLOCK_MESH_CUBE && !current->emitted_light &&
                                     !state->fluid_kind && state->block_id != MAAGE_BLOCK_CACTUS;
                            maage_mesh_lighting(registry, chunk, neighbors, x, y, z,
                                                crossed ? MAAGE_FACE_POSITIVE_Y : face, boundary, smooth,
                                                current->emitted_light, &illumination);
                            maage_mesh_face(vertices, indices, face_count, chunk->x * 16 + x,
                                            y, chunk->z * 16 + z, face, definition, uv, &face_shape, model,
                                            state->metadata, log_axis, material,
                                            tint_mode ? colors[tint_mode] : neutral, animation,
                                            state->fluid_kind ? fluid_heights : NULL, flow, &illumination);
                            if (overlay) maage_mesh_face(vertices, indices, face_count + 1,
                                                         chunk->x * 16 + x, y, chunk->z * 16 + z, face, definition,
                                                         definition->overlay_uv, &face_shape, model, state->metadata, -1,
                                                         MAAGE_MESH_MATERIAL_CUTOUT, colors[definition->overlay_tint_mode], NULL, NULL, NULL, &illumination);
                            if (reverse) maage_mesh_reverse_face(vertices, indices, face_count,
                                                                 face_count + 1 + (overlay ? 1 : 0));
                        }
                        if (owners) {
                            size_t owner, end = face_count + 1 + (overlay ? 1 : 0) + (reverse ? 1 : 0);
                            for (owner = face_count; owner < end; ++owner) {
                                owners[owner].x = chunk->x * 16 + x;
                                owners[owner].y = y;
                                owners[owner].z = chunk->z * 16 + z;
                            }
                        }
                        face_count += 1 + (overlay ? 1 : 0) + (reverse ? 1 : 0);
                    }
                }
        }
    }
    return face_count;
}

static maage_status maage_mesh_failure(maage_status_trace *trace, unsigned int code) {
    maage_status result = maage_status_make(code, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_MESH,
                                            code == MAAGE_STATUS_INTERRUPTED ? MAAGE_DETAIL_CANCELLED : 0);
    return MAAGE_STATUS_TRACE(trace, result, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_MESH);
}
maage_status maage_world_mesh_create_chunk_job(const maage_block_registry *blocks,
                                               const maage_biome_registry *biomes, const maage_chunk *chunk,
                                               const maage_chunk *neighbors[8], maage_leaves_mode mode,
                                               const maage_job_token *token, maage_world_mesh **output, maage_status_trace *trace) {
    maage_world_mesh *mesh;
    size_t faces, written, maximum = (size_t) - 1;
    unsigned int failure = MAAGE_STATUS_INVARIANT_VIOLATION;
    if (!output) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_MESH, 5);
    *output = NULL;
    if (mode < MAAGE_LEAVES_FAST || mode > MAAGE_LEAVES_FANCY)
        return maage_mesh_failure(trace, MAAGE_STATUS_INVALID_ARGUMENT);
    if (!blocks || !biomes || !chunk || !neighbors) {
        return MAAGE_STATUS_TRACE(trace, maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_MESH, !blocks ? 0 : (!biomes ? 1 : (!chunk ? 2 : 3))), MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_MESH);
    }
    faces = maage_mesh_generate(blocks, biomes, chunk, neighbors, NULL, NULL, NULL, mode, token, &failure);
    if (faces == maximum) return maage_mesh_failure(trace, failure);
    if (faces > UINT_MAX / 4 ||
        faces > maximum / (4 * sizeof(maage_mesh_vertex)) ||
        faces > maximum / (6 * sizeof(unsigned int)) || faces > maximum / sizeof(maage_block_position))
        return maage_mesh_failure(trace, MAAGE_STATUS_BUFFER_OVERFLOW);
    if (token && maage_job_cancelled(token)) return maage_mesh_failure(trace, MAAGE_STATUS_INTERRUPTED);
    mesh = (maage_world_mesh *) calloc(1, sizeof(*mesh));
    if (!mesh) return maage_mesh_failure(trace, MAAGE_STATUS_ALLOCATION_FAILED);
    if (faces) {
        mesh->vertices = (maage_mesh_vertex *) malloc(faces * 4 * sizeof(*mesh->vertices));
        mesh->indices = (unsigned int *) malloc(faces * 6 * sizeof(*mesh->indices));
        mesh->owners = (maage_block_position *) malloc(faces * sizeof(*mesh->owners));
        if (!mesh->vertices || !mesh->indices || !mesh->owners) {
            maage_world_mesh_destroy(mesh);
            return maage_mesh_failure(trace, MAAGE_STATUS_ALLOCATION_FAILED);
        }
        written = maage_mesh_generate(blocks, biomes, chunk, neighbors, mesh->vertices,
                                      mesh->indices, mesh->owners, mode, token, &failure);
        if (written != faces) {
            maage_world_mesh_destroy(mesh);
            return maage_mesh_failure(trace, failure);
        }
    }
    mesh->data.vertices = mesh->vertices;
    mesh->data.indices = mesh->indices;
    mesh->data.vertex_count = faces * 4;
    mesh->data.index_count = faces * 6;
    *output = mesh;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_MESH, 0);
}

const maage_mesh_data *maage_world_mesh_data(const maage_world_mesh *mesh) {
    return mesh ? &mesh->data : NULL;
}
maage_status maage_world_mesh_destroy(maage_world_mesh *mesh) {
    if (!mesh) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_DESTROY, 0);
    free(mesh->owners);
    free(mesh->indices);
    free(mesh->vertices);
    free(mesh);
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_DESTROY, 0);
}

static int maage_mesh_removed(const maage_block_position *removed, size_t count, int x, int y, int z) {
    size_t i;
    for (i = 0; i < count; ++i)
        if (removed[i].x == x && removed[i].y == y && removed[i].z == z) return 1;
    return 0;
}
maage_status maage_world_mesh_filter(const maage_world_mesh *mesh,
                                     const maage_block_position *removed, size_t count, maage_mesh_data *data, unsigned int **indices) {
    size_t face, written = 0;
    if (!mesh || !data || !indices || (count && !removed))
        return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_MESH, 0);
    *indices = NULL;
    *data = mesh->data;
    if (mesh->data.index_count) {
        *indices = (unsigned int *) malloc(mesh->data.index_count * sizeof(**indices));
        if (!*indices) return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_MESH, 0);
    }
    for (face = 0; face < mesh->data.index_count / 6; ++face) {
        if (maage_mesh_removed(removed, count, mesh->owners[face].x, mesh->owners[face].y, mesh->owners[face].z)) continue;
        memcpy(*indices + written, mesh->indices + face * 6, 6 * sizeof(**indices));
        written += 6;
    }
    data->indices = *indices;
    data->index_count = written;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_MESH, 0);
}
static int maage_reveal_index(int x, int y, int z) {
    return x + 1 + 3 * (z + 1) + 9 * (y + 1);
}
static unsigned char maage_reveal_sample(const maage_block_registry *blocks,
                                         const maage_world_neighborhood *sample, const int cell[3], int *opaque) {
    int i = maage_reveal_index(cell[0], cell[1], cell[2]);
    const maage_block_state_definition *state = maage_block_registry_state(blocks, sample->states[i]);
    const maage_block_definition *block = state ? maage_block_registry_block(blocks, state->block_id) : NULL;
    *opaque = block && block->opaque;
    return sample->light[i];
}
maage_status maage_world_mesh_reveal(const maage_block_registry *blocks,
                                     const maage_biome_registry *biomes, const maage_world_neighborhood *sample,
                                     const maage_block_position *removed, size_t count, unsigned int supplied_faces, maage_leaves_mode leaves,
                                     maage_mesh_vertex vertices[48], unsigned int indices[72], maage_mesh_data *data) {
    const maage_block_state_definition *old, *state, *above;
    const maage_block_definition *block;
    const maage_block_face_definition *definition;
    const maage_biome_definition *biome;
    const float *uv;
    maage_block_shape shape;
    maage_mesh_illumination illumination;
    float color[3], material, ao;
    unsigned char base, a, b, diagonal;
    unsigned int direction, face, corner, channel, layer;
    size_t faces = 0;
    int x, y, z, axis, u, v, i, ca, cb, cc, cd, bx, bz, snowed;
    unsigned int sky = 0, emission = 0, neighbor_light;
    maage_world_neighborhood visible;
    int center[3] = {0, 0, 0}, first[3], second[3], last[3];
    if (!blocks || !biomes || !sample || !sample->valid || !data || !vertices || !indices || (count && !removed))
        return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_MESH, 0);
    data->vertices = vertices;
    data->indices = indices;
    data->vertex_count = data->index_count = 0;
    old = maage_block_registry_state(blocks, sample->states[13]);
    block = old ? maage_block_registry_block(blocks, old->block_id) : NULL;
    if (!block || !block->opaque) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_MESH, 0);
    visible = *sample;
    for (i = 0; i < 27; ++i)
        if (i != 13 && maage_mesh_removed(removed, count,
                                          sample->x + i % 3 - 1, sample->y + i / 9 - 1, sample->z + (i / 3) % 3 - 1)) visible.states[i] = MAAGE_STATE_AIR;
    sample = &visible;
    for (direction = 0; direction < 6; ++direction) {
        i = maage_reveal_index(maage_face_neighbor[direction][0], maage_face_neighbor[direction][1], maage_face_neighbor[direction][2]);
        neighbor_light = sample->light[i] >> 4;
        if (neighbor_light && direction != MAAGE_FACE_POSITIVE_Y) --neighbor_light;
        if (neighbor_light > sky) sky = neighbor_light;
        neighbor_light = sample->light[i] & 15;
        if (neighbor_light) --neighbor_light;
        if (neighbor_light > emission) emission = neighbor_light;
    }
    visible.light[13] = (unsigned char) (sky * 16 + emission);
    for (direction = 0; direction < 6; ++direction) {
        x = maage_face_neighbor[direction][0];
        y = maage_face_neighbor[direction][1];
        z = maage_face_neighbor[direction][2];
        if ((supplied_faces & (1U << direction)) ||
            maage_mesh_removed(removed, count, sample->x + x, sample->y + y, sample->z + z)) continue;
        state = maage_block_registry_state(blocks, sample->states[maage_reveal_index(x, y, z)]);
        block = state ? maage_block_registry_block(blocks, state->block_id) : NULL;
        if (!block || !block->solid || block->mesh_shape != MAAGE_BLOCK_MESH_CUBE || state->fluid_kind) continue;
        face = direction ^ 1U;
        axis = face / 2;
        u = (axis + 1) % 3;
        v = (axis + 2) % 3;
        if ((face & 1) ? block->selection_shape.maximum[axis] != 1 : block->selection_shape.minimum[axis] != 0) continue;
        definition = &state->faces[face];
        shape = block->selection_shape;
        if (block->id == MAAGE_BLOCK_CACTUS && axis != 1)
            shape.minimum[axis] = shape.maximum[axis] = face & 1 ? 0.9375 : 0.0625;
        above = y < 1 ? maage_block_registry_state(blocks, sample->states[maage_reveal_index(x, y + 1, z)]) : NULL;
        snowed = above && above->block_id == MAAGE_BLOCK_SNOW;
        base = maage_reveal_sample(blocks, sample, center, &ca);
        if ((base & 15) < block->emitted_light) base = (unsigned char) ((base & 240) | block->emitted_light);
        for (corner = 0; corner < 4; ++corner) {
            a = b = diagonal = base;
            ao = 1;
            if (!block->emitted_light && block->id != MAAGE_BLOCK_CACTUS) {
                memcpy(first, center, sizeof(first));
                memcpy(second, center, sizeof(second));
                memcpy(last, center, sizeof(last));
                first[u] = maage_face_corners[face][corner][u] ? 1 : -1;
                second[v] = maage_face_corners[face][corner][v] ? 1 : -1;
                last[u] = first[u];
                last[v] = second[v];
                a = maage_reveal_sample(blocks, sample, first, &cb);
                b = maage_reveal_sample(blocks, sample, second, &cc);
                if (cb && cc) {
                    diagonal = a;
                    cd = 1;
                } else
                    diagonal = maage_reveal_sample(blocks, sample, last, &cd);
                /* The predicted center is air; surrounding AO stays sampled. */
                ao = 1 - 0.2f * (cb + cc + cd);
                if (!a) a = base;
                if (!b) b = base;
                if (!diagonal) diagonal = base;
            }
            illumination.light[corner][0] = ((base >> 4) + (a >> 4) + (b >> 4) + (diagonal >> 4)) / 60.0f;
            illumination.light[corner][1] = ((base & 15) + (a & 15) + (b & 15) + (diagonal & 15)) / 60.0f;
            illumination.occlusion[corner] = ao;
        }
        material = MAAGE_MESH_MATERIAL_OPAQUE;
        if (block->id == MAAGE_BLOCK_CACTUS || (block->id == MAAGE_BLOCK_LEAVES && leaves != MAAGE_LEAVES_FAST))
            material = MAAGE_MESH_MATERIAL_CUTOUT;
        if (block->id == MAAGE_BLOCK_ICE) material = MAAGE_MESH_MATERIAL_TRANSLUCENT;
        for (layer = 0; layer < 2; ++layer) {
            unsigned int tint = layer ? definition->overlay_tint_mode : definition->tint_mode;
            if (layer && (snowed || definition->overlay_uv[2] <= definition->overlay_uv[0])) continue;
            uv = layer ? definition->overlay_uv : definition->uv;
            if (!layer && snowed && definition->snow_uv[2] > definition->snow_uv[0]) {
                uv = definition->snow_uv;
                tint = MAAGE_BLOCK_TINT_NONE;
            }
            if (!layer && block->id == MAAGE_BLOCK_LEAVES && leaves == MAAGE_LEAVES_FAST &&
                state->leaf_fast_uv[2] > state->leaf_fast_uv[0]) uv = state->leaf_fast_uv;
            for (channel = 0; channel < 3; ++channel)
                color[channel] = tint ? 0 : 1;
            if (tint == MAAGE_BLOCK_TINT_GRASS_LOCAL) {
                biome = maage_biome_registry_get(biomes, sample->biomes[x + 2 + 5 * (z + 2)]);
                for (channel = 0; channel < 3; ++channel)
                    color[channel] = biome ? biome->grass_color[channel] : 1;
            } else if (tint)
                for (bz = -1; bz <= 1; ++bz)
                    for (bx = -1; bx <= 1; ++bx) {
                        biome = maage_biome_registry_get(biomes, sample->biomes[x + bx + 2 + 5 * (z + bz + 2)]);
                        for (channel = 0; channel < 3; ++channel)
                            color[channel] += !biome ? 1.0f / 9 : (tint == MAAGE_BLOCK_TINT_FOLIAGE ? biome->foliage_color[channel] : biome->grass_color[channel]) / 9;
                    }
            maage_mesh_face(vertices, indices, faces, sample->x + x, sample->y + y, sample->z + z, face,
                            definition, uv, &shape,
                            MAAGE_BLOCK_MESH_CUBE, state->metadata,
                            state->block_id == MAAGE_BLOCK_LOG ? ((state->metadata & 12) == 4 ? 0 : ((state->metadata & 12) == 8 ? 2 : 1)) : -1,
                            layer ? MAAGE_MESH_MATERIAL_CUTOUT : material, color, definition->animation, NULL, NULL, &illumination);
            ++faces;
        }
    }
    data->vertex_count = faces * 4;
    data->index_count = faces * 6;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_MESH, 0);
}
