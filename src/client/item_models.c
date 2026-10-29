#include "item_models.h"
#include <math.h>
#include <string.h>

static const int maage_interaction_corners[6][4][3] = {
    {{0, 0, 0}, {0, 0, 1}, {0, 1, 1}, {0, 1, 0}},
    {{1, 0, 1}, {1, 0, 0}, {1, 1, 0}, {1, 1, 1}},
    {{0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1}},
    {{0, 1, 1}, {1, 1, 1}, {1, 1, 0}, {0, 1, 0}},
    {{1, 0, 0}, {0, 0, 0}, {0, 1, 0}, {1, 1, 0}},
    {{0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}}};

static void maage_item_model_quad(maage_mesh_vertex *vertices, unsigned int *indices,
                                  size_t *faces, const float positions[4][3], const float uv[4],
                                  const float tint[4], float shade, float material, const float *animation) {
    static const unsigned int offsets[6] = {0, 1, 2, 0, 2, 3};
    maage_mesh_vertex *vertex;
    float edge[2][3], normal[3], length;
    unsigned int corner, axis, base = (unsigned int) (*faces * 4);
    for (axis = 0; axis < 3; ++axis) {
        edge[0][axis] = positions[1][axis] - positions[0][axis];
        edge[1][axis] = positions[2][axis] - positions[0][axis];
    }
    for (axis = 0; axis < 3; ++axis)
        normal[axis] = edge[0][(axis + 1) % 3] * edge[1][(axis + 2) % 3] -
                       edge[0][(axis + 2) % 3] * edge[1][(axis + 1) % 3];
    length = (float) sqrt(normal[0] * normal[0] + normal[1] * normal[1] + normal[2] * normal[2]);
    for (corner = 0; corner < 4; ++corner) {
        vertex = &vertices[base + corner];
        memset(vertex, 0, sizeof(*vertex));
        for (axis = 0; axis < 3; ++axis) {
            vertex->position[axis] = positions[corner][axis];
            vertex->normal[axis] = length > 0 ? normal[axis] / length : 0;
            vertex->color[axis] = tint[axis] * shade;
            if (animation) vertex->animation[axis] = animation[axis];
        }
        vertex->color[3] = tint[3];
        vertex->material = material;
        vertex->light[0] = 1;
        vertex->uv[0] = uv[corner == 1 || corner == 2 ? 2 : 0];
        vertex->uv[1] = uv[corner < 2 ? 3 : 1];
    }
    for (corner = 0; corner < 6; ++corner)
        indices[*faces * 6 + corner] = base + offsets[corner];
    ++*faces;
}
void maage_client_item_box(const float grass_color[3],
                           maage_mesh_vertex *vertices, unsigned int *indices, size_t *faces,
                           const double minimum[3], const double maximum[3],
                           const maage_block_state_definition *state, float material) {
    static const float shade[6] = {0.6f, 0.6f, 0.5f, 1, 0.8f, 0.8f};
    float positions[4][3], tint[4];
    const maage_block_face_definition *definition;
    unsigned int face, corner, axis;
    for (face = 0; face < MAAGE_FACE_COUNT; ++face) {
        definition = &state->faces[face];
        for (corner = 0; corner < 4; ++corner)
            for (axis = 0; axis < 3; ++axis)
                positions[corner][axis] = (float) (maage_interaction_corners[face][corner][axis] ? maximum[axis] : minimum[axis]);
        maage_client_item_face_tint(grass_color, definition, definition->tint_mode, tint);
        maage_item_model_quad(vertices, indices, faces, positions, definition->uv,
                              tint, shade[face], material, definition->animation);
        if (definition->overlay_uv[2] > definition->overlay_uv[0]) {
            /* Separate biased layer retains dirt RGB below the colored fringe. */
            axis = face / 2;
            for (corner = 0; corner < 4; ++corner)
                positions[corner][axis] += face % 2 ? 0.0002f : -0.0002f;
            maage_client_item_face_tint(grass_color, definition, definition->overlay_tint_mode, tint);
            maage_item_model_quad(vertices, indices, faces, positions,
                                  definition->overlay_uv, tint, shade[face], MAAGE_MESH_MATERIAL_CUTOUT, NULL);
        }
    }
}
static void maage_item_model_sprite(maage_mesh_vertex *vertices, unsigned int *indices,
                                    size_t *faces, const float uv[4], const float tint[4]) {
    float positions[4][3], rectangle[4], coordinate, sample;
    unsigned int face, corner, axis, pixel;
    /* Front/back plus one texture-sampled edge per pixel row/column, matching
     * the recognizable thin item silhouette without turning plants into cubes. */
    for (face = 4; face < 6; ++face) {
        for (corner = 0; corner < 4; ++corner) {
            for (axis = 0; axis < 3; ++axis)
                positions[corner][axis] = (float) maage_interaction_corners[face][corner][axis];
            positions[corner][2] = face == 4 ? -0.0625f : 0;
        }
        memcpy(rectangle, uv, sizeof(rectangle));
        if (face == MAAGE_FACE_POSITIVE_Z) {
            rectangle[0] = uv[2];
            rectangle[2] = uv[0];
        }
        maage_item_model_quad(vertices, indices, faces, positions, rectangle, tint,
                              face == 4 ? 0.8f : 1.0f, MAAGE_MESH_MATERIAL_CUTOUT, NULL);
    }
    for (face = 0; face < 4; ++face)
        for (pixel = 0; pixel < 16; ++pixel) {
            axis = face / 2;
            coordinate = (float) (pixel + face % 2) / 16;
            sample = ((float) pixel + 0.5f) / 16;
            memcpy(rectangle, uv, sizeof(rectangle));
            if (axis == 0)
                rectangle[0] = rectangle[2] = uv[2] + (uv[0] - uv[2]) * sample;
            else {
                rectangle[1] = rectangle[3] = uv[3] + (uv[1] - uv[3]) * sample;
                rectangle[0] = uv[2];
                rectangle[2] = uv[0];
            }
            for (corner = 0; corner < 4; ++corner) {
                positions[corner][0] = (float) maage_interaction_corners[face][corner][0];
                positions[corner][1] = (float) maage_interaction_corners[face][corner][1];
                positions[corner][2] = maage_interaction_corners[face][corner][2] ? 0 : -0.0625f;
                positions[corner][axis] = coordinate;
            }
            maage_item_model_quad(vertices, indices, faces, positions, rectangle,
                                  tint, 0.75f, MAAGE_MESH_MATERIAL_CUTOUT, NULL);
        }
}

maage_status maage_client_item_model(const maage_client_resources *resources,
                                     const maage_item_registry *items, const maage_block_registry *blocks,
                                     const maage_item_stack *stack, maage_mesh_vertex *vertices, unsigned int *indices,
                                     maage_mesh_data *data) {
    const maage_item_definition *item;
    const maage_block_state_definition *state;
    const maage_block_definition *block;
    maage_texture_id texture;
    size_t faces = 0;
    unsigned int axis, i;
    float tint[4] = {1, 1, 1, 1}, material;
    double minimum[3], maximum[3];
    if (!resources || !stack || !vertices || !indices || !data ||
        !(item = maage_item_registry_get(items, stack->item_id)))
        return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_LOOKUP, 0);
    state = item->has_block ? maage_client_item_icon_state(blocks, item, stack->metadata) : NULL;
    block = state ? maage_block_registry_block(blocks, state->block_id) : NULL;
    if (item->model == MAAGE_ITEM_MODEL_SPRITE) {
        texture = maage_client_item_texture(item, stack->metadata);
        if (texture >= MAAGE_CONTENT_TEXTURE_COUNT) return maage_status_make(MAAGE_STATUS_NOT_FOUND,
                                                                             MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_LOOKUP, 0);
        if (state) maage_client_item_face_tint(resources->inventory_grass_color,
                                               &state->faces[MAAGE_FACE_POSITIVE_Z], state->faces[MAAGE_FACE_POSITIVE_Z].tint_mode, tint);
        maage_item_model_sprite(vertices, indices, &faces, resources->uv[texture], tint);
        /* Center the shared sprite. Contexts apply their own transforms. */
        for (i = 0; i < faces * 4; ++i) {
            vertices[i].position[0] -= 0.5f;
            vertices[i].position[1] -= 0.5f;
        }
    } else {
        if (!block) return maage_status_make(MAAGE_STATUS_NOT_FOUND, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_LOOKUP, 0);
        for (axis = 0; axis < 3; ++axis) {
            minimum[axis] = block->selection_shape.minimum[axis] - 0.5;
            maximum[axis] = block->selection_shape.maximum[axis] - 0.5;
        }
        material = block->opaque ? MAAGE_MESH_MATERIAL_OPAQUE : MAAGE_MESH_MATERIAL_CUTOUT;
        if (block->id == MAAGE_BLOCK_ICE) material = MAAGE_MESH_MATERIAL_TRANSLUCENT;
        maage_client_item_box(resources->inventory_grass_color, vertices, indices, &faces,
                              minimum, maximum, state, material);
    }
    data->vertices = vertices;
    data->indices = indices;
    data->vertex_count = faces * 4;
    data->index_count = faces * 6;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_CREATE, 0);
}

size_t maage_client_item_icon(const maage_item_registry *items,
                              const maage_block_registry *blocks, const maage_item_stack *stack,
                              const float grass[3], const float textures[MAAGE_CONTENT_TEXTURE_COUNT][4],
                              maage_render_ui_vertex vertices[24]) {
    static const unsigned int visible[3] = {MAAGE_FACE_NEGATIVE_X, MAAGE_FACE_POSITIVE_Z, MAAGE_FACE_POSITIVE_Y};
    /* GUI light rig: ambient 0.4 plus two diffuse 0.6 lights,
     * evaluated for the orthographic item transform's visible face normals. */
    static const float shade[3] = {0.637f, 0.435f, 1};
    const maage_item_definition *item = maage_item_registry_get(items, stack->item_id);
    const maage_block_state_definition *state;
    const maage_block_definition *block;
    const maage_block_face_definition *face;
    maage_render_ui_vertex *vertex;
    const float *uv;
    float tint[4];
    double position[3], sample_u, sample_v;
    size_t faces = 0;
    unsigned int i, corner, axis, layer;
    maage_texture_id texture;
    if (!item) return 0;
    state = item->has_block ? maage_client_item_icon_state(blocks, item, stack->metadata) : NULL;
    block = state ? maage_block_registry_block(blocks, state->block_id) : NULL;
    if (item->model == MAAGE_ITEM_MODEL_SPRITE) {
        texture = maage_client_item_texture(item, stack->metadata);
        if (texture >= MAAGE_CONTENT_TEXTURE_COUNT) return 0;
        for (axis = 0; axis < 4; ++axis)
            tint[axis] = 1;
        if (state) maage_client_item_face_tint(grass, &state->faces[MAAGE_FACE_POSITIVE_Z],
                                               state->faces[MAAGE_FACE_POSITIVE_Z].tint_mode, tint);
        uv = textures[texture];
        for (corner = 0; corner < 4; ++corner) {
            vertex = &vertices[corner];
            vertex->position[0] = corner == 1 || corner == 2 ? 16 : 0;
            vertex->position[1] = corner < 2 ? 0 : 16;
            vertex->uv[0] = uv[corner == 1 || corner == 2 ? 2 : 0];
            vertex->uv[1] = uv[corner < 2 ? 1 : 3];
            memcpy(vertex->color, tint, sizeof(tint));
        }
        return 1;
    }
    if (!state || !block) return 0;
    for (i = 0; i < 3; ++i) {
        face = &state->faces[visible[i]];
        for (layer = 0; layer < 2; ++layer) {
            if (layer && !(face->overlay_uv[2] > face->overlay_uv[0])) continue;
            uv = layer ? face->overlay_uv : face->uv;
            maage_client_item_face_tint(grass, face, layer ? face->overlay_tint_mode : face->tint_mode, tint);
            for (corner = 0; corner < 4; ++corner) {
                vertex = &vertices[faces * 4 + corner];
                for (axis = 0; axis < 3; ++axis)
                    position[axis] = (maage_interaction_corners[visible[i]][corner][axis] ? block->selection_shape.maximum[axis] : block->selection_shape.minimum[axis]) - 0.5;
                /* Scale 10 and X=210 / Y=45 rotations project centered item
                 * bounds into GUI coordinates.
                 * Full cubes occupy x=0.929..15.071, y=0.134..15.866. */
                vertex->position[0] = (float) ((position[0] + position[2]) * 7.071067812 + 8);
                vertex->position[1] = (float) ((position[2] - position[0]) * 3.535533906 - position[1] * 8.660254038 + 8);
                if (visible[i] == MAAGE_FACE_POSITIVE_Y) {
                    sample_u = position[0] + 0.5;
                    sample_v = position[2] + 0.5;
                } else {
                    sample_u = position[visible[i] == MAAGE_FACE_NEGATIVE_X ? 2 : 0] + 0.5;
                    sample_v = 0.5 - position[1];
                }
                vertex->uv[0] = (float) (uv[0] + (uv[2] - uv[0]) * sample_u);
                vertex->uv[1] = (float) (uv[1] + (uv[3] - uv[1]) * sample_v);
                for (axis = 0; axis < 3; ++axis)
                    vertex->color[axis] = tint[axis] * shade[i];
                vertex->color[3] = tint[3];
            }
            ++faces;
        }
    }
    return faces;
}
