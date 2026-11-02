#include "world_internal.h"
#include "maage/jobs.h"
#include "maage/block_geometry.h"
#include <limits.h>
#include <stddef.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct maage_mesh_owner {
    unsigned short minimum, maximum; /* Inclusive owner-cell box, within one section. */
} maage_mesh_owner;

struct maage_world_mesh {
    maage_mesh_data data;
    maage_mesh_preparation *prepared;
    maage_mesh_vertex *vertices;
    unsigned int *indices;
    maage_mesh_owner *owners;
    unsigned char *face_flags; /* Material category and quad winding per face. */
    int chunk_x, chunk_z;
    size_t vertex_bytes, index_bytes, retained_bytes;
};

/* Each generated face has four private vertices; all four quad windings
 * produced by the regular and reverse-face paths fit into two bits. */
static const unsigned char maage_mesh_quad_indices[4][6] = {
    {0, 1, 2, 0, 2, 3},
    {0, 1, 3, 1, 2, 3},
    {0, 2, 1, 0, 3, 2},
    {0, 3, 1, 1, 3, 2}};
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
static const maage_chunk *maage_mesh_neighbor(const maage_chunk *chunk, const maage_chunk *neighbors[8], int *x, int *z) {
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

static unsigned char maage_mesh_sample(const maage_block_registry *registry, const maage_chunk *chunk, const maage_chunk *neighbors[8], int x, int y, int z, unsigned char fallback, int *occludes) {
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
    return maage_chunk_presentation_light(sample, registry, x, y, z);
}

static void maage_mesh_lighting(const maage_block_registry *registry, const maage_chunk *chunk, const maage_chunk *neighbors[8], int x, int y, int z, unsigned int face, int outward, int smooth, unsigned int emission, maage_mesh_illumination *output) {
    unsigned char base, a, b, diagonal, own;
    int n[3], first[3], second[3], corner[3], axis, u, v, i, j;
    int center_opaque, side_a, side_b, diagonal_opaque;
    float ao;
    own = maage_chunk_presentation_light(chunk, registry, x, y, z);
    n[0] = x;
    n[1] = y;
    n[2] = z;
    if (outward)
        for (i = 0; i < 3; ++i)
            n[i] += maage_face_neighbor[face][i];
    base = maage_mesh_sample(registry, chunk, neighbors, n[0], n[1], n[2], own, &center_opaque);
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
            a = maage_mesh_sample(registry, chunk, neighbors, first[0], first[1], first[2], base, &side_a);
            b = maage_mesh_sample(registry, chunk, neighbors, second[0], second[1], second[2], base, &side_b);
            if (side_a && side_b) {
                diagonal = a;
                diagonal_opaque = 1;
            } else
                diagonal = maage_mesh_sample(registry, chunk, neighbors, corner[0], corner[1], corner[2], base, &diagonal_opaque);
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

/* Fluid brightness takes the brighter channel from a cell and the one above,
 * independently of corner AO. This also lights top faces beneath opaque roofs. */
static void maage_mesh_fluid_lighting(const maage_block_registry *registry, const maage_chunk *chunk, const maage_chunk *neighbors[8], int x, int y, int z, unsigned int face, unsigned int emission, maage_mesh_illumination *output) {
    unsigned char own, base, above;
    unsigned int sky, block, corner;
    int opaque, sx = x, sy = y, sz = z;
    own = maage_chunk_presentation_light(chunk, registry, x, y, z);
    if (face != MAAGE_FACE_POSITIVE_Y) {
        sx += maage_face_neighbor[face][0];
        sy += maage_face_neighbor[face][1];
        sz += maage_face_neighbor[face][2];
    }
    base = maage_mesh_sample(registry, chunk, neighbors, sx, sy, sz, own, &opaque);
    above = maage_mesh_sample(registry, chunk, neighbors, sx, sy + 1, sz, base, &opaque);
    sky = (base >> 4) > (above >> 4) ? base >> 4 : above >> 4;
    block = (base & 15U) > (above & 15U) ? base & 15U : above & 15U;
    if (block < emission) block = emission;
    for (corner = 0; corner < 4; ++corner) {
        output->light[corner][0] = sky / 15.0f;
        output->light[corner][1] = block / 15.0f;
        output->occlusion[corner] = 1;
    }
}

/* A five-cell halo covers the largest box plus corner interpolation. Prefix
 * sums reuse immutable samples across every height/face; missing columns carry
 * zero weight, never an invented center-biome identity. */
typedef struct maage_mesh_biome_cache {
    double sums[27][27][7]; /* grass RGB, foliage RGB, available sample count */
    float colors[17][17][4][3];
    unsigned char ready[17][17];
    int initialized, halo;
} maage_mesh_biome_cache;

static void maage_mesh_biome_cache_init(maage_mesh_biome_cache *cache, const maage_biome_registry *biomes, const maage_chunk *chunk, const maage_chunk *neighbors[8], maage_biome_blend blend) {
    const maage_chunk *sample;
    const maage_biome_definition *biome;
    double value;
    int x, z, sx, sz, channel, edge;
    memset(cache, 0, sizeof(*cache));
    cache->halo = (int) blend + (blend >= MAAGE_BIOME_BLEND_SMOOTH_5 ? 1 : 0);
    edge = 16 + cache->halo * 2;
    for (z = 0; z < edge; ++z)
        for (x = 0; x < edge; ++x) {
            sx = x - cache->halo;
            sz = z - cache->halo;
            sample = maage_mesh_neighbor(chunk, neighbors, &sx, &sz);
            biome = sample ? maage_biome_registry_get(biomes, sample->biomes[sz * 16 + sx]) : NULL;
            for (channel = 0; channel < 7; ++channel) {
                value = !biome ? 0 : (channel == 6 ? 1 : (channel < 3 ? biome->grass_color[channel] : biome->foliage_color[channel - 3]));
                cache->sums[z + 1][x + 1][channel] = value + cache->sums[z + 1][x][channel] + cache->sums[z][x + 1][channel] - cache->sums[z][x][channel];
            }
        }
    cache->initialized = 1;
}

static const float (*maage_mesh_biome_colors(const maage_biome_registry *biomes, const maage_chunk *chunk, const maage_chunk *neighbors[8], maage_biome_blend blend, maage_mesh_biome_cache *cache, int x, int z))[3] {
    const maage_biome_definition *biome;
    double sums[7] = {0, 0, 0, 0, 0, 0, 0};
    int pass, channel, radius = (int) blend, smooth = blend >= MAAGE_BIOME_BLEND_SMOOTH_5;
    int x0, z0, x1, z1, cx, cz;
    float (*colors)[3];
    if (!cache->initialized) maage_mesh_biome_cache_init(cache, biomes, chunk, neighbors, blend);
    colors = cache->colors[z][x];
    if (cache->ready[z][x]) return (const float (*)[3]) colors;
    /* Smooth vertices average the four adjacent block-centered box filters.
     * The world-space corner and its sample set are identical on either side
     * of a block/chunk boundary. Count weights also interpolate availability. */
    for (pass = 0; pass < (smooth ? 4 : 1); ++pass) {
        cx = x - (smooth ? pass & 1 : 0);
        cz = z - (smooth ? pass >> 1 : 0);
        x0 = cx - radius + cache->halo;
        z0 = cz - radius + cache->halo;
        x1 = cx + radius + cache->halo + 1;
        z1 = cz + radius + cache->halo + 1;
        for (channel = 0; channel < 7; ++channel)
            sums[channel] += cache->sums[z1][x1][channel] - cache->sums[z0][x1][channel] - cache->sums[z1][x0][channel] + cache->sums[z0][x0][channel];
    }
    biome = !smooth ? maage_biome_registry_get(biomes, chunk->biomes[z * 16 + x]) : NULL;
    for (channel = 0; channel < 3; ++channel) {
        colors[MAAGE_BLOCK_TINT_NONE][channel] = 1;
        colors[MAAGE_BLOCK_TINT_GRASS][channel] = sums[6] > 0 ? (float) (sums[channel] / sums[6]) : 1;
        colors[MAAGE_BLOCK_TINT_FOLIAGE][channel] = sums[6] > 0 ? (float) (sums[channel + 3] / sums[6]) : 1;
        if (blend == MAAGE_BIOME_BLEND_VANILLA) {
            colors[MAAGE_BLOCK_TINT_GRASS][channel] = (float) (floor(colors[MAAGE_BLOCK_TINT_GRASS][channel] * 255 + 0.0001) / 255);
            colors[MAAGE_BLOCK_TINT_FOLIAGE][channel] = (float) (floor(colors[MAAGE_BLOCK_TINT_FOLIAGE][channel] * 255 + 0.0001) / 255);
        }
        colors[MAAGE_BLOCK_TINT_GRASS_LOCAL][channel] = smooth ? colors[MAAGE_BLOCK_TINT_GRASS][channel] : (biome ? biome->grass_color[channel] : 1);
    }
    cache->ready[z][x] = 1;
    return (const float (*)[3]) colors;
}

/* Shared corner heights depend on the same four cells in either adjoining
 * column. Source/falling cells anchor the surface; open space lowers edges,
 * and a fluid above any contributor fills the corner to the next voxel. */
static float maage_mesh_fluid_height(const maage_block_registry *registry, const maage_chunk *chunk, const maage_chunk *neighbors[8], int x, int y, int z, maage_fluid_id kind) {
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
                level = state->fluid_level >= state->fluid_definition->flow_levels ? 0 : state->fluid_level;
                weight = !level ? 11U : 1U;
                depth += weight * (level + 1) / (float) (state->fluid_definition->flow_levels + 1U);
                total += weight;
            } else {
                block = state ? maage_block_registry_block(registry, state->block_id) : NULL;
                if (block && !block->material_solid) {
                    depth += 1;
                    ++total;
                }
            }
        }
    return total ? 1.0f - depth / total : 1.0f;
}

static void maage_mesh_fluid_flow(const maage_block_registry *registry, const maage_chunk *chunk, const maage_chunk *neighbors[8], int x, int y, int z, const maage_block_state_definition *current, float flow[2]) {
    static const int dx[4] = {-1, 1, 0, 0}, dz[4] = {0, 0, -1, 1};
    const maage_chunk *sample;
    const maage_block_state_definition *sides[4], *below[4], *above[4];
    int side, sx, sz;
    double vector[3], length;
    for (side = 0; side < 4; ++side) {
        sx = x + dx[side];
        sz = z + dz[side];
        sample = maage_mesh_neighbor(chunk, neighbors, &sx, &sz);
        sides[side] = sample ? maage_block_registry_state(registry, maage_chunk_get(sample, sx, y, sz)) : NULL;
        below[side] = sample ? maage_block_registry_state(registry, maage_chunk_get(sample, sx, y - 1, sz)) : NULL;
        above[side] = sample ? maage_block_registry_state(registry, maage_chunk_get(sample, sx, y + 1, sz)) : NULL;
    }
    maage_block_fluid_flow(registry, current, sides, below, above, vector);
    length = sqrt(vector[0] * vector[0] + vector[2] * vector[2]);
    flow[0] = length > 0 ? (float) (vector[0] / length) : 0;
    flow[1] = length > 0 ? (float) (vector[2] / length) : 0;
}

static void maage_mesh_face(maage_mesh_vertex *vertices, unsigned int *indices, size_t face_index, int x, int y, int z, unsigned int face, const maage_block_face_definition *definition, const float uv[4], const maage_block_shape *shape, unsigned int model, unsigned int metadata, int log_axis, float material, float tint[4][3], int smooth_tint, const float animation[3], const float fluid_heights[4], const float fluid_flow[2], const maage_mesh_illumination *illumination) {
    static const unsigned int offsets[2][6] = {{0, 1, 2, 0, 2, 3}, {0, 1, 3, 1, 2, 3}};
    static const float shade[MAAGE_FACE_COUNT] = {0.6f, 0.6f, 0.5f, 1.0f, 0.8f, 0.8f};
    static const int cross_corners[4][4][3] = {
        {{0, 0, 0}, {1, 0, 1}, {1, 1, 1}, {0, 1, 0}},
        {{1, 0, 1}, {0, 0, 0}, {0, 1, 0}, {1, 1, 1}},
        {{1, 0, 0}, {0, 0, 1}, {0, 1, 1}, {1, 1, 0}},
        {{0, 0, 1}, {1, 0, 0}, {1, 1, 0}, {0, 1, 1}}};
    maage_mesh_vertex *vertex;
    unsigned int base, corner, component, alternate, source;
    const int *point;
    float brightness, local[3], u, v, ao, weights, light[2], t, shear, color;
    int axis, first, second;
    brightness = model == MAAGE_BLOCK_MESH_CUBE ? shade[face] : 1.0f;
    base = (unsigned int) (face_index * 4);
    axis = face / 2;
    first = (axis + 1) % 3;
    second = (axis + 2) % 3;
    for (corner = 0; corner < 4; ++corner) {
        vertex = &vertices[face_index * 4 + corner];
        memset(vertex->tile, 0, sizeof(vertex->tile));
        point = model == MAAGE_BLOCK_MESH_CROSS ? cross_corners[face][corner] : maage_face_corners[face][corner];
        for (component = 0; component < 3; ++component)
            local[component] = (float) (shape->minimum[component] + point[component] * (shape->maximum[component] - shape->minimum[component]));
        if (fluid_heights && point[1]) local[1] = fluid_heights[point[0] + 2 * point[2]];
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
                    u = 0.5f + (2 * local[0] - 1) * 0.25f * fluid_flow[1] - (2 * local[2] - 1) * 0.25f * fluid_flow[0];
                    v = 0.5f + (2 * local[2] - 1) * 0.25f * fluid_flow[1] + (2 * local[0] - 1) * 0.25f * fluid_flow[0];
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
        vertex->uv[0] = uv[0] + u * (uv[2] - uv[0]);
        vertex->uv[1] = uv[1] + v * (uv[3] - uv[1]);
        for (component = 0; component < 3; ++component) {
            color = tint[0][component];
            if (smooth_tint) color = tint[0][component] * (1 - local[0]) * (1 - local[2]) + tint[1][component] * local[0] * (1 - local[2]) + tint[2][component] * (1 - local[0]) * local[2] + tint[3][component] * local[0] * local[2];
            vertex->color[component] = definition->tint[component] * color * brightness * ao;
            vertex->animation[component] = animation ? animation[component] : 0;
        }
        vertex->color[3] = definition->tint[3];
        vertex->light[0] = light[0];
        vertex->light[1] = light[1];
        vertex->material = material;
    }
    alternate = illumination->occlusion[0] + illumination->occlusion[2] > illumination->occlusion[1] + illumination->occlusion[3];
    for (corner = 0; corner < 6; ++corner)
        indices[face_index * 6 + corner] = base + offsets[alternate][corner];
}

static void maage_mesh_reverse_face(maage_mesh_vertex *vertices, unsigned int *indices, size_t source, size_t destination) {
    unsigned int corner, offset;
    for (corner = 0; corner < 4; ++corner) {
        vertices[destination * 4 + corner] = vertices[source * 4 + corner];
    }
    for (corner = 0; corner < 6; ++corner) {
        offset = corner % 3 == 1 ? corner + 1 : (corner % 3 == 2 ? corner - 1 : corner);
        indices[destination * 6 + corner] = (unsigned int) (destination * 4) + indices[source * 6 + offset] - (unsigned int) (source * 4);
    }
}

/* Preserve exposed foliage and two interior cutout planes per direction. */
static int maage_mesh_smart_leaf_hidden(const maage_block_registry *registry, const maage_chunk *chunk, const maage_chunk *neighbors[8], int x, int y, int z, unsigned int face) {
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
        if (!state || !maage_block_registry_block(registry, state->block_id)->foliage) return 0;
    }
    return 1;
}

typedef struct maage_mesh_custom_work {
    const maage_block_registry *blocks;
    const maage_biome_registry *biomes;
    const maage_chunk *chunk;
    const maage_chunk **neighbors;
    const maage_block_definition *block;
    const maage_block_state_definition *state;
    maage_mesh_biome_cache *cache;
    const maage_job_token *token;
    maage_mesh_vertex *vertices;
    unsigned int *indices;
    maage_mesh_owner *owners;
    size_t count, capacity;
    int x, y, z;
    unsigned int face_mask;
    maage_leaves_mode leaves;
    maage_biome_blend blend;
    maage_status result;
} maage_mesh_custom_work;
static const maage_block_state_definition *maage_mesh_custom_sample(const void *opaque, int dx, int dy, int dz) {
    const maage_mesh_custom_work *work = (const maage_mesh_custom_work *) opaque;
    const maage_chunk *sample;
    int x, z, y;
    /* The job pins only the owning column and its immediate immutable halo. */
    if (dx < -1 || dx > 1 || dz < -1 || dz > 1 || dy < -1 || dy > 1) return NULL;
    x = work->x + dx;
    y = work->y + dy;
    z = work->z + dz;
    if (y < 0 || y >= MAAGE_WORLD_HEIGHT) return NULL;
    sample = maage_mesh_neighbor(work->chunk, work->neighbors, &x, &z);
    return sample ? maage_block_registry_state(work->blocks, maage_chunk_get(sample, x, y, z)) : NULL;
}
static maage_status maage_mesh_custom_quad_write(maage_mesh_custom_work *work, const maage_block_geometry_quad *quad) {
    static const unsigned int winding[6] = {0, 1, 2, 0, 2, 3};
    static const float shade[6] = {0.6f, 0.6f, 0.5f, 1, 0.8f, 0.8f};
    const maage_block_face_definition *appearance;
    const maage_block_state_definition *neighbor;
    const maage_block_definition *neighbor_block;
    maage_mesh_illumination illumination;
    const float (*colors)[3];
    const float *uv;
    maage_mesh_vertex *vertex;
    maage_status result;
    size_t face, amount;
    unsigned int corner, axis, base, layer, layers, tint_mode;
    int outward, snowed;
    float material, u, v, light[2], ao, weight, bias, tint[4][3];
    if (work->token && maage_job_cancelled(work->token)) return maage_status_make(MAAGE_STATUS_INTERRUPTED, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_MESH, MAAGE_DETAIL_CANCELLED);
    result = maage_block_geometry_quad_validate(quad);
    if (maage_status_failed(result)) return result;
    if (!(work->face_mask & (1U << quad->face))) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_MESH, 0);
    appearance = quad->appearance ? quad->appearance : &work->state->faces[quad->face];
    if (appearance->tint_mode > MAAGE_BLOCK_TINT_GRASS_LOCAL || appearance->overlay_tint_mode > MAAGE_BLOCK_TINT_GRASS_LOCAL) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_MESH, 0);
    if (quad->cull) {
        neighbor = maage_mesh_custom_sample(work, maage_face_neighbor[quad->face][0], maage_face_neighbor[quad->face][1], maage_face_neighbor[quad->face][2]);
        neighbor_block = neighbor ? maage_block_registry_block(work->blocks, neighbor->block_id) : NULL;
        if (neighbor_block && (neighbor_block->opaque || (work->leaves == MAAGE_LEAVES_FAST && neighbor_block->foliage) || (work->block->cull_same && neighbor->block_id == work->state->block_id))) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_MESH, 0);
        if (neighbor_block && work->block->foliage && neighbor_block->foliage && work->leaves == MAAGE_LEAVES_SMART && maage_mesh_smart_leaf_hidden(work->blocks, work->chunk, work->neighbors, work->x, work->y, work->z, quad->face)) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_MESH, 0);
    }
    neighbor = maage_mesh_custom_sample(work, 0, 1, 0);
    neighbor_block = neighbor ? maage_block_registry_block(work->blocks, neighbor->block_id) : NULL;
    snowed = neighbor_block && neighbor_block->snow_cover;
    layers = appearance->overlay_uv[2] > appearance->overlay_uv[0] && !snowed ? 2U : 1U;
    amount = layers * (quad->double_sided ? 2U : 1U);
    if (work->count > (size_t) - 1 - amount || work->count > UINT_MAX / 4U - amount || (work->vertices && (work->count > work->capacity || amount > work->capacity - work->count))) return maage_status_make(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_MESH, 0);
    face = work->count;
    work->count += amount;
    if (work->vertices) {
        outward = 1;
        for (corner = 0; corner < 4; ++corner)
            if (quad->positions[corner][quad->face / 2] != (float) (quad->face & 1U)) outward = 0;
        maage_mesh_lighting(work->blocks, work->chunk, work->neighbors, work->x, work->y, work->z, quad->face, outward || work->block->use_neighbor_brightness, !work->block->emitted_light && !work->state->fluid_kind, work->block->emitted_light, &illumination);
        for (layer = 0; layer < layers; ++layer) {
            uv = layer ? appearance->overlay_uv : appearance->uv;
            tint_mode = layer ? appearance->overlay_tint_mode : appearance->tint_mode;
            if (!layer && snowed && appearance->snow_uv[2] > appearance->snow_uv[0]) {
                uv = appearance->snow_uv;
                tint_mode = MAAGE_BLOCK_TINT_NONE;
            }
            for (corner = 0; corner < 4; ++corner) {
                colors = tint_mode ? maage_mesh_biome_colors(work->biomes, work->chunk, work->neighbors, work->blend, work->cache, work->x + (work->blend >= MAAGE_BIOME_BLEND_SMOOTH_5 ? corner & 1U : 0), work->z + (work->blend >= MAAGE_BIOME_BLEND_SMOOTH_5 ? corner >> 1 : 0)) : NULL;
                for (axis = 0; axis < 3; ++axis)
                    tint[corner][axis] = colors ? colors[tint_mode][axis] : 1;
            }
            material = quad->material == MAAGE_BLOCK_GEOMETRY_INHERIT_MATERIAL ? (float) work->block->mesh_material : (float) quad->material;
            if (layer) material = MAAGE_MESH_MATERIAL_CUTOUT;
            else if (work->block->foliage && quad->material == MAAGE_BLOCK_GEOMETRY_INHERIT_MATERIAL) material = work->leaves == MAAGE_LEAVES_FAST ? MAAGE_MESH_MATERIAL_OPAQUE : MAAGE_MESH_MATERIAL_CUTOUT;
            bias = layer ? (quad->face & 1U ? 0.0002f : -0.0002f) : 0;
            base = (unsigned int) (face * 4);
            for (corner = 0; corner < 4; ++corner) {
                vertex = &work->vertices[face * 4 + corner];
                memset(vertex, 0, sizeof(*vertex));
                for (axis = 0; axis < 3; ++axis)
                    vertex->position[axis] = quad->positions[corner][axis] + (float) (axis == 0 ? work->chunk->x * 16 + work->x : (axis == 1 ? work->y : work->chunk->z * 16 + work->z)) + (axis == quad->face / 2 ? bias : 0);
                vertex->uv[0] = uv[0] + quad->uv[corner][0] * (uv[2] - uv[0]);
                vertex->uv[1] = uv[1] + quad->uv[corner][1] * (uv[3] - uv[1]);
                light[0] = light[1] = ao = 0;
                for (axis = 0; axis < 4; ++axis) {
                    u = quad->positions[corner][(quad->face / 2 + 1) % 3];
                    v = quad->positions[corner][(quad->face / 2 + 2) % 3];
                    weight = maage_face_corners[quad->face][axis][(quad->face / 2 + 1) % 3] ? u : 1 - u;
                    weight *= maage_face_corners[quad->face][axis][(quad->face / 2 + 2) % 3] ? v : 1 - v;
                    light[0] += illumination.light[axis][0] * weight;
                    light[1] += illumination.light[axis][1] * weight;
                    ao += illumination.occlusion[axis] * weight;
                }
                for (axis = 0; axis < 3; ++axis) {
                    u = quad->positions[corner][0];
                    v = quad->positions[corner][2];
                    vertex->color[axis] = appearance->tint[axis] * shade[quad->face] * ao * (tint[0][axis] * (1 - u) * (1 - v) + tint[1][axis] * u * (1 - v) + tint[2][axis] * (1 - u) * v + tint[3][axis] * u * v);
                    if (!layer) vertex->animation[axis] = appearance->animation[axis];
                }
                vertex->color[3] = appearance->tint[3];
                vertex->light[0] = light[0];
                vertex->light[1] = light[1];
                vertex->material = material;
            }
            for (corner = 0; corner < 6; ++corner)
                work->indices[face * 6 + corner] = base + winding[corner];
            if (quad->double_sided) maage_mesh_reverse_face(work->vertices, work->indices, face, face + 1);
            face += quad->double_sided ? 2U : 1U;
        }
    }
    if (work->owners)
        for (axis = 0; axis < amount; ++axis)
            work->owners[work->count - amount + axis].minimum = work->owners[work->count - amount + axis].maximum = (unsigned short) (work->y * 256 + work->z * 16 + work->x);
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_MESH, 0);
}
static maage_status maage_mesh_custom_quad(void *opaque, const maage_block_geometry_quad *quad) {
    maage_mesh_custom_work *work = (maage_mesh_custom_work *) opaque;
    maage_status result;
    if (maage_status_failed(work->result)) return work->result;
    result = maage_mesh_custom_quad_write(work, quad);
    if (maage_status_failed(result)) work->result = result;
    return result;
}

static size_t maage_mesh_generate(const maage_block_registry *registry, const maage_biome_registry *biomes, const maage_chunk *chunk, const maage_chunk *neighbors[8], maage_mesh_vertex *vertices, unsigned int *indices, maage_mesh_owner *owners, maage_leaves_mode mode, maage_biome_blend blend, maage_mesh_biome_cache *cache, const maage_job_token *token, const maage_block_position *cell, unsigned int face_mask, size_t face_capacity, maage_status *failure) {
    const maage_registry_bindings *ids = maage_block_registry_bindings(registry);
    const maage_block_geometry_definition *geometry;
    maage_mesh_custom_work custom;
    maage_block_geometry_query query;
    maage_block_geometry_sink sink;
    maage_status custom_result;
    const maage_block_state_definition *state, *neighbor, *above;
    const maage_block_definition *block, *current;
    const maage_block_face_definition *definition;
    const float *uv, *animation, *flow;
    const float (*sample_colors)[3];
    maage_block_shape shape, face_shape;
    maage_mesh_illumination illumination;
    const maage_chunk *adjacent;
    maage_block_state_id id;
    float colors[4][4][3], fluid_heights[4], fluid_flow[2], neutral[4][3] = {{1, 1, 1}, {1, 1, 1}, {1, 1, 1}, {1, 1, 1}}, material;
    int x, y, z, section, nx, ny, nz, log_axis;
    unsigned int face, axis, face_limit, model, tint_mode, corner, tint, component;
    int crossed, boundary, snowed, colors_ready, overlay, smooth, reverse;
    size_t face_count = 0;
    for (section = cell ? cell->y / 16 : 0; section < (cell ? cell->y / 16 + 1 : 16); ++section) {
        if (token && maage_job_cancelled(token)) {
            *failure = MAAGE_STATUS_INTERRUPTED;
            return (size_t) - 1;
        }
        if (!chunk->sections[section]) continue;
        for (y = cell ? cell->y : section * MAAGE_CHUNK_EDGE; y < (cell ? cell->y + 1 : (section + 1) * MAAGE_CHUNK_EDGE); ++y) {
            if (token && maage_job_cancelled(token)) {
                *failure = MAAGE_STATUS_INTERRUPTED;
                return (size_t) - 1;
            }
            for (z = cell ? cell->z : 0; z < (cell ? cell->z + 1 : MAAGE_CHUNK_EDGE); ++z)
                for (x = cell ? cell->x : 0; x < (cell ? cell->x + 1 : MAAGE_CHUNK_EDGE); ++x) {
                    id = maage_chunk_get(chunk, x, y, z);
                    state = maage_block_registry_state(registry, id);
                    if (!state) return (size_t) - 1;
                    if (state->block_id == ids->block_air) continue;
                    current = maage_block_registry_block(registry, state->block_id);
                    if (!current) return (size_t) - 1;
                    model = current->mesh_shape;
                    if (model == MAAGE_BLOCK_GEOMETRY_CUSTOM) {
                        geometry = maage_block_geometry_get(maage_block_registry_geometry(registry), current->geometry_id);
                        if (!geometry || !geometry->emit) return (size_t) - 1;
                        memset(&custom, 0, sizeof(custom));
                        custom.blocks = registry;
                        custom.biomes = biomes;
                        custom.chunk = chunk;
                        custom.neighbors = neighbors;
                        custom.block = current;
                        custom.state = state;
                        custom.cache = cache;
                        custom.token = token;
                        custom.vertices = vertices;
                        custom.indices = indices;
                        custom.owners = owners;
                        custom.count = face_count;
                        custom.capacity = face_capacity;
                        custom.x = x;
                        custom.y = y;
                        custom.z = z;
                        custom.face_mask = face_mask;
                        custom.leaves = mode;
                        custom.blend = blend;
                        memset(&query, 0, sizeof(query));
                        query.blocks = registry;
                        query.block = current;
                        query.state = state;
                        query.x = chunk->x * 16 + x;
                        query.y = y;
                        query.z = chunk->z * 16 + z;
                        query.sample = maage_mesh_custom_sample;
                        query.sample_context = &custom;
                        sink.quad = maage_mesh_custom_quad;
                        sink.context = &custom;
                        custom_result = geometry->emit(geometry->context, &query, &sink);
                        if (maage_status_failed(custom.result)) custom_result = custom.result;
                        if (maage_status_failed(custom_result)) {
                            *failure = custom_result;
                            return (size_t) - 1;
                        }
                        face_count = custom.count;
                        continue;
                    }
                    if (model > MAAGE_BLOCK_MESH_TORCH) return (size_t) - 1;
                    crossed = model == MAAGE_BLOCK_MESH_CROSS;
                    face_limit = crossed ? 4 : MAAGE_FACE_COUNT;
                    material = current->mesh_material;
                    if (current->foliage) material = mode == MAAGE_LEAVES_FAST ? MAAGE_MESH_MATERIAL_OPAQUE : MAAGE_MESH_MATERIAL_CUTOUT;
                    for (axis = 0; axis < 3; ++axis) {
                        shape.minimum[axis] = 0;
                        shape.maximum[axis] = 1;
                    }
                    if (current->selectable && model == MAAGE_BLOCK_MESH_CUBE) shape = current->selection_shape;
                    if (crossed) {
                        shape.minimum[0] = shape.minimum[2] = 0.05;
                        shape.maximum[0] = shape.maximum[2] = 0.95;
                        /* Anchor crossed plants to a lowered solid substrate.
                         * Full-height quads retain each texture's authored
                         * transparent stage height without stretching it. */
                        neighbor = maage_block_registry_state(registry, maage_chunk_get(chunk, x, y - 1, z));
                        block = neighbor ? maage_block_registry_block(registry, neighbor->block_id) : NULL;
                        if (!block) return (size_t) - 1;
                        if (block->solid) {
                            shape.minimum[1] = block->collision_shape.maximum[1] - 1;
                            shape.maximum[1] = block->collision_shape.maximum[1];
                        }
                    } else if (model == MAAGE_BLOCK_MESH_TORCH) {
                        shape.minimum[0] = shape.minimum[2] = 7.0 / 16.0;
                        shape.maximum[0] = shape.maximum[2] = 9.0 / 16.0;
                        shape.maximum[1] = 10.0 / 16.0;
                    } else if (current->mesh_inset) {
                        shape.minimum[0] = shape.minimum[2] = 0;
                        shape.maximum[0] = shape.maximum[2] = 1;
                    }
                    above = maage_block_registry_state(registry, maage_chunk_get(chunk, x, y + 1, z));
                    if (vertices && state->fluid_kind) {
                        for (axis = 0; axis < 4; ++axis) {
                            fluid_heights[axis] = maage_mesh_fluid_height(registry, chunk, neighbors, x + (axis & 1), y, z + (axis >> 1), state->fluid_kind);
                            if (!above || above->fluid_kind != state->fluid_kind) fluid_heights[axis] -= 0.001f;
                        }
                        maage_mesh_fluid_flow(registry, chunk, neighbors, x, y, z, state, fluid_flow);
                    }
                    snowed = above && maage_block_registry_block(registry, above->block_id)->snow_cover;
                    colors_ready = 0;
                    log_axis = (int) state->texture_axis - 1;
                    for (face = 0; face < face_limit; ++face) {
                        if (!(face_mask & (1U << face))) continue;
                        face_shape = shape;
                        nx = x + (crossed ? 0 : maage_face_neighbor[face][0]);
                        ny = y + (crossed ? 0 : maage_face_neighbor[face][1]);
                        nz = z + (crossed ? 0 : maage_face_neighbor[face][2]);
                        adjacent = maage_mesh_neighbor(chunk, neighbors, &nx, &nz);
                        id = adjacent && ny >= 0 && ny < MAAGE_WORLD_HEIGHT ? maage_chunk_get(adjacent, nx, ny, nz) : ids->state_air;
                        neighbor = maage_block_registry_state(registry, id);
                        block = neighbor ? maage_block_registry_block(registry, neighbor->block_id) : NULL;
                        if (!block) return (size_t) - 1;
                        axis = face / 2;
                        boundary = model == MAAGE_BLOCK_MESH_CUBE && (face & 1 ? shape.maximum[axis] == 1 : shape.minimum[axis] == 0);
                        if (current->mesh_inset && axis != 1) {
                            face_shape.minimum[axis] = face_shape.maximum[axis] = face & 1 ? 0.9375 : 0.0625;
                            boundary = 0;
                        }
                        if (boundary && (!state->fluid_kind || face != MAAGE_FACE_POSITIVE_Y) && (block->opaque || (mode == MAAGE_LEAVES_FAST && block->foliage))) continue;
                        if (boundary && current->cull_same && state->block_id == neighbor->block_id) continue;
                        if (current->foliage && block->foliage && boundary) {
                            if (mode == MAAGE_LEAVES_FAST) continue;
                            if (mode == MAAGE_LEAVES_SMART && maage_mesh_smart_leaf_hidden(registry, chunk, neighbors, x, y, z, face)) continue;
                        }
                        if (state->fluid_kind && state->fluid_kind == neighbor->fluid_kind) continue;
                        definition = &state->faces[crossed ? MAAGE_FACE_POSITIVE_Z : face];
                        overlay = definition->overlay_uv[2] > definition->overlay_uv[0] && !snowed;
                        reverse = state->fluid_definition && state->fluid_definition->reverse_faces && face != MAAGE_FACE_NEGATIVE_Y;
                        if (face_count > UINT_MAX / 4U - 1U - (overlay ? 1U : 0U) - (reverse ? 1U : 0U) || (vertices && (face_count > face_capacity || 1U + (overlay ? 1U : 0U) + (reverse ? 1U : 0U) > face_capacity - face_count))) {
                            *failure = MAAGE_STATUS_BUFFER_OVERFLOW;
                            return (size_t) - 1;
                        }
                        if (vertices) {
                            tint_mode = definition->tint_mode;
                            uv = definition->uv;
                            animation = definition->animation;
                            flow = NULL;
                            if (state->fluid_kind && definition->has_flow_texture && definition->flow_uv[2] > definition->flow_uv[0] &&
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
                            if (mode == MAAGE_LEAVES_FAST && current->foliage && state->leaf_fast_uv[2] > state->leaf_fast_uv[0]) uv = state->leaf_fast_uv;
                            if (!colors_ready && (tint_mode || overlay)) {
                                for (corner = 0; corner < 4; ++corner) {
                                    sample_colors = maage_mesh_biome_colors(biomes, chunk, neighbors, blend, cache, x + (blend >= MAAGE_BIOME_BLEND_SMOOTH_5 ? corner & 1 : 0), z + (blend >= MAAGE_BIOME_BLEND_SMOOTH_5 ? corner >> 1 : 0));
                                    for (tint = 0; tint < 4; ++tint)
                                        for (component = 0; component < 3; ++component)
                                            colors[tint][corner][component] = sample_colors[tint][component];
                                }
                                colors_ready = 1;
                            }
                            smooth = model == MAAGE_BLOCK_MESH_CUBE && !current->emitted_light && !state->fluid_kind && !current->mesh_inset;
                            /* A recessed top with neighbor brightness samples
                             * above its surface, outside the opaque cell.
                             * Geometric boundary checks still own face culling. */
                            if (state->fluid_kind) maage_mesh_fluid_lighting(registry, chunk, neighbors, x, y, z, face, current->emitted_light, &illumination);
                            else maage_mesh_lighting(registry, chunk, neighbors, x, y, z, crossed ? MAAGE_FACE_POSITIVE_Y : face, boundary || (model == MAAGE_BLOCK_MESH_CUBE && current->use_neighbor_brightness && face == MAAGE_FACE_POSITIVE_Y), smooth, current->emitted_light, &illumination);
                            maage_mesh_face(vertices, indices, face_count, chunk->x * 16 + x, y, chunk->z * 16 + z, face, definition, uv, &face_shape, model, state->metadata, log_axis, material, tint_mode ? colors[tint_mode] : neutral, tint_mode && blend >= MAAGE_BIOME_BLEND_SMOOTH_5, animation, state->fluid_kind ? fluid_heights : NULL, flow, &illumination);
                            if (!overlay && !reverse && !state->fluid_kind && model == MAAGE_BLOCK_MESH_CUBE && material == MAAGE_MESH_MATERIAL_OPAQUE && log_axis < 0 && uv[2] > uv[0] && uv[3] > uv[1] && face_shape.minimum[0] == 0 && face_shape.minimum[1] == 0 && face_shape.minimum[2] == 0 && face_shape.maximum[0] == 1 && face_shape.maximum[1] == 1 && face_shape.maximum[2] == 1) {
                                maage_mesh_vertex *quad = vertices + face_count * 4;
                                for (corner = 1; corner < 4; ++corner)
                                    if (memcmp(quad[0].color, quad[corner].color, sizeof(quad[0].color)) || memcmp(quad[0].light, quad[corner].light, sizeof(quad[0].light))) break;
                                if (corner == 4)
                                    for (corner = 0; corner < 4; ++corner) {
                                        memcpy(quad[corner].tile, uv, sizeof(quad[corner].tile));
                                        quad[corner].uv[0] = quad[corner].uv[0] == uv[0] ? 0 : 1;
                                        quad[corner].uv[1] = quad[corner].uv[1] == uv[1] ? 0 : 1;
                                    }
                            }
                            if (overlay) maage_mesh_face(vertices, indices, face_count + 1, chunk->x * 16 + x, y, chunk->z * 16 + z, face, definition, definition->overlay_uv, &face_shape, model, state->metadata, -1, MAAGE_MESH_MATERIAL_CUTOUT, colors[definition->overlay_tint_mode], definition->overlay_tint_mode && blend >= MAAGE_BIOME_BLEND_SMOOTH_5, NULL, NULL, NULL, &illumination);
                            if (reverse) maage_mesh_reverse_face(vertices, indices, face_count, face_count + 1 + (overlay ? 1 : 0));
                        }
                        if (owners) {
                            size_t owner, end = face_count + 1 + (overlay ? 1 : 0) + (reverse ? 1 : 0);
                            for (owner = face_count; owner < end; ++owner) {
                                owners[owner].minimum = owners[owner].maximum = (unsigned short) (y * 256 + z * 16 + x);
                            }
                        }
                        face_count += 1 + (overlay ? 1 : 0) + (reverse ? 1 : 0);
                    }
                }
        }
    }
    return face_count;
}

/* Greedy rectangles remain inside a physical section. Only constant corner
 * shading and identical atlas/material/animation tuples merge. Other geometry
 * uses the original path, including transparent sorting and arbitrary shapes. */
static unsigned int maage_mesh_direction(const maage_world_mesh *mesh, size_t face) {
    const maage_mesh_vertex *vertices = mesh->vertices + face * 4;
    unsigned int axis, corner, cell = mesh->owners[face].minimum;
    float owner[3];
    owner[0] = (float) (mesh->chunk_x * 16 + (int) (cell & 15U));
    owner[1] = (float) (cell >> 8);
    owner[2] = (float) (mesh->chunk_z * 16 + (int) ((cell >> 4) & 15U));
    for (axis = 0; axis < 3; ++axis) {
        for (corner = 1; corner < 4; ++corner)
            if (vertices[corner].position[axis] != vertices[0].position[axis]) break;
        if (corner == 4) return axis * 2U + (vertices[0].position[axis] > owner[axis] ? 1U : 0U);
    }
    return 0;
}
static int maage_mesh_same_surface(const maage_world_mesh *mesh, size_t first, size_t second) {
    const maage_mesh_vertex *a = mesh->vertices + first * 4, *b = mesh->vertices + second * 4;
    unsigned int corner;
    for (corner = 0; corner < 4; ++corner)
        if (memcmp(a[corner].uv, b[corner].uv, sizeof(a[corner]) - offsetof(maage_mesh_vertex, uv))) return 0;
    return 1;
}
static unsigned int maage_mesh_grid_cell(unsigned int plane, unsigned int u, unsigned int v, unsigned int axis) {
    unsigned int point[3];
    point[axis] = plane;
    point[(axis + 1) % 3] = u;
    point[(axis + 2) % 3] = v;
    return point[1] * 256U + point[2] * 16U + point[0];
}
static maage_status maage_mesh_merge(maage_world_mesh *mesh, size_t *faces, const maage_job_token *token) {
    unsigned int *grid;
    unsigned int section, direction, axis, plane, u, v, width, height, x, y, corner, component, origin[3], end[3];
    size_t face, source, next, first, cursor = 0, written = 0;
    unsigned int planes[6][16], point[3], candidates;
    maage_mesh_vertex *quad;
    float gradients[2][2];
    void *shrunk;
    grid = (unsigned int *) calloc(6UL * MAAGE_SECTION_VOLUME, sizeof(*grid));
    if (!grid) return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_MESH, 0);
    for (section = 0; section < 16; ++section) {
        if (token && maage_job_cancelled(token)) {
            free(grid);
            return maage_status_make(MAAGE_STATUS_INTERRUPTED, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_MESH, MAAGE_DETAIL_CANCELLED);
        }
        /* The ordinary generator emits all faces of each section together. */
        first = cursor;
        while (cursor < *faces && mesh->owners[cursor].minimum / MAAGE_SECTION_VOLUME == section)
            ++cursor;
        if (first == cursor) continue;
        candidates = 0;
        memset(planes, 0, sizeof(planes));
        memset(grid, 0, 6UL * MAAGE_SECTION_VOLUME * sizeof(*grid));
        for (face = first; face < cursor; ++face)
            if (mesh->owners[face].minimum <= mesh->owners[face].maximum && mesh->owners[face].minimum / MAAGE_SECTION_VOLUME == section && mesh->vertices[face * 4].tile[2] > mesh->vertices[face * 4].tile[0]) {
                direction = maage_mesh_direction(mesh, face);
                grid[direction * MAAGE_SECTION_VOLUME + mesh->owners[face].minimum % MAAGE_SECTION_VOLUME] = (unsigned int) face + 1U;
                point[0] = mesh->owners[face].minimum & 15U;
                point[1] = (mesh->owners[face].minimum >> 8) & 15U;
                point[2] = (mesh->owners[face].minimum >> 4) & 15U;
                ++planes[direction][point[direction / 2]];
                ++candidates;
            }
        if (!candidates) continue;
        for (direction = 0; direction < 6; ++direction) {
            axis = direction / 2;
            for (plane = 0; plane < 16; ++plane) {
                if (!planes[direction][plane]) continue;
                for (v = 0; v < 16; ++v)
                    for (u = 0; u < 16; ++u) {
                        source = grid[direction * MAAGE_SECTION_VOLUME + maage_mesh_grid_cell(plane, u, v, axis)];
                        if (!source) continue;
                        --source;
                        for (width = 1; u + width < 16; ++width) {
                            next = grid[direction * MAAGE_SECTION_VOLUME + maage_mesh_grid_cell(plane, u + width, v, axis)];
                            if (!next || !maage_mesh_same_surface(mesh, source, next - 1)) break;
                        }
                        for (height = 1; v + height < 16; ++height) {
                            for (x = 0; x < width; ++x) {
                                next = grid[direction * MAAGE_SECTION_VOLUME + maage_mesh_grid_cell(plane, u + x, v + height, axis)];
                                if (!next || !maage_mesh_same_surface(mesh, source, next - 1)) break;
                            }
                            if (x != width) break;
                        }
                        for (y = 0; y < height; ++y)
                            for (x = 0; x < width; ++x) {
                                unsigned int *entry = &grid[direction * MAAGE_SECTION_VOLUME + maage_mesh_grid_cell(plane, u + x, v + y, axis)];
                                if (*entry - 1U != source) {
                                    mesh->owners[*entry - 1U].minimum = 1;
                                    mesh->owners[*entry - 1U].maximum = 0;
                                }
                                *entry = 0;
                            }
                        if (width == 1 && height == 1) continue;
                        origin[axis] = end[axis] = plane;
                        origin[(axis + 1) % 3] = u;
                        origin[(axis + 2) % 3] = v;
                        end[(axis + 1) % 3] = u + width - 1U;
                        end[(axis + 2) % 3] = v + height - 1U;
                        mesh->owners[source].maximum = (unsigned short) (section * MAAGE_SECTION_VOLUME + end[1] * 256U + end[2] * 16U + end[0]);
                        quad = mesh->vertices + source * 4;
                        memset(gradients, 0, sizeof(gradients));
                        for (x = 0; x < 2; ++x) {
                            unsigned int coordinate = (axis + x + 1) % 3;
                            for (corner = 1; corner < 4; ++corner)
                                if (quad[corner].position[coordinate] != quad[0].position[coordinate] && quad[corner].position[(axis + 2 - x) % 3] == quad[0].position[(axis + 2 - x) % 3]) {
                                    for (component = 0; component < 2; ++component)
                                        gradients[x][component] = (quad[corner].uv[component] - quad[0].uv[component]) / (quad[corner].position[coordinate] - quad[0].position[coordinate]);
                                    break;
                                }
                        }
                        for (corner = 0; corner < 4; ++corner) {
                            float du = quad[corner].position[(axis + 1) % 3] - (float) (origin[(axis + 1) % 3] + ((axis + 1) % 3 == 1 ? section * 16U : 0U));
                            float dv = quad[corner].position[(axis + 2) % 3] - (float) (origin[(axis + 2) % 3] + ((axis + 2) % 3 == 1 ? section * 16U : 0U));
                            if ((axis + 1) % 3 == 0) du -= (float) (mesh->chunk_x * 16);
                            if ((axis + 1) % 3 == 2) du -= (float) (mesh->chunk_z * 16);
                            if ((axis + 2) % 3 == 0) dv -= (float) (mesh->chunk_x * 16);
                            if ((axis + 2) % 3 == 2) dv -= (float) (mesh->chunk_z * 16);
                            du *= (float) (width - 1U);
                            dv *= (float) (height - 1U);
                            quad[corner].position[(axis + 1) % 3] += du;
                            quad[corner].position[(axis + 2) % 3] += dv;
                            for (component = 0; component < 2; ++component)
                                quad[corner].uv[component] += du * gradients[0][component] + dv * gradients[1][component];
                        }
                    }
            }
        }
    }
    free(grid);
    for (face = 0; face < *faces; ++face) {
        unsigned int indices[6];
        if (mesh->owners[face].minimum > mesh->owners[face].maximum) continue;
        for (corner = 0; corner < 6; ++corner)
            indices[corner] = mesh->indices[face * 6 + corner] - (unsigned int) (face * 4) + (unsigned int) (written * 4);
        if (face != written) {
            memmove(mesh->vertices + written * 4, mesh->vertices + face * 4, 4 * sizeof(*mesh->vertices));
            mesh->owners[written] = mesh->owners[face];
        }
        memcpy(mesh->indices + written * 6, indices, sizeof(indices));
        ++written;
    }
    if (written) {
        shrunk = realloc(mesh->vertices, written * 4 * sizeof(*mesh->vertices));
        if (shrunk) {
            mesh->vertices = (maage_mesh_vertex *) shrunk;
            mesh->vertex_bytes = written * 4 * sizeof(*mesh->vertices);
        }
        shrunk = realloc(mesh->indices, written * 6 * sizeof(*mesh->indices));
        if (shrunk) {
            mesh->indices = (unsigned int *) shrunk;
            mesh->index_bytes = written * 6 * sizeof(*mesh->indices);
        }
        shrunk = realloc(mesh->owners, written * sizeof(*mesh->owners));
        if (shrunk) {
            mesh->owners = (maage_mesh_owner *) shrunk;
            mesh->retained_bytes -= (*faces - written) * sizeof(*mesh->owners);
        }
        shrunk = realloc(mesh->face_flags, written);
        if (shrunk) {
            mesh->face_flags = (unsigned char *) shrunk;
            mesh->retained_bytes -= *faces - written;
        }
    }
    *faces = written;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_MESH, 0);
}

static maage_status maage_mesh_failure(maage_status_trace *trace, maage_status code) {
    maage_status result = code > 255U ? (maage_status) code : maage_status_make(code, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_MESH, code == MAAGE_STATUS_INTERRUPTED ? MAAGE_DETAIL_CANCELLED : 0);
    return MAAGE_STATUS_TRACE(trace, result, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_MESH);
}
maage_status maage_world_mesh_create_chunk_job(const maage_block_registry *blocks, const maage_biome_registry *biomes, const maage_chunk *chunk, const maage_chunk *neighbors[8], maage_leaves_mode mode, maage_biome_blend blend, const maage_job_token *token, size_t reserved_bytes, size_t *required_bytes, maage_world_mesh **output, maage_status_trace *trace) {
    maage_world_mesh *mesh;
    maage_mesh_biome_cache cache;
    size_t faces, written, preparation_bytes, source_bytes, maximum = (size_t) - 1;
    size_t per_face = 4 * sizeof(maage_mesh_vertex) + 6 * sizeof(unsigned int) + sizeof(maage_mesh_owner) + sizeof(unsigned char);
    maage_status estimate;
    maage_status failure = MAAGE_STATUS_INVARIANT_VIOLATION;
    if (!output) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_MESH, 5);
    *output = NULL;
    cache.initialized = 0;
    if (mode < MAAGE_LEAVES_FAST || mode > MAAGE_LEAVES_FANCY || blend < MAAGE_BIOME_BLEND_OFF || blend > MAAGE_BIOME_BLEND_SMOOTH_9) return maage_mesh_failure(trace, MAAGE_STATUS_INVALID_ARGUMENT);
    if (!blocks || !biomes || !chunk || !neighbors) return MAAGE_STATUS_TRACE(trace, maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_MESH, !blocks ? 0 : (!biomes ? 1 : (!chunk ? 2 : 3))), MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_MESH);
    faces = maage_mesh_generate(blocks, biomes, chunk, neighbors, NULL, NULL, NULL, mode, blend, &cache, token, NULL, 63U, (size_t) - 1, &failure);
    if (faces == maximum) return maage_mesh_failure(trace, failure);
    if (faces > UINT_MAX / 4 ||
        faces > maximum / (4 * sizeof(maage_mesh_vertex)) ||
        faces > maximum / (6 * sizeof(unsigned int)) || faces > maximum / sizeof(maage_block_position))
        return maage_mesh_failure(trace, MAAGE_STATUS_BUFFER_OVERFLOW);
    /* Reserve a conservative peak before allocating faces, deduplication tables
     * or preparation. A larger result is retried by the owner, never waited on
     * by a worker. The reservation includes unmerged output and scratch. */
    estimate = maage_mesh_preparation_estimate_bytes(faces * 4, faces * 6, &preparation_bytes);
    if (maage_status_failed(estimate)) return MAAGE_STATUS_PROPAGATE(trace, estimate, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_MESH);
    if (maximum < 1048576UL || sizeof(*mesh) > maximum - 1048576UL || faces > (maximum - 1048576UL - sizeof(*mesh)) / per_face) return maage_mesh_failure(trace, MAAGE_STATUS_BUFFER_OVERFLOW);
    source_bytes = 1048576UL + sizeof(*mesh) + faces * per_face;
    if (preparation_bytes > maximum - source_bytes) return maage_mesh_failure(trace, MAAGE_STATUS_BUFFER_OVERFLOW);
    *required_bytes = source_bytes + preparation_bytes;
    if (*required_bytes > reserved_bytes) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_MESH, 0);
    if (token && maage_job_cancelled(token)) return maage_mesh_failure(trace, MAAGE_STATUS_INTERRUPTED);
    mesh = (maage_world_mesh *) calloc(1, sizeof(*mesh));
    if (!mesh) return maage_mesh_failure(trace, MAAGE_STATUS_ALLOCATION_FAILED);
    mesh->chunk_x = chunk->x;
    mesh->chunk_z = chunk->z;
    mesh->vertex_bytes = faces * 4 * sizeof(*mesh->vertices);
    mesh->index_bytes = faces * 6 * sizeof(*mesh->indices);
    mesh->retained_bytes = faces * (sizeof(*mesh->owners) + sizeof(*mesh->face_flags));
    if (faces) {
        mesh->vertices = (maage_mesh_vertex *) malloc(faces * 4 * sizeof(*mesh->vertices));
        mesh->indices = (unsigned int *) malloc(faces * 6 * sizeof(*mesh->indices));
        mesh->owners = (maage_mesh_owner *) malloc(faces * sizeof(*mesh->owners));
        mesh->face_flags = (unsigned char *) malloc(faces * sizeof(*mesh->face_flags));
        if (!mesh->vertices || !mesh->indices || !mesh->owners || !mesh->face_flags) {
            maage_world_mesh_destroy(mesh);
            return maage_mesh_failure(trace, MAAGE_STATUS_ALLOCATION_FAILED);
        }
        written = maage_mesh_generate(blocks, biomes, chunk, neighbors, mesh->vertices, mesh->indices, mesh->owners, mode, blend, &cache, token, NULL, 63U, faces, &failure);
        if (written != faces) {
            maage_world_mesh_destroy(mesh);
            return maage_mesh_failure(trace, failure);
        }
    }
    if (faces) {
        maage_status result = maage_mesh_merge(mesh, &faces, token);
        if (maage_status_failed(result)) {
            maage_world_mesh_destroy(mesh);
            return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_MESH);
        }
    }
    mesh->data.vertices = mesh->vertices;
    mesh->data.indices = mesh->indices;
    mesh->data.vertex_count = faces * 4;
    mesh->data.index_count = faces * 6;
    if (faces) {
        maage_status result = maage_mesh_prepare(&mesh->data, &mesh->prepared);
        if (maage_status_failed(result)) {
            maage_world_mesh_destroy(mesh);
            return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_MESH);
        }
        /* The validated quad geometry is only needed until the GPU upload.
         * Preserve all information used by later prediction filtering in a
         * single byte for each face. */
        for (written = 0; written < faces; ++written) {
            unsigned int pattern, corner, category;
            float material = mesh->vertices[written * 4].material;
            for (pattern = 0; pattern < 4; ++pattern) {
                for (corner = 0; corner < 6; ++corner)
                    if (mesh->indices[written * 6 + corner] != (unsigned int) (written * 4) + maage_mesh_quad_indices[pattern][corner]) break;
                if (corner == 6) break;
            }
            if (pattern == 4) {
                maage_world_mesh_destroy(mesh);
                return maage_mesh_failure(trace, MAAGE_STATUS_INVARIANT_VIOLATION);
            }
            category = material == MAAGE_MESH_MATERIAL_TRANSLUCENT ? 1U : (material == MAAGE_MESH_MATERIAL_MULTIPLY ? 2U : 0U);
            mesh->face_flags[written] = (unsigned char) (category | (pattern << 2) | (maage_mesh_direction(mesh, written) << 4));
        }
    }
    *output = mesh;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_MESH, 0);
}

size_t maage_world_mesh_resident_bytes(const maage_world_mesh *mesh) {
    return mesh ? sizeof(*mesh) + mesh->vertex_bytes + mesh->index_bytes + mesh->retained_bytes + maage_mesh_preparation_resident_bytes(mesh->prepared) : 0;
}
maage_mesh_preparation *maage_world_mesh_take_preparation(maage_world_mesh *mesh) {
    maage_mesh_preparation *prepared = mesh ? mesh->prepared : NULL;
    if (mesh) mesh->prepared = NULL;
    return prepared;
}

const maage_mesh_data *maage_world_mesh_data(const maage_world_mesh *mesh) {
    return mesh ? &mesh->data : NULL;
}
const unsigned char *maage_world_mesh_face_flags(const maage_world_mesh *mesh) {
    return mesh ? mesh->face_flags : NULL;
}
void maage_world_mesh_release_upload_data(maage_world_mesh *mesh) {
    if (!mesh || mesh->prepared || !mesh->face_flags) return;
    /* Only the Client calls this after a successful native upload. A future
     * index view reconstructs the original quad winding without CPU vertices. */
    free(mesh->indices);
    free(mesh->vertices);
    mesh->indices = NULL;
    mesh->vertices = NULL;
    mesh->data.indices = NULL;
    mesh->data.vertices = NULL;
    mesh->vertex_bytes = mesh->index_bytes = 0;
}
maage_status maage_world_mesh_destroy(maage_world_mesh *mesh) {
    if (!mesh) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_DESTROY, 0);
    maage_mesh_preparation_destroy(mesh->prepared);
    free(mesh->owners);
    free(mesh->face_flags);
    free(mesh->indices);
    free(mesh->vertices);
    free(mesh);
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_DESTROY, 0);
}

static int maage_mesh_interval_distance(int point, int minimum, int maximum) {
    return point < minimum ? minimum - point : (point > maximum ? point - maximum : 0);
}
static int maage_mesh_owner_removed(const maage_world_mesh *mesh, size_t face, const maage_block_position *removed, size_t count) {
    unsigned int minimum = mesh->owners[face].minimum, maximum = mesh->owners[face].maximum;
    size_t i;
    int dx, dy, dz;
    for (i = 0; i < count; ++i) {
        dx = maage_mesh_interval_distance(removed[i].x, mesh->chunk_x * 16 + (int) (minimum & 15U), mesh->chunk_x * 16 + (int) (maximum & 15U));
        dy = maage_mesh_interval_distance(removed[i].y, minimum >> 8, maximum >> 8);
        dz = maage_mesh_interval_distance(removed[i].z, mesh->chunk_z * 16 + (int) ((minimum >> 4) & 15U), mesh->chunk_z * 16 + (int) ((maximum >> 4) & 15U));
        if ((dx <= 1 && dy <= 1 && dz <= 1) || (!dx && !dy && dz <= 3) || (!dx && !dz && dy <= 3) || (!dy && !dz && dx <= 3)) return 1;
    }
    return 0;
}
maage_status maage_world_mesh_filter(const maage_world_mesh *mesh, const maage_block_position *removed, size_t count, maage_mesh_data *data, unsigned int **indices) {
    size_t face, retained = 0, written = 0;
    unsigned int corner, base, pattern;
    if (!mesh || !data || !indices || (count && !removed) || (mesh->data.index_count && (!mesh->owners || !mesh->face_flags)))
        return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_MESH, 0);
    *indices = NULL;
    *data = mesh->data;
    for (face = 0; face < mesh->data.index_count / 6; ++face)
        if (!maage_mesh_owner_removed(mesh, face, removed, count)) ++retained;
    if (retained) {
        *indices = (unsigned int *) malloc(retained * 6 * sizeof(**indices));
        if (!*indices) return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_MESH, 0);
    }
    for (face = 0; face < mesh->data.index_count / 6; ++face) {
        if (maage_mesh_owner_removed(mesh, face, removed, count)) continue;
        pattern = (mesh->face_flags[face] >> 2) & 3U;
        base = (unsigned int) (face * 4);
        for (corner = 0; corner < 6; ++corner)
            (*indices)[written * 6 + corner] = base + maage_mesh_quad_indices[pattern][corner];
        ++written;
    }
    data->indices = *indices;
    data->index_count = written * 6;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_MESH, 0);
}
/* Local edits remove complete merged rectangles. Outside the ordinary local
 * patch, regenerate only each removed rectangle's face direction, retaining all
 * unaffected cached faces. Each rectangle is section-bounded; no world jobs or
 * GPU waits are introduced on the input path. */
static void maage_mesh_patch_mask(unsigned char mask[65536], int x, int z, const maage_block_position *centers, size_t count, const maage_world_mesh *const *sources, size_t source_count, size_t *lower, size_t *upper) {
    maage_block_position cell;
    const maage_world_mesh *source;
    size_t i, j, face, index;
    unsigned int minimum, maximum, ix, iy, iz, direction;
    int dx, dy, dz;
    memset(mask, 0, 65536UL);
    *lower = 65536UL;
    *upper = 0;
    for (i = 0; i < count; ++i)
        for (dy = -3; dy <= 3; ++dy)
            for (dz = -3; dz <= 3; ++dz)
                for (dx = -3; dx <= 3; ++dx) {
                    if ((abs(dx) > 1 || abs(dy) > 1 || abs(dz) > 1) && ((dx != 0) + (dy != 0) + (dz != 0) != 1)) continue;
                    cell.x = centers[i].x + dx;
                    cell.y = centers[i].y + dy;
                    cell.z = centers[i].z + dz;
                    if (cell.y < 0 || cell.y >= 256 || maage_world_chunk_coordinate(cell.x) != x || maage_world_chunk_coordinate(cell.z) != z) continue;
                    index = (size_t) cell.y * 256 + maage_world_local_coordinate(cell.z) * 16 + maage_world_local_coordinate(cell.x);
                    mask[index] = 63U;
                    if (index < *lower) *lower = index;
                    if (index > *upper) *upper = index;
                }
    for (j = 0; j < source_count; ++j) {
        source = sources[j];
        if (!source || source->chunk_x != x || source->chunk_z != z) continue;
        for (face = 0; face < source->data.index_count / 6; ++face) {
            minimum = source->owners[face].minimum;
            maximum = source->owners[face].maximum;
            if (minimum == maximum || !maage_mesh_owner_removed(source, face, centers, count)) continue;
            direction = source->face_flags[face] >> 4;
            if (minimum < *lower) *lower = minimum;
            if (maximum > *upper) *upper = maximum;
            for (iy = minimum >> 8; iy <= maximum >> 8; ++iy)
                for (iz = (minimum >> 4) & 15U; iz <= ((maximum >> 4) & 15U); ++iz)
                    for (ix = minimum & 15U; ix <= (maximum & 15U); ++ix)
                        mask[iy * 256U + iz * 16U + ix] |= (unsigned char) (1U << direction);
        }
    }
}
maage_status maage_world_mesh_patch(const maage_world *world, const maage_block_position *centers, size_t count, const maage_world_mesh *const *sources, size_t source_count, maage_leaves_mode mode, maage_biome_blend blend, maage_world_mesh **output, maage_status_trace *trace) {
    maage_block_position columns[32 * 4], cell;
    const maage_chunk *chunk, *neighbors[8];
    maage_mesh_biome_cache cache;
    maage_world_mesh *mesh = NULL;
    unsigned char mask[65536];
    size_t i, j, n = 0, faces = 0, written = 0, amount, index, voxel, lower, upper;
    unsigned int pass;
    maage_status failure = MAAGE_STATUS_INVARIANT_VIOLATION;
    int x, z;
    const maage_block_registry *blocks;
    const maage_biome_registry *biomes;
    if (output) *output = NULL;
    if (!world || !output || count > 32 || (count && !centers) || (source_count && !sources) || mode < MAAGE_LEAVES_FAST || mode > MAAGE_LEAVES_FANCY || blend < MAAGE_BIOME_BLEND_OFF || blend > MAAGE_BIOME_BLEND_SMOOTH_9) return maage_mesh_failure(trace, MAAGE_STATUS_INVALID_ARGUMENT);
    for (i = 0; i < count; ++i) {
        if (centers[i].x <= -MAAGE_WORLD_LIMIT || centers[i].x >= MAAGE_WORLD_LIMIT || centers[i].z <= -MAAGE_WORLD_LIMIT || centers[i].z >= MAAGE_WORLD_LIMIT || centers[i].y < 0 || centers[i].y >= 256) return maage_mesh_failure(trace, MAAGE_STATUS_INVALID_ARGUMENT);
        for (z = maage_world_chunk_coordinate(centers[i].z - 3); z <= maage_world_chunk_coordinate(centers[i].z + 3); ++z)
            for (x = maage_world_chunk_coordinate(centers[i].x - 3); x <= maage_world_chunk_coordinate(centers[i].x + 3); ++x) {
                for (j = 0; j < n; ++j)
                    if (columns[j].x == x && columns[j].z == z) break;
                if (j == n) {
                    columns[n].x = x;
                    columns[n++].z = z;
                }
            }
    }
    for (pass = 0; pass < 2; ++pass) {
        if (pass) {
            if (faces > UINT_MAX / 4U || faces > (size_t) - 1 / (4 * sizeof(maage_mesh_vertex)) || faces > (size_t) - 1 / (6 * sizeof(unsigned int))) return maage_mesh_failure(trace, MAAGE_STATUS_BUFFER_OVERFLOW);
            mesh = (maage_world_mesh *) calloc(1, sizeof(*mesh));
            if (!mesh) return maage_mesh_failure(trace, MAAGE_STATUS_ALLOCATION_FAILED);
            if (faces) {
                mesh->vertices = (maage_mesh_vertex *) malloc(faces * 4 * sizeof(*mesh->vertices));
                mesh->indices = (unsigned int *) malloc(faces * 6 * sizeof(*mesh->indices));
                if (!mesh->vertices || !mesh->indices) {
                    maage_world_mesh_destroy(mesh);
                    return maage_mesh_failure(trace, MAAGE_STATUS_ALLOCATION_FAILED);
                }
            }
        }
        for (i = 0; i < n; ++i) {
            if (!maage_world_present_inputs(world, columns[i].x * 16, columns[i].z * 16, &chunk, neighbors, &blocks, &biomes)) continue;
            maage_mesh_patch_mask(mask, columns[i].x, columns[i].z, centers, count, sources, source_count, &lower, &upper);
            cache.initialized = 0;
            for (voxel = lower; voxel <= upper; ++voxel) {
                if (!mask[voxel]) continue;
                cell.x = (int) (voxel % 16);
                cell.y = (int) (voxel / 256);
                cell.z = (int) ((voxel / 16) % 16);
                amount = maage_mesh_generate(blocks, biomes, chunk, neighbors, pass && faces ? mesh->vertices + written * 4 : NULL, pass && faces ? mesh->indices + written * 6 : NULL, NULL, mode, blend, pass ? &cache : NULL, NULL, &cell, mask[voxel], pass ? faces - written : (size_t) - 1, &failure);
                if (amount == (size_t) - 1 || (pass && amount > faces - written)) {
                    maage_world_mesh_destroy(mesh);
                    return maage_mesh_failure(trace, failure);
                }
                if (!pass) {
                    if (amount > (size_t) - 1 - faces) return maage_mesh_failure(trace, MAAGE_STATUS_BUFFER_OVERFLOW);
                    faces += amount;
                } else {
                    for (index = 0; index < amount * 6; ++index)
                        mesh->indices[written * 6 + index] += (unsigned int) (written * 4);
                    written += amount;
                }
            }
        }
    }
    mesh->data.vertices = mesh->vertices;
    mesh->data.indices = mesh->indices;
    mesh->data.vertex_count = written * 4;
    mesh->data.index_count = written * 6;
    *output = mesh;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_MESH, 0);
}
