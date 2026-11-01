#include "maage/internal/geometry.h"
#include <float.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

static maage_status maage_geometry_validate(const maage_mesh_data *data) {
    size_t index, axis;
    if (!data || !data->vertices || !data->indices || !data->vertex_count ||
        !data->index_count || data->index_count % 3 || data->vertex_count > UINT_MAX ||
        data->vertex_count > (size_t) - 1 / sizeof(*data->vertices) ||
        data->index_count > (size_t) - 1 / sizeof(*data->indices))
        return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_UPLOAD, 0);
    for (index = 0; index < data->index_count; ++index)
        if (data->indices[index] >= data->vertex_count) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_UPLOAD, 0);
    for (index = 0; index < data->index_count; index += 3) {
        float material = data->vertices[data->indices[index]].material;
        if (!(material == MAAGE_MESH_MATERIAL_OPAQUE || material == MAAGE_MESH_MATERIAL_CUTOUT ||
              material == MAAGE_MESH_MATERIAL_TRANSLUCENT || material == MAAGE_MESH_MATERIAL_MULTIPLY) ||
            data->vertices[data->indices[index + 1]].material != material ||
            data->vertices[data->indices[index + 2]].material != material)
            return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_UPLOAD, 1);
    }
    for (index = 0; index < data->vertex_count; ++index) {
        const float *animation = data->vertices[index].animation;
        if (!(animation[0] >= 0 && animation[0] <= 1 &&
              animation[1] >= -FLT_MAX && animation[1] <= FLT_MAX &&
              animation[2] >= 0 && animation[2] <= FLT_MAX) ||
            (animation[1] != 0 && !(animation[2] >= 1)))
            return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_UPLOAD, 1);
        for (axis = 0; axis < 3; ++axis)
            if (!(data->vertices[index].position[axis] >= -FLT_MAX && data->vertices[index].position[axis] <= FLT_MAX))
                return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_UPLOAD, 1);
    }
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_UPLOAD, 0);
}

static maage_status maage_geometry_prepare(maage_geometry *geometry, const maage_mesh_data *data, unsigned int *grouped_indices) {
    size_t index, destination[3], category, triangle_count = 0, axis, corner;
    maage_geometry_triangle *triangle;
    float material;
    if (!geometry || !data || !grouped_indices || !data->vertices || !data->indices || !data->index_count || data->index_count % 3 || geometry->triangles) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_UPLOAD, 0);
    for (index = 0; index < data->index_count; index += 3) {
        material = data->vertices[data->indices[index]].material;
        if (material == MAAGE_MESH_MATERIAL_TRANSLUCENT) geometry->translucent_count += 3;
        else if (material == MAAGE_MESH_MATERIAL_MULTIPLY) geometry->multiply_count += 3;
        else
            geometry->solid_count += 3;
    }
    if (geometry->translucent_count) {
        if (geometry->translucent_count / 3 > (size_t) - 1 / sizeof(*geometry->triangles)) return maage_status_make(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_UPLOAD, 0);
        geometry->triangles = (maage_geometry_triangle *) calloc(geometry->translucent_count / 3, sizeof(*geometry->triangles));
        if (!geometry->triangles) return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_UPLOAD, 0);
    }
    destination[0] = 0;
    destination[1] = geometry->solid_count;
    destination[2] = geometry->solid_count + geometry->translucent_count;
    for (index = 0; index < data->index_count; index += 3) {
        material = data->vertices[data->indices[index]].material;
        category = material == MAAGE_MESH_MATERIAL_TRANSLUCENT ? 1 : (material == MAAGE_MESH_MATERIAL_MULTIPLY ? 2 : 0);
        memcpy(grouped_indices + destination[category], data->indices + index, 3 * sizeof(*grouped_indices));
        destination[category] += 3;
        if (category != 1) continue;
        triangle = &geometry->triangles[triangle_count];
        memcpy(triangle->indices, data->indices + index, sizeof(triangle->indices));
        triangle->order = triangle_count++;
        for (axis = 0; axis < 3; ++axis) {
            for (corner = 0; corner < 3; ++corner)
                triangle->center[axis] += data->vertices[triangle->indices[corner]].position[axis] / 3;
            geometry->center[axis] += triangle->center[axis] / (geometry->translucent_count / 3);
        }
    }
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_UPLOAD, 0);
}
maage_status maage_geometry_dispose(maage_geometry *geometry) {
    if (!geometry) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_DESTROY, 0);
    free(geometry->triangles);
    memset(geometry, 0, sizeof(*geometry));
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_DESTROY, 0);
}

maage_status maage_mesh_prepare(const maage_mesh_data *data, maage_mesh_preparation **output) {
    maage_mesh_preparation *prepared;
    maage_status result;
    if (!output) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_UPLOAD, 0);
    *output = NULL;
    result = maage_geometry_validate(data);
    if (maage_status_failed(result)) return result;
    prepared = (maage_mesh_preparation *) calloc(1, sizeof(*prepared));
    if (!prepared) return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_UPLOAD, 0);
    prepared->indices = (unsigned int *) malloc(data->index_count * sizeof(*prepared->indices));
    if (!prepared->indices) result = maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_UPLOAD, 0);
    else result = maage_geometry_prepare(&prepared->geometry, data, prepared->indices);
    if (maage_status_failed(result)) {
        maage_mesh_preparation_destroy(prepared);
        return result;
    }
    prepared->data = *data;
    prepared->data.indices = prepared->indices;
    *output = prepared;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_UPLOAD, 0);
}
maage_status maage_mesh_preparation_destroy(maage_mesh_preparation *prepared) {
    if (!prepared) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_DESTROY, 0);
    maage_geometry_dispose(&prepared->geometry);
    free(prepared->indices);
    free(prepared);
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_DESTROY, 0);
}
