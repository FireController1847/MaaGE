#include <maage/world.h>

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

/* Bottom-left, bottom-right, top-right, top-left when viewed from outside. */
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
    const maage_block_face_definition *definition)
{
    maage_mesh_vertex *vertex;
    unsigned int base;
    unsigned int corner;
    unsigned int component;
    static const unsigned int offsets[6] = {0, 1, 2, 0, 2, 3};
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
        for (component = 0; component < 4; ++component)
            vertex->color[component] = definition->tint[component];
    }
    for (corner = 0; corner < 6; ++corner)
        indices[face_index * 6 + corner] = base + offsets[corner];
}

static size_t maage_mesh_generate(const maage_world *world,
    maage_mesh_vertex *vertices, unsigned int *indices)
{
    const maage_block_registry *registry;
    const maage_block_state_definition *state;
    const maage_block_state_definition *neighbor;
    const maage_block_definition *block;
    int minimum[3];
    int maximum[3];
    int x;
    int y;
    int z;
    unsigned int face;
    size_t face_count;
    registry = maage_world_registry(world);
    maage_world_bounds(world, minimum, maximum);
    face_count = 0;
    for (z = minimum[2]; z < maximum[2]; ++z) {
        for (y = minimum[1]; y < maximum[1]; ++y) {
            for (x = minimum[0]; x < maximum[0]; ++x) {
                state = maage_block_registry_state(registry,
                    maage_world_get(world, x, y, z));
                if (state == NULL) return (size_t)-1;
                if (state->block_id == MAAGE_BLOCK_AIR) continue;
                for (face = 0; face < MAAGE_FACE_COUNT; ++face) {
                    neighbor = maage_block_registry_state(registry,
                        maage_world_get(world, x + maage_face_neighbor[face][0],
                            y + maage_face_neighbor[face][1],
                            z + maage_face_neighbor[face][2]));
                    if (neighbor == NULL) return (size_t)-1;
                    block = maage_block_registry_block(registry,
                        neighbor->block_id);
                    if (block == NULL) return (size_t)-1;
                    if (block->opaque) continue;
                    if (vertices != NULL)
                        maage_mesh_face(vertices, indices, face_count,
                            x, y, z, face, &state->faces[face]);
                    ++face_count;
                }
            }
        }
    }
    return face_count;
}

maage_world_mesh *maage_world_mesh_create(const maage_world *world)
{
    maage_world_mesh *mesh;
    size_t faces;
    size_t maximum;
    if (world == NULL) return NULL;
    faces = maage_mesh_generate(world, NULL, NULL);
    maximum = (size_t)-1;
    if (faces == maximum || faces > UINT_MAX / 4 ||
        faces > maximum / (4 * sizeof(maage_mesh_vertex)) ||
        faces > maximum / (6 * sizeof(unsigned int))) return NULL;
    mesh = (maage_world_mesh *)calloc(1, sizeof(*mesh));
    if (mesh == NULL) return NULL;
    if (faces != 0) {
        mesh->vertices = (maage_mesh_vertex *)malloc(
            faces * 4 * sizeof(*mesh->vertices));
        mesh->indices = (unsigned int *)malloc(faces * 6 * sizeof(*mesh->indices));
        if (mesh->vertices == NULL || mesh->indices == NULL) {
            maage_world_mesh_destroy(mesh);
            return NULL;
        }
        if (maage_mesh_generate(world, mesh->vertices, mesh->indices) != faces) {
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

const maage_mesh_data *maage_world_mesh_data(const maage_world_mesh *mesh)
{
    return mesh != NULL ? &mesh->data : NULL;
}

void maage_world_mesh_destroy(maage_world_mesh *mesh)
{
    if (mesh == NULL) return;
    free(mesh->indices);
    free(mesh->vertices);
    free(mesh);
}
