#include "models.h"
#include <float.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

typedef struct client_model_work {
    const maage_client_resources *resources;
    maage_mesh_vertex *vertices;
    unsigned int *indices;
    maage_texture_id texture;
    float tint[4];
    size_t count, capacity;
    maage_status failure;
} client_model_work;
static maage_status client_model_quad(void *opaque, const maage_model_quad *quad) {
    static const unsigned int winding[6] = {0, 1, 2, 0, 2, 3};
    static const float shade[6] = {0.6f, 0.6f, 0.5f, 1, 0.8f, 0.8f};
    client_model_work *work = (client_model_work *) opaque;
    maage_mesh_vertex *vertex;
    const float *uv;
    unsigned int corner, axis, base;
    if (maage_status_failed(work->failure)) return work->failure;
    if (!quad || quad->face >= 6 || quad->material > MAAGE_MESH_MATERIAL_TRANSLUCENT) goto malformed;
    for (corner = 0; corner < 4; ++corner) {
        for (axis = 0; axis < 3; ++axis)
            if (!(quad->positions[corner][axis] >= -FLT_MAX && quad->positions[corner][axis] <= FLT_MAX)) goto malformed;
        for (axis = 0; axis < 2; ++axis)
            if (!(quad->uv[corner][axis] >= 0 && quad->uv[corner][axis] <= 1)) goto malformed;
    }
    if (work->count >= UINT_MAX / 4U || work->count >= (size_t) - 1 / 6 || (work->vertices && work->count >= work->capacity)) return work->failure = maage_status_make(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_MESH, 0);
    if (!work->vertices) {
        ++work->count;
        return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_MESH, 0);
    }
    uv = work->resources->uv[work->texture];
    base = (unsigned int) (work->count * 4);
    for (corner = 0; corner < 4; ++corner) {
        vertex = &work->vertices[work->count * 4 + corner];
        memset(vertex, 0, sizeof(*vertex));
        for (axis = 0; axis < 3; ++axis) {
            vertex->position[axis] = quad->positions[corner][axis];
            vertex->color[axis] = work->tint[axis] * shade[quad->face];
        }
        for (axis = 0; axis < 2; ++axis)
            vertex->uv[axis] = uv[axis] + quad->uv[corner][axis] * (uv[axis + 2] - uv[axis]);
        vertex->color[3] = work->tint[3];
        vertex->material = (float) quad->material;
        vertex->light[0] = 1;
    }
    for (corner = 0; corner < 6; ++corner)
        work->indices[work->count * 6 + corner] = base + winding[corner];
    ++work->count;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_MESH, 0);
malformed:
    return work->failure = maage_status_make(MAAGE_STATUS_MALFORMED_INPUT, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_MESH, 0);
}
maage_status maage_client_model_part_create(const maage_client_resources *resources, const maage_model_definition *model, size_t part, maage_texture_id texture, unsigned long variant, maage_mesh_data *data) {
    client_model_work work;
    maage_model_sink sink;
    maage_model_animation animation;
    maage_model_transform transform;
    maage_status result;
    size_t count;
    unsigned int axis;
    if (data) memset(data, 0, sizeof(*data));
    if (!resources || !model || part >= model->part_count || !data) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_MESH, 0);
    if (texture == MAAGE_CONTENT_ID_INVALID) texture = model->texture_id;
    if (model->parts[part].texture_id != MAAGE_CONTENT_ID_INVALID) texture = model->parts[part].texture_id;
    if (texture >= resources->texture_count) return maage_status_make(MAAGE_STATUS_NOT_FOUND, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_MESH, 0);
    memset(&work, 0, sizeof(work));
    work.resources = resources;
    work.texture = texture;
    maage_model_transform_init(&transform);
    if (model->animate) {
        memset(&animation, 0, sizeof(animation));
        animation.variant = variant;
        result = model->animate(model->context, &animation, part, &transform);
        if (maage_status_failed(result)) return result;
    }
    for (axis = 0; axis < 4; ++axis) {
        if (!(transform.tint[axis] >= 0 && transform.tint[axis] <= 1)) return maage_status_make(MAAGE_STATUS_MALFORMED_INPUT, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_MESH, 0);
        work.tint[axis] = transform.tint[axis];
    }
    sink.quad = client_model_quad;
    sink.context = &work;
    result = maage_model_emit_part(model, part, &sink);
    if (maage_status_failed(work.failure)) return work.failure;
    if (maage_status_failed(result)) return result;
    count = work.count;
    if (!count) return result;
    if (count > (size_t) - 1 / (4 * sizeof(*work.vertices)) || count > (size_t) - 1 / (6 * sizeof(*work.indices))) return maage_status_make(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_MESH, 0);
    work.vertices = (maage_mesh_vertex *) malloc(count * 4 * sizeof(*work.vertices));
    work.indices = (unsigned int *) malloc(count * 6 * sizeof(*work.indices));
    if (!work.vertices || !work.indices) {
        free(work.vertices);
        free(work.indices);
        return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_MESH, 0);
    }
    work.count = 0;
    work.capacity = count;
    result = maage_model_emit_part(model, part, &sink);
    if (maage_status_failed(work.failure)) result = work.failure;
    if (!maage_status_failed(result) && work.count != count) result = maage_status_make(MAAGE_STATUS_INVARIANT_VIOLATION, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_MESH, 0);
    if (maage_status_failed(result)) {
        free(work.vertices);
        free(work.indices);
        return result;
    }
    data->vertices = work.vertices;
    data->indices = work.indices;
    data->vertex_count = count * 4;
    data->index_count = count * 6;
    return result;
}
