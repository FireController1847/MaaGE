#include "render_internal.h"
#include "render_shared.h"
#include <float.h>
#include <stdlib.h>
#include <string.h>

static int maage_render_triangle_order(const void *left, const void *right)
{
    const maage_render_triangle *a = (const maage_render_triangle *)left;
    const maage_render_triangle *b = (const maage_render_triangle *)right;
    if (a->depth != b->depth) return a->depth > b->depth ? -1 : 1;
    return a->order < b->order ? -1 : a->order != b->order;
}
int maage_render_mesh_order(const void *left, const void *right)
{
    const maage_render_ordered_mesh *a = (const maage_render_ordered_mesh *)left;
    const maage_render_ordered_mesh *b = (const maage_render_ordered_mesh *)right;
    if (a->depth != b->depth) return a->depth > b->depth ? -1 : 1;
    return a->order < b->order ? -1 : a->order != b->order;
}
int maage_render_camera_origin(const float matrix[16], double origin[3])
{
    double a[3], b[3], c[3], cross[3][3], determinant;
    int axis, next, last;
    /* The eye lies on clip X=Y=W=0. Solve without a backend projection. */
    for (axis = 0; axis < 3; ++axis) {
        a[axis] = matrix[axis * 4];
        b[axis] = matrix[axis * 4 + 1];
        c[axis] = matrix[axis * 4 + 3];
    }
    for (axis = 0; axis < 3; ++axis) {
        next = (axis + 1) % 3; last = (axis + 2) % 3;
        cross[0][axis] = b[next] * c[last] - b[last] * c[next];
        cross[1][axis] = c[next] * a[last] - c[last] * a[next];
        cross[2][axis] = a[next] * b[last] - a[last] * b[next];
    }
    determinant = a[0] * cross[0][0] + a[1] * cross[0][1] + a[2] * cross[0][2];
    if (determinant > -1e-12 && determinant < 1e-12) return 0;
    for (axis = 0; axis < 3; ++axis)
        origin[axis] = -(matrix[12] * cross[0][axis] + matrix[13] * cross[1][axis] +
            matrix[15] * cross[2][axis]) / determinant;
    return 1;
}
float maage_render_depth(const float center[3], const float matrix[16],
    const double origin[3], int perspective)
{
    double depth = 0, difference;
    int axis;
    for (axis = 0; axis < 3; ++axis) {
        difference = center[axis] - origin[axis];
        depth += perspective ? difference * difference : center[axis] * matrix[axis * 4 + 2];
    }
    return depth > FLT_MAX ? FLT_MAX : (depth < -FLT_MAX ? -FLT_MAX : (float)depth);
}
maage_status maage_render_geometry_prepare(maage_render_geometry *geometry,
    const maage_mesh_data *data, unsigned int *grouped_indices)
{
    size_t index, destination[3], category, triangle_count = 0, axis, corner;
    maage_render_triangle *triangle;
    float material;
    if (!geometry || !data || !grouped_indices || !data->vertices || !data->indices ||
        !data->index_count || data->index_count % 3 || geometry->triangles)
        return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_RENDER,
            MAAGE_OPERATION_UPLOAD, 0);
    for (index = 0; index < data->index_count; ++index)
        if (data->indices[index] >= data->vertex_count)
            return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_RENDER,
                MAAGE_OPERATION_UPLOAD, 0);
    for (index = 0; index < data->vertex_count; ++index)
        for (axis = 0; axis < 3; ++axis)
            if (!(data->vertices[index].position[axis] >= -FLT_MAX &&
                data->vertices[index].position[axis] <= FLT_MAX))
                return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_RENDER,
                    MAAGE_OPERATION_UPLOAD, 1);
    for (index = 0; index < data->index_count; index += 3) {
        material = data->vertices[data->indices[index]].material;
        if (material == MAAGE_MESH_MATERIAL_TRANSLUCENT) geometry->translucent_count += 3;
        else if (material == MAAGE_MESH_MATERIAL_MULTIPLY) geometry->multiply_count += 3;
        else geometry->solid_count += 3;
    }
    if (geometry->translucent_count) {
        if (geometry->translucent_count / 3 > (size_t)-1 / sizeof(*geometry->triangles))
            return maage_status_make(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_CONTEXT_RENDER,
                MAAGE_OPERATION_UPLOAD, 0);
        geometry->triangles = (maage_render_triangle *)calloc(geometry->translucent_count / 3,
            sizeof(*geometry->triangles));
        if (!geometry->triangles)
            return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_RENDER,
                MAAGE_OPERATION_UPLOAD, 0);
    }
    destination[0] = 0; destination[1] = geometry->solid_count;
    destination[2] = geometry->solid_count + geometry->translucent_count;
    for (index = 0; index < data->index_count; index += 3) {
        material = data->vertices[data->indices[index]].material;
        category = material == MAAGE_MESH_MATERIAL_TRANSLUCENT ? 1 :
            (material == MAAGE_MESH_MATERIAL_MULTIPLY ? 2 : 0);
        memcpy(grouped_indices + destination[category], data->indices + index,
            3 * sizeof(*grouped_indices));
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
maage_status maage_render_geometry_dispose(maage_render_geometry *geometry)
{
    if (!geometry) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_DESTROY, 0);
    free(geometry->triangles);
    memset(geometry, 0, sizeof(*geometry));
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_DESTROY, 0);
}
maage_status maage_render_geometry_sort(maage_render_geometry *geometry,
    const float matrix[16])
{
    double origin[3], movement = 0;
    int perspective, axis;
    size_t index, count = geometry->translucent_count / 3;
    if (!count) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_UPLOAD, 0);
    perspective = maage_render_camera_origin(matrix, origin);
    if (!perspective) {
        origin[0] = matrix[2]; origin[1] = matrix[6]; origin[2] = matrix[10];
    }
    for (axis = 0; axis < 3; ++axis)
        movement += (origin[axis] - geometry->sort_origin[axis]) *
            (origin[axis] - geometry->sort_origin[axis]);
    if (geometry->revision && movement <= (perspective ? 0.25 : 0.0001))
        return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_UPLOAD, 0);
    for (index = 0; index < count; ++index)
        geometry->triangles[index].depth = maage_render_depth(geometry->triangles[index].center,
            matrix, origin, perspective);
    qsort(geometry->triangles, count, sizeof(*geometry->triangles), maage_render_triangle_order);
    memcpy(geometry->sort_origin, origin, sizeof(origin));
    if (++geometry->revision == 0) geometry->revision = 1;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_UPLOAD, 0);
}
maage_status maage_render_geometry_write_sorted(const maage_render_geometry *geometry,
    unsigned int *indices)
{
    size_t index;
    if (!geometry || (!indices && geometry->translucent_count))
        return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_RENDER,
            MAAGE_OPERATION_UPLOAD, 0);
    for (index = 0; index < geometry->translucent_count / 3; ++index)
        memcpy(indices + index * 3, geometry->triangles[index].indices, 3 * sizeof(*indices));
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_UPLOAD, 0);
}
size_t maage_render_transient_capacity(size_t current, size_t required)
{
    size_t capacity = current ? current : 4096;
    while (capacity < required) {
        if (capacity > (size_t)-1 / 2) return required;
        capacity *= 2;
    }
    return capacity;
}
maage_status maage_render_frame_plan_prepare(const maage_renderer *renderer,
    const maage_render_ui_data *ui, int preparing, maage_render_frame_plan *plan)
{
    if (!renderer || !plan)
        return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_RENDER,
            MAAGE_OPERATION_PRESENT, 0);
    plan->layers[0] = preparing ? NULL : renderer->background;
    plan->layers[1] = preparing ? NULL : renderer->overlay;
    plan->layers[2] = ui;
    plan->foreground = preparing ? NULL : renderer->foreground;
    plan->panorama_count = ui ? ui->gaussian_background_indices : 0;
    if (ui && (plan->panorama_count > ui->index_count || plan->panorama_count % 3))
        return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_RENDER,
            MAAGE_OPERATION_PRESENT, 0);
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_PRESENT, 0);
}
