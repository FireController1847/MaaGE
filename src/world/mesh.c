#include "world_internal.h"
#include "maage/jobs.h"
#include <limits.h>
#include <stdlib.h>

struct maage_world_mesh {
    maage_mesh_data data;
    maage_mesh_vertex *vertices;
    unsigned int *indices;
};

static const int maage_face_neighbor[MAAGE_FACE_COUNT][3] = {
    {-1, 0, 0}, {1, 0, 0}, {0, -1, 0},
    {0, 1, 0}, {0, 0, -1}, {0, 0, 1}
};
/* Bottom-left, bottom-right, top-right, top-left viewed from outside. */
static const int maage_face_corners[MAAGE_FACE_COUNT][4][3] = {
    {{0, 0, 0}, {0, 0, 1}, {0, 1, 1}, {0, 1, 0}},
    {{1, 0, 1}, {1, 0, 0}, {1, 1, 0}, {1, 1, 1}},
    {{0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1}},
    {{0, 1, 1}, {1, 1, 1}, {1, 1, 0}, {0, 1, 0}},
    {{1, 0, 0}, {0, 0, 0}, {0, 1, 0}, {1, 1, 0}},
    {{0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}}
};

static void maage_mesh_face(maage_mesh_vertex *vertices, unsigned int *indices,
    size_t face_index, int x, int y, int z, unsigned int face,
    const maage_block_face_definition *definition, unsigned char light,
    const maage_block_shape *shape, int crossed)
{
    static const unsigned int offsets[6] = {0, 1, 2, 0, 2, 3};
    static const float shade[MAAGE_FACE_COUNT] = {0.6f, 0.6f, 0.5f, 1.0f, 0.8f, 0.8f};
    maage_mesh_vertex *vertex;
    static const int cross_corners[4][4][3] = {
        {{0,0,0},{1,0,1},{1,1,1},{0,1,0}},
        {{1,0,1},{0,0,0},{0,1,0},{1,1,1}},
        {{1,0,0},{0,0,1},{0,1,1},{1,1,0}},
        {{0,0,1},{1,0,0},{1,1,0},{0,1,1}}
    };
    static const float cross_normals[4][3] = {
        {-0.70710678f,0,0.70710678f}, {0.70710678f,0,-0.70710678f},
        {-0.70710678f,0,-0.70710678f}, {0.70710678f,0,0.70710678f}
    };
    unsigned int base, corner, component;
    const int *point;
    float brightness;
    brightness = crossed ? 0.9f : shade[face];
    base = (unsigned int)(face_index * 4);
    for (corner = 0; corner < 4; ++corner) {
        vertex = &vertices[face_index * 4 + corner];
        point = crossed ? cross_corners[face][corner] : maage_face_corners[face][corner];
        vertex->position[0] = (float)(x + shape->minimum[0] + point[0] * (shape->maximum[0] - shape->minimum[0]));
        vertex->position[1] = (float)(y + shape->minimum[1] + point[1] * (shape->maximum[1] - shape->minimum[1]));
        vertex->position[2] = (float)(z + shape->minimum[2] + point[2] * (shape->maximum[2] - shape->minimum[2]));
        for (component = 0; component < 3; ++component)
            vertex->normal[component] = crossed ? cross_normals[face][component] : (float)maage_face_neighbor[face][component];
        vertex->uv[0] = definition->uv[corner == 1 || corner == 2 ? 2 : 0];
        vertex->uv[1] = definition->uv[corner < 2 ? 3 : 1];
        for (component = 0; component < 3; ++component)
            vertex->color[component] = definition->tint[component] * brightness;
        vertex->color[3] = definition->tint[3];
        vertex->light[0] = (float)(light >> 4) / 15.0f;
        vertex->light[1] = (float)(light & 15) / 15.0f;
    }
    for (corner = 0; corner < 6; ++corner)
        indices[face_index * 6 + corner] = base + offsets[corner];
}

static const maage_chunk *maage_mesh_neighbor(const maage_chunk *chunk,
    const maage_chunk *neighbors[4], int *x, int *z)
{
    if (*x < 0) { *x += MAAGE_CHUNK_EDGE; return neighbors[0]; }
    if (*x >= MAAGE_CHUNK_EDGE) { *x -= MAAGE_CHUNK_EDGE; return neighbors[1]; }
    if (*z < 0) { *z += MAAGE_CHUNK_EDGE; return neighbors[2]; }
    if (*z >= MAAGE_CHUNK_EDGE) { *z -= MAAGE_CHUNK_EDGE; return neighbors[3]; }
    return chunk;
}

static size_t maage_mesh_generate(const maage_block_registry *registry,
    const maage_chunk *chunk, const maage_chunk *neighbors[4],
    maage_mesh_vertex *vertices, unsigned int *indices,
    const maage_job_token *token)
{
    const maage_block_state_definition *state, *neighbor;
    const maage_block_definition *block, *current;
    maage_block_shape shape, face_shape;
    double neighbor_height;
    const maage_chunk *adjacent;
    maage_block_state_id id;
    unsigned char light;
    int x, y, z, section, nx, ny, nz;
    unsigned int face, axis, face_limit;
    int crossed, boundary;
    size_t face_count = 0;
    for (section = 0; section < 16; ++section) {
        if (token && maage_job_cancelled(token)) return (size_t)-1;
        if (!chunk->sections[section]) continue;
        for (y = section * MAAGE_CHUNK_EDGE; y < (section + 1) * MAAGE_CHUNK_EDGE; ++y) {
            if (token && maage_job_cancelled(token)) return (size_t)-1;
            for (z = 0; z < MAAGE_CHUNK_EDGE; ++z) {
                for (x = 0; x < MAAGE_CHUNK_EDGE; ++x) {
                    id = maage_chunk_get(chunk, x, y, z);
                    state = maage_block_registry_state(registry, id);
                    if (!state) return (size_t)-1;
                    if (state->block_id == MAAGE_BLOCK_AIR) continue;
                    current = maage_block_registry_block(registry, state->block_id);
                    if (!current) return (size_t)-1;
                    crossed = current->mesh_shape == MAAGE_BLOCK_MESH_CROSS;
                    face_limit = crossed ? 4 : MAAGE_FACE_COUNT;
                    for (axis = 0; axis < 3; ++axis) { shape.minimum[axis] = 0; shape.maximum[axis] = 1; }
                    if (current->selectable && !crossed) shape = current->selection_shape;
                    if (state->fluid_kind && !(state->fluid_level & 8)) {
                        neighbor = maage_block_registry_state(registry, maage_chunk_get(chunk, x, y + 1, z));
                        if (!neighbor || neighbor->fluid_kind != state->fluid_kind)
                            shape.maximum[1] = (8.0 - (state->fluid_level & 7)) / 9.0;
                    }
                    for (face = 0; face < face_limit; ++face) {
                        face_shape = shape;
                        nx = x + (crossed ? 0 : maage_face_neighbor[face][0]);
                        ny = y + (crossed ? 0 : maage_face_neighbor[face][1]);
                        nz = z + (crossed ? 0 : maage_face_neighbor[face][2]);
                        adjacent = maage_mesh_neighbor(chunk, neighbors, &nx, &nz);
                        id = adjacent && ny >= 0 && ny < MAAGE_WORLD_HEIGHT ?
                            maage_chunk_get(adjacent, nx, ny, nz) : MAAGE_STATE_AIR;
                        neighbor = maage_block_registry_state(registry, id);
                        if (!neighbor) return (size_t)-1;
                        block = maage_block_registry_block(registry, neighbor->block_id);
                        if (!block) return (size_t)-1;
                        axis = face / 2;
                        boundary = crossed ? 0 : (face & 1 ? shape.maximum[axis] == 1 : shape.minimum[axis] == 0);
                        if (!crossed && block->opaque && boundary) continue;
                        if (!crossed && state->block_id == MAAGE_BLOCK_LEAVES && neighbor->block_id == MAAGE_BLOCK_LEAVES) continue;
                        if (!crossed && state->fluid_kind && state->fluid_kind == neighbor->fluid_kind) {
                            if (face == MAAGE_FACE_NEGATIVE_Y || face == MAAGE_FACE_POSITIVE_Y) continue;
                            neighbor_height = neighbor->fluid_level & 8 ? 1 : (8.0 - (neighbor->fluid_level & 7)) / 9.0;
                            if (neighbor_height >= shape.maximum[1]) continue;
                            face_shape.minimum[1] = neighbor_height;
                        }
                        if (vertices) {
                            light = adjacent && ny >= 0 && ny < MAAGE_WORLD_HEIGHT ?
                                maage_chunk_light(adjacent, nx, ny, nz) :
                                (ny < 0 ? 0 : 240);
                            maage_mesh_face(vertices, indices, face_count,
                                chunk->x * MAAGE_CHUNK_EDGE + x, y,
                                chunk->z * MAAGE_CHUNK_EDGE + z, face,
                                &state->faces[crossed ? MAAGE_FACE_POSITIVE_Z : face], light, &face_shape, crossed);
                        }
                        ++face_count;
                    }
                }
            }
        }
    }
    return face_count;
}

maage_world_mesh *maage_world_mesh_create_chunk_job(const maage_block_registry *blocks,
    const maage_biome_registry *biomes, const maage_chunk *chunk,
    const maage_chunk *neighbors[4], const maage_job_token *token)
{
    maage_world_mesh *mesh;
    size_t faces, maximum = (size_t)-1;
    if (!blocks || !biomes || !chunk || !neighbors) return NULL;
    faces = maage_mesh_generate(blocks, chunk, neighbors, NULL, NULL, token);
    if (faces == maximum || faces > UINT_MAX / 4 ||
        faces > maximum / (4 * sizeof(maage_mesh_vertex)) ||
        faces > maximum / (6 * sizeof(unsigned int))) return NULL;
    if (token && maage_job_cancelled(token)) return NULL;
    mesh = (maage_world_mesh *)calloc(1, sizeof(*mesh));
    if (!mesh) return NULL;
    if (faces) {
        mesh->vertices = (maage_mesh_vertex *)malloc(faces * 4 * sizeof(*mesh->vertices));
        mesh->indices = (unsigned int *)malloc(faces * 6 * sizeof(*mesh->indices));
        if (!mesh->vertices || !mesh->indices ||
            maage_mesh_generate(blocks, chunk, neighbors, mesh->vertices,
                mesh->indices, token) != faces) {
            maage_world_mesh_destroy(mesh);
            return NULL;
        }
    }
    mesh->data.vertices = mesh->vertices;
    mesh->data.indices = mesh->indices;
    mesh->data.vertex_count = faces * 4;
    mesh->data.index_count = faces * 6;
    return mesh;
}

maage_world_mesh *maage_world_mesh_create_chunk(const maage_block_registry *blocks,
    const maage_biome_registry *biomes, const maage_chunk *chunk,
    const maage_chunk *neighbors[4])
{
    return maage_world_mesh_create_chunk_job(blocks, biomes, chunk, neighbors, NULL);
}

const maage_mesh_data *maage_world_mesh_data(const maage_world_mesh *mesh)
{
    return mesh ? &mesh->data : NULL;
}
void maage_world_mesh_destroy(maage_world_mesh *mesh)
{
    if (!mesh) return;
    free(mesh->indices);
    free(mesh->vertices);
    free(mesh);
}
