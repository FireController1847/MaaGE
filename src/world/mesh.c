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
    const maage_block_face_definition *definition, unsigned char light)
{
    static const unsigned int offsets[6] = {0, 1, 2, 0, 2, 3};
    static const float shade[MAAGE_FACE_COUNT] = {0.6f, 0.6f, 0.5f, 1.0f, 0.8f, 0.8f};
    maage_mesh_vertex *vertex;
    unsigned int base, corner, component, level;
    float brightness;
    level = light >> 4;
    if ((light & 15) > level) level = light & 15;
    /* Initial skylight/blocklight uses a small ambient floor, not uniform light. */
    brightness = (0.12f + 0.88f * (float)level / 15.0f) * shade[face];
    base = (unsigned int)(face_index * 4);
    for (corner = 0; corner < 4; ++corner) {
        vertex = &vertices[face_index * 4 + corner];
        vertex->position[0] = (float)(x + maage_face_corners[face][corner][0]);
        vertex->position[1] = (float)(y + maage_face_corners[face][corner][1]);
        vertex->position[2] = (float)(z + maage_face_corners[face][corner][2]);
        for (component = 0; component < 3; ++component)
            vertex->normal[component] = (float)maage_face_neighbor[face][component];
        vertex->uv[0] = definition->uv[corner == 1 || corner == 2 ? 2 : 0];
        vertex->uv[1] = definition->uv[corner < 2 ? 3 : 1];
        for (component = 0; component < 3; ++component)
            vertex->color[component] = definition->tint[component] * brightness;
        vertex->color[3] = definition->tint[3];
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
    const maage_block_definition *block;
    const maage_chunk *adjacent;
    maage_block_state_id id;
    unsigned char light;
    int x, y, z, section, nx, ny, nz;
    unsigned int face;
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
                    for (face = 0; face < MAAGE_FACE_COUNT; ++face) {
                        nx = x + maage_face_neighbor[face][0];
                        ny = y + maage_face_neighbor[face][1];
                        nz = z + maage_face_neighbor[face][2];
                        adjacent = maage_mesh_neighbor(chunk, neighbors, &nx, &nz);
                        id = adjacent && ny >= 0 && ny < MAAGE_WORLD_HEIGHT ?
                            maage_chunk_get(adjacent, nx, ny, nz) : MAAGE_STATE_AIR;
                        neighbor = maage_block_registry_state(registry, id);
                        if (!neighbor) return (size_t)-1;
                        block = maage_block_registry_block(registry, neighbor->block_id);
                        if (!block) return (size_t)-1;
                        if (block->opaque || (state->block_id == MAAGE_BLOCK_WATER &&
                            neighbor->block_id == MAAGE_BLOCK_WATER)) continue;
                        if (vertices) {
                            light = adjacent && ny >= 0 && ny < MAAGE_WORLD_HEIGHT ?
                                maage_chunk_light(adjacent, nx, ny, nz) :
                                (ny < 0 ? 0 : 240);
                            maage_mesh_face(vertices, indices, face_count,
                                chunk->x * MAAGE_CHUNK_EDGE + x, y,
                                chunk->z * MAAGE_CHUNK_EDGE + z, face,
                                &state->faces[face], light);
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
