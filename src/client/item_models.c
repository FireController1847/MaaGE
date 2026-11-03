#include "item_models.h"
#include "maage/models.h"
#include "maage/internal/registry_pages.h"
#include "player_model.h"
#include "maage/block_geometry.h"
#include <limits.h>
#include <float.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

static const int maage_interaction_corners[6][4][3] = {
    {{0, 0, 0}, {0, 0, 1}, {0, 1, 1}, {0, 1, 0}},
    {{1, 0, 1}, {1, 0, 0}, {1, 1, 0}, {1, 1, 1}},
    {{0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1}},
    {{0, 1, 1}, {1, 1, 1}, {1, 1, 0}, {0, 1, 0}},
    {{1, 0, 0}, {0, 0, 0}, {0, 1, 0}, {1, 1, 0}},
    {{0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}}};

static void maage_item_model_quad(maage_mesh_vertex *vertices, unsigned int *indices, size_t *faces, const float positions[4][3], const float uv[4], const float tint[4], float shade, float material, const float *animation) {
    static const unsigned int offsets[6] = {0, 1, 2, 0, 2, 3};
    maage_mesh_vertex *vertex;
    unsigned int corner, axis, base = (unsigned int) (*faces * 4);
    if (!vertices) {
        ++*faces;
        return;
    }
    for (corner = 0; corner < 4; ++corner) {
        vertex = &vertices[base + corner];
        memset(vertex, 0, sizeof(*vertex));
        for (axis = 0; axis < 3; ++axis) {
            vertex->position[axis] = positions[corner][axis];
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
void maage_client_item_box(const float grass_color[3], maage_mesh_vertex *vertices, unsigned int *indices, size_t *faces, const double minimum[3], const double maximum[3], const maage_block_state_definition *state, float material) {
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
        maage_item_model_quad(vertices, indices, faces, positions, definition->uv, tint, shade[face], material, definition->animation);
        if (definition->overlay_uv[2] > definition->overlay_uv[0]) {
            /* Separate biased layer retains dirt RGB below the colored fringe. */
            axis = face / 2;
            for (corner = 0; corner < 4; ++corner)
                positions[corner][axis] += face % 2 ? 0.0002f : -0.0002f;
            maage_client_item_face_tint(grass_color, definition, definition->overlay_tint_mode, tint);
            maage_item_model_quad(vertices, indices, faces, positions, definition->overlay_uv, tint, shade[face], MAAGE_MESH_MATERIAL_CUTOUT, NULL);
        }
    }
}
static void maage_item_model_sprite(maage_mesh_vertex *vertices, unsigned int *indices, size_t *faces, const float uv[4], const float tint[4], float bias) {
    float positions[4][3], rectangle[4], coordinate, sample;
    unsigned int face, corner, axis, pixel;
    /* Front/back plus one texture-sampled edge per pixel row/column, matching
     * the recognizable thin item silhouette without turning plants into cubes. */
    for (face = 4; face < 6; ++face) {
        for (corner = 0; corner < 4; ++corner) {
            for (axis = 0; axis < 3; ++axis)
                positions[corner][axis] = (float) maage_interaction_corners[face][corner][axis];
            positions[corner][2] = face == 4 ? -0.0625f - bias : bias;
        }
        memcpy(rectangle, uv, sizeof(rectangle));
        if (face == MAAGE_FACE_POSITIVE_Z) {
            rectangle[0] = uv[2];
            rectangle[2] = uv[0];
        }
        maage_item_model_quad(vertices, indices, faces, positions, rectangle, tint, face == 4 ? 0.8f : 1.0f, MAAGE_MESH_MATERIAL_CUTOUT, NULL);
    }
    for (face = 0; face < 4; ++face)
        for (pixel = 0; pixel < 16; ++pixel) {
            axis = face / 2;
            coordinate = (float) (pixel + face % 2) / 16;
            sample = ((float) pixel + 0.5f) / 16;
            memcpy(rectangle, uv, sizeof(rectangle));
            if (axis == 0) rectangle[0] = rectangle[2] = uv[2] + (uv[0] - uv[2]) * sample;
            else {
                rectangle[1] = rectangle[3] = uv[3] + (uv[1] - uv[3]) * sample;
                rectangle[0] = uv[2];
                rectangle[2] = uv[0];
            }
            for (corner = 0; corner < 4; ++corner) {
                positions[corner][0] = (float) maage_interaction_corners[face][corner][0];
                positions[corner][1] = (float) maage_interaction_corners[face][corner][1];
                positions[corner][2] = maage_interaction_corners[face][corner][2] ? bias : -0.0625f - bias;
                positions[corner][axis] = coordinate + (face & 1U ? bias : -bias);
            }
            maage_item_model_quad(vertices, indices, faces, positions, rectangle, tint, 0.75f, MAAGE_MESH_MATERIAL_CUTOUT, NULL);
        }
}

typedef struct maage_item_geometry_work {
    const maage_block_definition *block;
    const maage_block_state_definition *state;
    const float *grass;
    maage_mesh_vertex *vertices;
    unsigned int *indices;
    size_t count, capacity;
    maage_status result;
} maage_item_geometry_work;
static maage_status maage_item_geometry_quad_write(maage_item_geometry_work *work, const maage_block_geometry_quad *quad) {
    static const unsigned int winding[6] = {0, 1, 2, 0, 2, 3};
    static const float shade[6] = {0.6f, 0.6f, 0.5f, 1, 0.8f, 0.8f};
    const maage_block_face_definition *appearance;
    maage_mesh_vertex *vertex;
    const float *uv;
    maage_status result;
    size_t face, amount;
    unsigned int corner, axis, layer, side, base, layers;
    float material, tint[4], bias;
    result = maage_block_geometry_quad_validate(quad);
    if (maage_status_failed(result)) return result;
    appearance = quad->appearance ? quad->appearance : &work->state->faces[quad->face];
    if (appearance->tint_mode > MAAGE_BLOCK_TINT_GRASS_LOCAL || appearance->overlay_tint_mode > MAAGE_BLOCK_TINT_GRASS_LOCAL) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_MESH, 0);
    layers = appearance->overlay_uv[2] > appearance->overlay_uv[0] ? 2U : 1U;
    amount = layers * (quad->double_sided ? 2U : 1U);
    if (work->count > ((size_t) - 1) / 6 - amount || work->count > UINT_MAX / 4U - amount || (work->vertices && (work->count > work->capacity || amount > work->capacity - work->count))) return maage_status_make(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_MESH, 0);
    face = work->count;
    work->count += amount;
    if (!work->vertices) return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_MESH, 0);
    for (layer = 0; layer < layers; ++layer) {
        uv = layer ? appearance->overlay_uv : appearance->uv;
        material = layer ? MAAGE_MESH_MATERIAL_CUTOUT : (quad->material == MAAGE_BLOCK_GEOMETRY_INHERIT_MATERIAL ? (float) work->block->mesh_material : (float) quad->material);
        maage_client_item_face_tint(work->grass, appearance, layer ? appearance->overlay_tint_mode : appearance->tint_mode, tint);
        bias = layer ? (quad->face & 1U ? 0.0002f : -0.0002f) : 0;
        for (side = 0; side < (quad->double_sided ? 2U : 1U); ++side, ++face) {
            base = (unsigned int) (face * 4);
            for (corner = 0; corner < 4; ++corner) {
                vertex = &work->vertices[face * 4 + corner];
                memset(vertex, 0, sizeof(*vertex));
                for (axis = 0; axis < 3; ++axis) {
                    vertex->position[axis] = quad->positions[corner][axis] - 0.5f + (axis == quad->face / 2 ? bias : 0);
                    vertex->color[axis] = tint[axis] * shade[quad->face];
                    if (!layer) vertex->animation[axis] = appearance->animation[axis];
                }
                vertex->color[3] = tint[3];
                vertex->light[0] = 1;
                vertex->material = material;
                vertex->uv[0] = uv[0] + quad->uv[corner][0] * (uv[2] - uv[0]);
                vertex->uv[1] = uv[1] + quad->uv[corner][1] * (uv[3] - uv[1]);
            }
            for (corner = 0; corner < 6; ++corner)
                work->indices[face * 6 + corner] = base + winding[side ? 5 - corner : corner];
        }
    }
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_MESH, 0);
}
static maage_status maage_item_geometry_quad(void *opaque, const maage_block_geometry_quad *quad) {
    maage_item_geometry_work *work = (maage_item_geometry_work *) opaque;
    maage_status result;
    if (maage_status_failed(work->result)) return work->result;
    result = maage_item_geometry_quad_write(work, quad);
    if (maage_status_failed(result)) work->result = result;
    return result;
}
typedef struct maage_registered_item_work {
    const maage_item_definition *item;
    const maage_model_definition *model;
    const float (*textures)[4];
    size_t texture_count;
    maage_texture_id texture;
    float matrix[16];
    maage_mesh_vertex *vertices;
    unsigned int *indices;
    size_t faces, capacity;
    maage_client_item_icon_sink icon;
    void *context;
    maage_status failure;
} maage_registered_item_work;
static void maage_registered_item_matrix(const maage_item_definition *item, float matrix[16]) {
    unsigned int axis;
    maage_player_matrix_identity(matrix);
    maage_player_matrix_translate(matrix, item->presentation.translation[0] - 0.5, item->presentation.translation[1] - 0.5, item->presentation.translation[2] - 0.5);
    for (axis = 0; axis < 3; ++axis)
        maage_player_matrix_rotate(matrix, axis, item->presentation.rotation[axis]);
    maage_player_matrix_scale(matrix, item->presentation.scale ? item->presentation.scale : 1, item->presentation.scale ? item->presentation.scale : 1, item->presentation.scale ? item->presentation.scale : 1);
}
static maage_status maage_registered_item_quad(void *opaque, const maage_model_quad *quad) {
    static const unsigned int winding[6] = {0, 1, 2, 0, 2, 3};
    maage_registered_item_work *work = (maage_registered_item_work *) opaque;
    maage_render_ui_vertex icon[4];
    maage_mesh_vertex *vertex;
    const float *uv;
    double positions[4][3], a[3], b[3], normal[3], length, shade;
    unsigned int corner, axis, k, first, second, base;
    maage_status result;
    if (maage_status_failed(work->failure)) return work->failure;
    if (!quad || quad->face >= 6 || quad->material > MAAGE_MESH_MATERIAL_TRANSLUCENT || work->texture >= work->texture_count) return work->failure = maage_status_make(MAAGE_STATUS_MALFORMED_INPUT, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_MESH, 0);
    for (corner = 0; corner < 4; ++corner) {
        for (axis = 0; axis < 3; ++axis)
            if (!(quad->positions[corner][axis] >= -FLT_MAX && quad->positions[corner][axis] <= FLT_MAX)) return work->failure = maage_status_make(MAAGE_STATUS_MALFORMED_INPUT, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_MESH, 0);
        for (axis = 0; axis < 2; ++axis)
            if (!(quad->uv[corner][axis] >= 0 && quad->uv[corner][axis] <= 1)) return work->failure = maage_status_make(MAAGE_STATUS_MALFORMED_INPUT, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_MESH, 0);
    }
    for (corner = 0; corner < 4; ++corner)
        for (axis = 0; axis < 3; ++axis) {
            positions[corner][axis] = work->matrix[12 + axis];
            for (k = 0; k < 3; ++k)
                positions[corner][axis] += work->matrix[k * 4 + axis] * quad->positions[corner][k];
            if (!(positions[corner][axis] >= -FLT_MAX && positions[corner][axis] <= FLT_MAX)) return work->failure = maage_status_make(MAAGE_STATUS_MALFORMED_INPUT, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_MESH, 0);
        }
    for (axis = 0; axis < 3; ++axis) {
        a[axis] = positions[1][axis] - positions[0][axis];
        b[axis] = positions[2][axis] - positions[0][axis];
    }
    length = 0;
    for (axis = 0; axis < 3; ++axis) {
        first = (axis + 1) % 3;
        second = (axis + 2) % 3;
        normal[axis] = a[first] * b[second] - a[second] * b[first];
        length += normal[axis] * normal[axis];
    }
    if (!(length > 0)) return work->failure = maage_status_make(MAAGE_STATUS_MALFORMED_INPUT, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_MESH, 0);
    length = sqrt(length);
    for (axis = 0; axis < 3; ++axis)
        normal[axis] /= length;
    shade = 0.5 + 0.5 * (normal[1] > 0 ? normal[1] : 0) + 0.1 * fabs(normal[0]) + 0.3 * fabs(normal[2]);
    uv = work->textures[work->texture];
    if (work->icon) {
        if (-normal[0] + normal[1] + normal[2] <= 0) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_PRESENT, 0);
        shade = 0.5 + 0.5 * (normal[1] > 0 ? normal[1] : 0) + 0.137 * fabs(normal[0]) - 0.065 * fabs(normal[2]);
        for (corner = 0; corner < 4; ++corner) {
            icon[corner].position[0] = (float) ((positions[corner][0] + positions[corner][2]) * 7.071067812 + 8);
            icon[corner].position[1] = (float) ((positions[corner][2] - positions[corner][0]) * 3.535533906 - positions[corner][1] * 8.660254038 + 8);
            for (axis = 0; axis < 2; ++axis)
                icon[corner].uv[axis] = uv[axis] + quad->uv[corner][axis] * (uv[axis + 2] - uv[axis]);
            for (axis = 0; axis < 3; ++axis)
                icon[corner].color[axis] = (float) shade;
            icon[corner].color[3] = 1;
        }
        result = work->icon(work->context, icon);
        if (maage_status_failed(result)) work->failure = result;
        return result;
    }
    if (work->faces >= UINT_MAX / 4U || work->faces >= (size_t) - 1 / 6 || (work->vertices && work->faces >= work->capacity)) return work->failure = maage_status_make(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_MESH, 0);
    if (!work->vertices) {
        ++work->faces;
        return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_MESH, 0);
    }
    base = (unsigned int) (work->faces * 4);
    for (corner = 0; corner < 4; ++corner) {
        vertex = &work->vertices[work->faces * 4 + corner];
        memset(vertex, 0, sizeof(*vertex));
        for (axis = 0; axis < 3; ++axis) {
            vertex->position[axis] = (float) positions[corner][axis];
            vertex->color[axis] = (float) shade;
        }
        for (axis = 0; axis < 2; ++axis)
            vertex->uv[axis] = uv[axis] + quad->uv[corner][axis] * (uv[axis + 2] - uv[axis]);
        vertex->color[3] = 1;
        vertex->material = (float) quad->material;
        vertex->light[0] = 1;
    }
    for (corner = 0; corner < 6; ++corner)
        work->indices[work->faces * 6 + corner] = base + winding[corner];
    ++work->faces;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_MESH, 0);
}
static maage_status maage_registered_item_emit(const maage_model_registry *models, maage_registered_item_work *work) {
    maage_model_sink sink;
    maage_status result;
    float transform[16], part_matrix[16];
    size_t part;
    int visible;
    work->model = maage_model_registry_get(models, work->item->registered_model);
    if (!work->model) return maage_status_make(MAAGE_STATUS_NOT_FOUND, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_MESH, 0);
    maage_registered_item_matrix(work->item, transform);
    maage_player_matrix_scale(transform, work->model->scale, work->model->scale, work->model->scale);
    sink.quad = maage_registered_item_quad;
    sink.context = work;
    for (part = 0; part < work->model->part_count; ++part) {
        work->texture = work->model->parts[part].texture_id == MAAGE_CONTENT_ID_INVALID ? work->model->texture_id : work->model->parts[part].texture_id;
        if (work->texture >= work->texture_count) return maage_status_make(MAAGE_STATUS_NOT_FOUND, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_MESH, 0);
        result = maage_model_part_matrix(work->model, NULL, part, part_matrix, &visible);
        if (maage_status_failed(result)) return result;
        maage_player_matrix_multiply(work->matrix, transform, part_matrix);
        if (!visible) continue;
        result = maage_model_emit_part(work->model, part, &sink);
        if (maage_status_failed(work->failure)) return work->failure;
        if (maage_status_failed(result)) return result;
    }
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_MESH, 0);
}
static maage_status maage_item_model_generate(const maage_client_resources *resources, const maage_item_registry *items, const maage_block_registry *blocks, const maage_item_stack *stack, maage_mesh_vertex *vertices, unsigned int *indices, size_t capacity, maage_mesh_data *data) {
    const maage_item_definition *item;
    const maage_block_state_definition *state;
    const maage_block_definition *block;
    const maage_block_geometry_definition *geometry;
    maage_block_geometry_query query;
    maage_block_geometry_sink sink;
    maage_item_geometry_work work;
    maage_registered_item_work registered;
    maage_status result;
    maage_texture_id texture;
    size_t faces = 0, i, layer;
    unsigned int axis;
    float tint[4] = {1, 1, 1, 1}, material;
    double minimum[3], maximum[3];
    if (!resources || !stack || !data || !(item = maage_item_registry_get(items, stack->item_id))) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_LOOKUP, 0);
    state = item->has_block ? maage_client_item_icon_state(blocks, item, stack->metadata) : NULL;
    block = state ? maage_block_registry_block(blocks, state->block_id) : NULL;
    if (item->model == MAAGE_ITEM_MODEL_REGISTERED) {
        memset(&registered, 0, sizeof(registered));
        registered.item = item;
        registered.textures = resources->uv;
        registered.texture_count = resources->texture_count;
        registered.vertices = vertices;
        registered.indices = indices;
        registered.capacity = capacity;
        result = maage_registered_item_emit(maage_content_models(resources->content), &registered);
        if (maage_status_failed(result)) return result;
        faces = registered.faces;
    } else if (item->model == MAAGE_ITEM_MODEL_SPRITE) {
        if (item->sprite_layer_count > (size_t) - 1 / 66 || (vertices && capacity < 66 * (item->sprite_layer_count ? item->sprite_layer_count : 1))) return maage_status_make(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_MESH, 0);
        for (layer = 0; layer < (item->sprite_layer_count ? item->sprite_layer_count : 1); ++layer) {
            texture = item->sprite_layer_count ? item->sprite_layers[layer].texture : maage_client_item_texture(item, stack->metadata);
            if (texture >= resources->texture_count) return maage_status_make(MAAGE_STATUS_NOT_FOUND, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_LOOKUP, 0);
            if (item->sprite_layer_count) memcpy(tint, item->sprite_layers[layer].tint, sizeof(tint));
            else if (state) maage_client_item_face_tint(resources->inventory_grass_color, &state->faces[MAAGE_FACE_POSITIVE_Z], state->faces[MAAGE_FACE_POSITIVE_Z].tint_mode, tint);
            maage_item_model_sprite(vertices, indices, &faces, resources->uv[texture], tint, (float) layer * 0.0002f);
        }
        /* Center the shared sprite. Contexts apply their own transforms. */
        if (vertices)
            for (i = 0; i < faces * 4; ++i) {
                vertices[i].position[0] -= 0.5f;
                vertices[i].position[1] -= 0.5f;
            }
    } else {
        if (!block || !(geometry = maage_block_geometry_get(maage_block_registry_geometry(blocks), block->geometry_id))) return maage_status_make(MAAGE_STATUS_NOT_FOUND, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_LOOKUP, 0);
        if (geometry->primitive == MAAGE_BLOCK_GEOMETRY_CUSTOM) {
            memset(&work, 0, sizeof(work));
            work.block = block;
            work.state = state;
            work.grass = resources->inventory_grass_color;
            work.vertices = vertices;
            work.indices = indices;
            work.capacity = capacity;
            memset(&query, 0, sizeof(query));
            query.blocks = blocks;
            query.block = block;
            query.state = state;
            query.geometry = geometry;
            query.item = 1;
            sink.quad = maage_item_geometry_quad;
            sink.context = &work;
            result = geometry->emit(geometry->context, &query, &sink);
            if (maage_status_failed(work.result)) return work.result;
            if (maage_status_failed(result)) return result;
            faces = work.count;
        } else {
            for (axis = 0; axis < MAAGE_FACE_COUNT; ++axis)
                faces += state->faces[axis].overlay_uv[2] > state->faces[axis].overlay_uv[0] ? 2U : 1U;
            if (vertices && capacity < faces) return maage_status_make(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_MESH, 0);
            faces = 0;
            for (axis = 0; axis < 3; ++axis) {
                minimum[axis] = block->selection_shape.minimum[axis] - 0.5;
                maximum[axis] = block->selection_shape.maximum[axis] - 0.5;
            }
            material = block->opaque ? MAAGE_MESH_MATERIAL_OPAQUE : MAAGE_MESH_MATERIAL_CUTOUT;
            if (block->mesh_material == MAAGE_MESH_MATERIAL_TRANSLUCENT) material = MAAGE_MESH_MATERIAL_TRANSLUCENT;
            maage_client_item_box(resources->inventory_grass_color, vertices, indices, &faces, minimum, maximum, state, material);
        }
    }
    data->vertices = vertices;
    data->indices = indices;
    data->vertex_count = faces * 4;
    data->index_count = faces * 6;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_CREATE, 0);
}
maage_status maage_client_item_model_size(const maage_client_resources *resources, const maage_item_registry *items, const maage_block_registry *blocks, const maage_item_stack *stack, size_t *faces) {
    maage_mesh_data data;
    maage_status result;
    if (!faces) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_MESH, 0);
    *faces = 0;
    result = maage_item_model_generate(resources, items, blocks, stack, NULL, NULL, 0, &data);
    if (!maage_status_failed(result)) *faces = data.index_count / 6;
    return result;
}
maage_status maage_client_item_model(const maage_client_resources *resources, const maage_item_registry *items, const maage_block_registry *blocks, const maage_item_stack *stack, maage_mesh_vertex *vertices, unsigned int *indices, size_t face_capacity, maage_mesh_data *data) {
    if (!vertices || !indices || !data) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_MESH, 0);
    memset(data, 0, sizeof(*data));
    return maage_item_model_generate(resources, items, blocks, stack, vertices, indices, face_capacity, data);
}
maage_status maage_client_item_model_create(const maage_client_resources *resources, const maage_item_registry *items, const maage_block_registry *blocks, const maage_item_stack *stack, maage_mesh_data *data) {
    maage_mesh_vertex *vertices;
    unsigned int *indices;
    size_t faces;
    maage_status result;
    if (!data) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_CREATE, 0);
    memset(data, 0, sizeof(*data));
    result = maage_client_item_model_size(resources, items, blocks, stack, &faces);
    if (maage_status_failed(result)) return result;
    if (faces > UINT_MAX / 4U || faces > (size_t) - 1 / (4 * sizeof(*vertices)) || faces > (size_t) - 1 / (6 * sizeof(*indices))) return maage_status_make(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_CREATE, 0);
    if (!faces) return result;
    vertices = (maage_mesh_vertex *) malloc(faces * 4 * sizeof(*vertices));
    indices = (unsigned int *) malloc(faces * 6 * sizeof(*indices));
    if (!vertices || !indices) {
        free(vertices);
        free(indices);
        return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_CREATE, 0);
    }
    result = maage_item_model_generate(resources, items, blocks, stack, vertices, indices, faces, data);
    if (!maage_status_failed(result) && data->index_count != faces * 6) result = maage_status_make(MAAGE_STATUS_INVARIANT_VIOLATION, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_MESH, 0);
    if (maage_status_failed(result)) {
        free(vertices);
        free(indices);
        memset(data, 0, sizeof(*data));
    }
    return result;
}
maage_status maage_client_item_model_destroy(maage_mesh_data *data) {
    if (!data) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_DESTROY, 0);
    free((void *) data->vertices);
    free((void *) data->indices);
    memset(data, 0, sizeof(*data));
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_DESTROY, 0);
}

typedef struct maage_item_icon_work {
    const maage_block_state_definition *state;
    const float *grass;
    maage_client_item_icon_sink sink;
    void *context;
    maage_status result;
} maage_item_icon_work;
static maage_status maage_item_icon_quad_write(maage_item_icon_work *work, const maage_block_geometry_quad *quad) {
    static const float shade[6] = {0.637f, 0.637f, 0.5f, 1, 0.435f, 0.435f};
    const maage_block_face_definition *appearance;
    maage_render_ui_vertex vertices[4];
    maage_status result;
    const float *uv;
    float tint[4];
    double position[3], normal[3], edge_a[3], edge_b[3], facing;
    unsigned int corner, axis, layer, first, second;
    result = maage_block_geometry_quad_validate(quad);
    if (maage_status_failed(result)) return result;
    /* Project the actual polygon normal so slanted/custom faces retain the
     * same visibility convention as the optimized cube's three visible sides. */
    for (axis = 0; axis < 3; ++axis) {
        edge_a[axis] = quad->positions[1][axis] - quad->positions[0][axis];
        edge_b[axis] = quad->positions[2][axis] - quad->positions[0][axis];
    }
    for (axis = 0; axis < 3; ++axis) {
        first = (axis + 1) % 3;
        second = (axis + 2) % 3;
        normal[axis] = edge_a[first] * edge_b[second] - edge_a[second] * edge_b[first];
    }
    facing = -normal[0] + normal[1] + normal[2];
    if (facing <= 0 && !quad->double_sided) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_PRESENT, 0);
    appearance = quad->appearance ? quad->appearance : &work->state->faces[quad->face];
    if (appearance->tint_mode > MAAGE_BLOCK_TINT_GRASS_LOCAL || appearance->overlay_tint_mode > MAAGE_BLOCK_TINT_GRASS_LOCAL) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_PRESENT, 0);
    for (layer = 0; layer < 2; ++layer) {
        if (layer && !(appearance->overlay_uv[2] > appearance->overlay_uv[0])) continue;
        uv = layer ? appearance->overlay_uv : appearance->uv;
        maage_client_item_face_tint(work->grass, appearance, layer ? appearance->overlay_tint_mode : appearance->tint_mode, tint);
        for (corner = 0; corner < 4; ++corner) {
            for (axis = 0; axis < 3; ++axis)
                position[axis] = quad->positions[corner][axis] - 0.5;
            vertices[corner].position[0] = (float) ((position[0] + position[2]) * 7.071067812 + 8);
            vertices[corner].position[1] = (float) ((position[2] - position[0]) * 3.535533906 - position[1] * 8.660254038 + 8);
            vertices[corner].uv[0] = uv[0] + quad->uv[corner][0] * (uv[2] - uv[0]);
            vertices[corner].uv[1] = uv[1] + quad->uv[corner][1] * (uv[3] - uv[1]);
            for (axis = 0; axis < 3; ++axis)
                vertices[corner].color[axis] = tint[axis] * shade[quad->face];
            vertices[corner].color[3] = tint[3];
        }
        result = work->sink(work->context, vertices);
        if (maage_status_failed(result)) return result;
    }
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_PRESENT, 0);
}
static maage_status maage_item_icon_quad(void *opaque, const maage_block_geometry_quad *quad) {
    maage_item_icon_work *work = (maage_item_icon_work *) opaque;
    maage_status result;
    if (maage_status_failed(work->result)) return work->result;
    result = maage_item_icon_quad_write(work, quad);
    if (maage_status_failed(result)) work->result = result;
    return result;
}
maage_status maage_client_item_icon(const maage_model_registry *models, const maage_item_registry *items, const maage_block_registry *blocks, const maage_item_stack *stack, const float grass[3], const float (*textures)[4], size_t texture_count, maage_client_item_icon_sink sink, void *context) {
    static const unsigned int visible[3] = {MAAGE_FACE_NEGATIVE_X, MAAGE_FACE_POSITIVE_Z, MAAGE_FACE_POSITIVE_Y};
    /* GUI light rig: ambient 0.4 plus two diffuse 0.6 lights,
     * evaluated for the orthographic item transform's visible face normals. */
    static const float shade[3] = {0.637f, 0.435f, 1};
    const maage_item_definition *item;
    const maage_block_state_definition *state;
    const maage_block_definition *block;
    const maage_block_face_definition *face;
    const maage_block_geometry_definition *geometry;
    maage_block_geometry_query query;
    maage_block_geometry_sink geometry_sink;
    maage_item_icon_work work;
    maage_registered_item_work registered;
    maage_render_ui_vertex vertices[4], *vertex;
    maage_status result;
    const float *uv;
    float tint[4];
    double position[3], sample_u, sample_v;
    size_t i;
    unsigned int corner, axis, layer;
    maage_texture_id texture;
    if (!stack || !grass || !textures || !sink || !(item = maage_item_registry_get(items, stack->item_id))) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_PRESENT, 0);
    state = item->has_block ? maage_client_item_icon_state(blocks, item, stack->metadata) : NULL;
    block = state ? maage_block_registry_block(blocks, state->block_id) : NULL;
    if (item->model == MAAGE_ITEM_MODEL_REGISTERED) {
        memset(&registered, 0, sizeof(registered));
        registered.item = item;
        registered.textures = textures;
        registered.texture_count = texture_count;
        registered.icon = sink;
        registered.context = context;
        return maage_registered_item_emit(models, &registered);
    }
    if (item->model == MAAGE_ITEM_MODEL_SPRITE) {
        for (i = 0; i < (item->sprite_layer_count ? item->sprite_layer_count : 1); ++i) {
            texture = item->sprite_layer_count ? item->sprite_layers[i].texture : maage_client_item_texture(item, stack->metadata);
            if (texture >= texture_count) return maage_status_make(MAAGE_STATUS_NOT_FOUND, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_PRESENT, 0);
            for (axis = 0; axis < 4; ++axis)
                tint[axis] = item->sprite_layer_count ? item->sprite_layers[i].tint[axis] : 1;
            if (!item->sprite_layer_count && state) maage_client_item_face_tint(grass, &state->faces[MAAGE_FACE_POSITIVE_Z], state->faces[MAAGE_FACE_POSITIVE_Z].tint_mode, tint);
            uv = textures[texture];
            for (corner = 0; corner < 4; ++corner) {
                vertex = &vertices[corner];
                vertex->position[0] = corner == 1 || corner == 2 ? 16 : 0;
                vertex->position[1] = corner < 2 ? 0 : 16;
                vertex->uv[0] = uv[corner == 1 || corner == 2 ? 2 : 0];
                vertex->uv[1] = uv[corner < 2 ? 1 : 3];
                memcpy(vertex->color, tint, sizeof(tint));
            }
            result = sink(context, vertices);
            if (maage_status_failed(result)) return result;
        }
        return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_PRESENT, 0);
    }
    if (!state || !block || !(geometry = maage_block_geometry_get(maage_block_registry_geometry(blocks), block->geometry_id))) return maage_status_make(MAAGE_STATUS_NOT_FOUND, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_PRESENT, 0);
    if (geometry->primitive == MAAGE_BLOCK_GEOMETRY_CUSTOM) {
        memset(&work, 0, sizeof(work));
        work.state = state;
        work.grass = grass;
        work.sink = sink;
        work.context = context;
        memset(&query, 0, sizeof(query));
        query.blocks = blocks;
        query.block = block;
        query.state = state;
        query.geometry = geometry;
        query.item = 1;
        geometry_sink.quad = maage_item_icon_quad;
        geometry_sink.context = &work;
        result = geometry->emit(geometry->context, &query, &geometry_sink);
        return maage_status_failed(work.result) ? work.result : result;
    }
    for (i = 0; i < 3; ++i) {
        face = &state->faces[visible[i]];
        for (layer = 0; layer < 2; ++layer) {
            if (layer && !(face->overlay_uv[2] > face->overlay_uv[0])) continue;
            uv = layer ? face->overlay_uv : face->uv;
            maage_client_item_face_tint(grass, face, layer ? face->overlay_tint_mode : face->tint_mode, tint);
            for (corner = 0; corner < 4; ++corner) {
                vertex = &vertices[corner];
                for (axis = 0; axis < 3; ++axis)
                    position[axis] = (maage_interaction_corners[visible[i]][corner][axis] ? block->selection_shape.maximum[axis] : block->selection_shape.minimum[axis]) - 0.5;
                /* Scale 10 and X=210 / Y=45 rotations project centered item
                 * bounds into GUI coordinates. */
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
            result = sink(context, vertices);
            if (maage_status_failed(result)) return result;
        }
    }
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_PRESENT, 0);
}

typedef struct maage_client_icon_variant {
    maage_render_ui_vertex *vertices;
    size_t count, capacity;
    maage_status result;
} maage_client_icon_variant;
typedef struct maage_client_icon_item {
    maage_registry_pages variants;
} maage_client_icon_item;
struct maage_client_item_icon_cache {
    maage_registry_pages items;
};
static maage_status maage_client_icon_cache_quad(void *opaque, const maage_render_ui_vertex vertices[4]) {
    maage_client_icon_variant *variant = (maage_client_icon_variant *) opaque;
    if (variant->count >= (size_t) - 1 / (4 * sizeof(*vertices)) || (variant->vertices && variant->count >= variant->capacity)) return maage_status_make(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_MESH, 0);
    if (variant->vertices) memcpy(variant->vertices + variant->count * 4, vertices, 4 * sizeof(*vertices));
    ++variant->count;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_MESH, 0);
}
maage_status maage_client_item_icon_cached(maage_client_item_icon_cache **cache, const maage_model_registry *models, const maage_item_registry *items, const maage_block_registry *blocks, const maage_item_stack *stack, const float grass[3], const float (*textures)[4], size_t texture_count, maage_client_item_icon_sink sink, void *context) {
    const maage_item_definition *definition = stack ? maage_item_registry_get(items, stack->item_id) : NULL;
    maage_client_icon_item *item;
    maage_client_icon_variant *variant;
    maage_status result;
    size_t i;
    void **slot;
    if (!cache || !definition || !sink || !maage_item_stack_valid(items, stack)) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_PRESENT, 0);
    if (definition->model != MAAGE_ITEM_MODEL_REGISTERED) return maage_client_item_icon(models, items, blocks, stack, grass, textures, texture_count, sink, context);
    if (!*cache) {
        *cache = (maage_client_item_icon_cache *) calloc(1, sizeof(**cache));
        if (!*cache) return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_CREATE, 0);
    }
    slot = maage_registry_pages_find(&(*cache)->items, stack->item_id);
    if (!slot) slot = maage_registry_pages_ensure(&(*cache)->items, stack->item_id);
    if (!slot) return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_CREATE, 0);
    if (!*slot) {
        *slot = calloc(1, sizeof(*item));
        if (!*slot) return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_CREATE, 0);
    }
    item = (maage_client_icon_item *) *slot;
    slot = maage_registry_pages_find(&item->variants, stack->metadata);
    if (!slot) slot = maage_registry_pages_ensure(&item->variants, stack->metadata);
    if (!slot) return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_CREATE, 0);
    variant = (maage_client_icon_variant *) *slot;
    if (!variant) {
        variant = (maage_client_icon_variant *) calloc(1, sizeof(*variant));
        if (!variant) return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_CREATE, 0);
        *slot = variant;
        result = maage_client_item_icon(models, items, blocks, stack, grass, textures, texture_count, maage_client_icon_cache_quad, variant);
        if (maage_status_failed(result)) return variant->result = result;
        variant->capacity = variant->count;
        if (variant->count) {
            variant->vertices = (maage_render_ui_vertex *) malloc(variant->count * 4 * sizeof(*variant->vertices));
            if (!variant->vertices) return variant->result = maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_CREATE, 0);
            variant->count = 0;
            result = maage_client_item_icon(models, items, blocks, stack, grass, textures, texture_count, maage_client_icon_cache_quad, variant);
            if (maage_status_failed(result)) return variant->result = result;
            if (variant->count != variant->capacity) return variant->result = maage_status_make(MAAGE_STATUS_INVARIANT_VIOLATION, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_MESH, 0);
        }
        variant->result = maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_PRESENT, 0);
    }
    if (maage_status_failed(variant->result)) return variant->result;
    for (i = 0; i < variant->count; ++i) {
        result = sink(context, variant->vertices + i * 4);
        if (maage_status_failed(result)) return result;
    }
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_PRESENT, 0);
}
static void maage_client_icon_variant_destroy(void *opaque) {
    maage_client_icon_variant *variant = (maage_client_icon_variant *) opaque;
    free(variant->vertices);
    free(variant);
}
static void maage_client_icon_item_destroy(void *opaque) {
    maage_client_icon_item *item = (maage_client_icon_item *) opaque;
    maage_registry_pages_destroy(&item->variants, maage_client_icon_variant_destroy);
    free(item);
}
void maage_client_item_icon_cache_destroy(maage_client_item_icon_cache *cache) {
    if (!cache) return;
    maage_registry_pages_destroy(&cache->items, maage_client_icon_item_destroy);
    free(cache);
}
