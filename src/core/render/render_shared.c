#include "render_internal.h"
#include <float.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

static maage_status maage_render_ui_validate(const maage_renderer *renderer,
    const maage_render_ui_data *batch);

static int maage_render_triangle_order(const void *left, const void *right)
{
    const maage_render_triangle *a = (const maage_render_triangle *)left;
    const maage_render_triangle *b = (const maage_render_triangle *)right;
    if (a->depth != b->depth) return a->depth > b->depth ? -1 : 1;
    return a->order < b->order ? -1 : a->order != b->order;
}
static int maage_render_mesh_order(const void *left, const void *right)
{
    const maage_render_ordered_mesh *a = (const maage_render_ordered_mesh *)left;
    const maage_render_ordered_mesh *b = (const maage_render_ordered_mesh *)right;
    if (a->depth != b->depth) return a->depth > b->depth ? -1 : 1;
    return a->order < b->order ? -1 : a->order != b->order;
}
static int maage_render_camera_origin(const float matrix[16], double origin[3])
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
static float maage_render_depth(const float center[3], const float matrix[16],
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
static maage_status maage_render_geometry_sort(maage_render_geometry *geometry,
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
maage_status maage_render_frame_plan_prepare(maage_renderer *renderer,
    const maage_render_mesh *const *meshes, size_t count, const float matrix[16],
    const maage_render_ui_data *ui, int preparing, maage_render_frame_plan *plan)
{
    maage_render_ordered_mesh *ordered;
    const maage_render_geometry *geometry;
    double origin[3] = {0, 0, 0};
    size_t index, required, triangle, axis_index;
    float center[3];
    int perspective, axis;
    maage_status result;
    if (!renderer || !plan || (count && (!meshes || !matrix)))
        return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_RENDER,
            MAAGE_OPERATION_PRESENT, 0);
    if (!preparing && renderer->instance_count && (!matrix || renderer->instance_count > (size_t)-1-count))
        return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_PRESENT, 0);
    memset(plan, 0, sizeof(*plan));
    for (index = 0; index < count; ++index)
        if (!meshes[index] || meshes[index]->renderer != renderer)
            return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_RENDER,
                MAAGE_OPERATION_PRESENT, 0);
    if (matrix) for (axis = 0; axis < 16; ++axis)
        if (!(matrix[axis] >= -FLT_MAX && matrix[axis] <= FLT_MAX))
            return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_RENDER,
                MAAGE_OPERATION_PRESENT, 0);
    plan->preparing = preparing;
    plan->meshes = meshes; plan->mesh_count = count;
    plan->instances = preparing ? NULL : renderer->instances;
    plan->instance_count = preparing ? 0 : renderer->instance_count;
    plan->layers[0] = preparing ? NULL : renderer->background;
    plan->layers[1] = preparing ? NULL : renderer->overlay;
    plan->layers[2] = ui;
    plan->foreground = preparing ? NULL : renderer->foreground;
    plan->panorama_count = ui ? ui->gaussian_background_indices : 0;
    if (plan->panorama_count && (plan->panorama_count > ui->index_count || plan->panorama_count % 3))
        return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_RENDER,
            MAAGE_OPERATION_PRESENT, 0);
    for (axis = 0; axis < 3; ++axis) {
        result = maage_render_ui_validate(renderer, plan->layers[axis]);
        if (maage_status_failed(result)) return result;
    }
    required = count;
    for (index = 0; index < plan->instance_count; ++index) {
        triangle = plan->instances[index].mesh->geometry.translucent_count / 3;
        if (triangle > (size_t)-1-required) return maage_status_make(MAAGE_STATUS_BUFFER_OVERFLOW,
            MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_PRESENT, 0);
        required += triangle;
    }
    if (required > renderer->ordered_capacity) {
        if (required > (size_t)-1 / sizeof(*ordered))
            return maage_status_make(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_CONTEXT_RENDER,
                MAAGE_OPERATION_PRESENT, 0);
        ordered = (maage_render_ordered_mesh *)realloc(renderer->ordered, required * sizeof(*ordered));
        if (!ordered) return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED,
            MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_PRESENT, 0);
        renderer->ordered = ordered; renderer->ordered_capacity = required;
    }
    plan->ordered = renderer->ordered;
    perspective = matrix ? maage_render_camera_origin(matrix, origin) : 0;
    for (index = 0; index < count; ++index) {
        geometry = &meshes[index]->geometry;
        if (!geometry->translucent_count) continue;
        ordered = &renderer->ordered[plan->ordered_count++];
        ordered->mesh = meshes[index]; ordered->instance = NULL; ordered->triangle = 0; ordered->order = index;
        ordered->depth = maage_render_depth(geometry->center, matrix, origin, perspective);
    }
    for (index = 0; index < plan->instance_count; ++index) {
        const maage_render_instance *instance = &plan->instances[index];
        geometry = &instance->mesh->geometry;
        for (triangle = 0; triangle < geometry->translucent_count / 3; ++triangle) {
            for (axis_index = 0; axis_index < 3; ++axis_index)
                center[axis_index] = instance->transform[12+axis_index] +
                    instance->transform[axis_index] * geometry->triangles[triangle].center[0] +
                    instance->transform[4+axis_index] * geometry->triangles[triangle].center[1] +
                    instance->transform[8+axis_index] * geometry->triangles[triangle].center[2];
            ordered = &renderer->ordered[plan->ordered_count];
            ordered->mesh = instance->mesh; ordered->instance = instance;
            ordered->triangle = geometry->solid_count + geometry->triangles[triangle].order * 3;
            ordered->order = plan->ordered_count++;
            ordered->depth = maage_render_depth(center, matrix, origin, perspective);
        }
    }
    if (plan->ordered_count > 1) qsort(renderer->ordered, plan->ordered_count,
        sizeof(*plan->ordered), maage_render_mesh_order);
    if (matrix) memcpy(plan->world.matrix, matrix, sizeof(plan->world.matrix));
    plan->world.lighting[0] = renderer->sky_light;
    plan->world.parameters[0] = renderer->animation_tick;
    plan->hand = plan->world;
    memcpy(plan->hand.matrix, renderer->foreground_matrix, sizeof(plan->hand.matrix));
    plan->hand.lighting[1] = renderer->foreground_light[0];
    plan->hand.lighting[2] = renderer->foreground_light[1];
    plan->hand.lighting[3] = 1;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_PRESENT, 0);
}

const maage_render_pipeline_state maage_render_pipeline_states[MAAGE_RENDER_PIPELINE_COUNT] = {
    {0, 0, 1, 1, 1, 0, MAAGE_RENDER_FACTOR_SOURCE_ALPHA, MAAGE_RENDER_FACTOR_INVERSE_ALPHA},
    {0, 0, 1, 0, 0, 1, MAAGE_RENDER_FACTOR_SOURCE_ALPHA, MAAGE_RENDER_FACTOR_INVERSE_ALPHA},
    {0, 0, 1, 0, 1, 1, MAAGE_RENDER_FACTOR_DESTINATION_COLOR, MAAGE_RENDER_FACTOR_SOURCE_COLOR},
    {1, 0, 0, 0, 0, 1, MAAGE_RENDER_FACTOR_SOURCE_ALPHA, MAAGE_RENDER_FACTOR_INVERSE_ALPHA},
    {1, 0, 0, 0, 0, 1, MAAGE_RENDER_FACTOR_SOURCE_ALPHA, MAAGE_RENDER_FACTOR_ONE},
    {0, 1, 0, 0, 0, 0, MAAGE_RENDER_FACTOR_SOURCE_ALPHA, MAAGE_RENDER_FACTOR_INVERSE_ALPHA}
};

maage_status maage_render_geometry_sort_slot(const maage_render_mesh *mesh,
    const float matrix[16], unsigned long *revisions, size_t slots, size_t slot)
{
    /* The opaque handle owns this mutable cache; caller geometry stays immutable. */
    maage_render_geometry *geometry = &((maage_render_mesh *)mesh)->geometry;
    maage_status result = maage_render_geometry_sort(geometry, matrix);
    if (maage_status_failed(result)) return result;
    if (maage_status_code(result) == MAAGE_STATUS_SUCCESS)
        memset(revisions, 0, slots * sizeof(*revisions));
    return maage_status_make(geometry->translucent_count && revisions[slot] != geometry->revision ?
        MAAGE_STATUS_SUCCESS : MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_UPLOAD, 0);
}
size_t maage_render_geometry_range(const maage_render_geometry *geometry,
    unsigned int layer, size_t *first)
{
    *first = layer == MAAGE_RENDER_MULTIPLY ? geometry->solid_count + geometry->translucent_count : 0;
    return layer == MAAGE_RENDER_SOLID ? geometry->solid_count :
        (layer == MAAGE_RENDER_TRANSLUCENT ? geometry->translucent_count : geometry->multiply_count);
}
void maage_render_ui_uniforms(const maage_render_ui_data *batch, maage_render_uniforms *uniforms)
{
    memset(uniforms, 0, sizeof(*uniforms));
    uniforms->parameters[1] = batch->logical_width;
    uniforms->parameters[2] = batch->logical_height;
}
maage_status maage_render_ui_pack(const maage_render_ui_data *batch,
    maage_render_ui_gpu_vertex *vertices, unsigned int *indices)
{
    size_t index;
    if (!batch || (!vertices && batch->vertex_count) || (!indices && batch->index_count))
        return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_RENDER,
            MAAGE_OPERATION_UPLOAD, 0);
    for (index = 0; index < batch->vertex_count; ++index) {
        vertices[index].vertex = batch->vertices[index];
        vertices[index].depth = batch->projective_depth ? batch->projective_depth[index] : 1.0f;
    }
    if (batch->index_count) memcpy(indices, batch->indices, batch->index_count * sizeof(*indices));
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_UPLOAD, 0);
}
const maage_render_mesh *maage_render_frame_next_mesh(const maage_render_frame_plan *plan,
    int foreground, maage_render_mesh_cursor *cursor)
{
    const maage_render_mesh *mesh;
    const maage_render_ordered_mesh *ordered;
    size_t count, first, index;
    while (cursor->layer <= MAAGE_RENDER_MULTIPLY) {
        count = foreground ? (plan->foreground ? 1 : 0) :
            (cursor->layer == MAAGE_RENDER_TRANSLUCENT ? plan->ordered_count : plan->mesh_count + plan->instance_count);
        if (cursor->index == count) { ++cursor->layer; cursor->index = 0; continue; }
        index = cursor->index++; cursor->instance = NULL; cursor->triangle = 0;
        if (foreground) mesh = plan->foreground;
        else if (cursor->layer == MAAGE_RENDER_TRANSLUCENT) {
            ordered = &plan->ordered[index]; mesh = ordered->mesh;
            cursor->instance = ordered->instance; cursor->triangle = ordered->triangle;
        } else if (index < plan->mesh_count) mesh = plan->meshes[index];
        else { cursor->instance = &plan->instances[index-plan->mesh_count]; mesh = cursor->instance->mesh; }
        if (maage_render_geometry_range(&mesh->geometry, cursor->layer, &first)) return mesh;
    }
    return NULL;
}
void maage_render_draw_uniforms(const maage_render_frame_plan *plan, int foreground,
    const maage_render_mesh_cursor *cursor, maage_render_uniforms *uniforms)
{
    unsigned int row, column, k;
    *uniforms = foreground ? plan->hand : plan->world;
    if (!cursor->instance) return;
    for (column = 0; column < 4; ++column) for (row = 0; row < 4; ++row) {
        uniforms->matrix[column*4+row] = 0;
        for (k = 0; k < 4; ++k) uniforms->matrix[column*4+row] +=
            plan->world.matrix[k*4+row] * cursor->instance->transform[column*4+k];
    }
    uniforms->lighting[1] = cursor->instance->sky_light;
    uniforms->lighting[2] = cursor->instance->block_light;
    uniforms->lighting[3] = 1;
}
int maage_render_gpu_layout_supported(void)
{
    return sizeof(float) == 4 && sizeof(unsigned int) == 4 &&
        sizeof(maage_mesh_vertex) == MAAGE_RENDER_MESH_VERTEX_BYTES &&
        sizeof(maage_render_ui_vertex) == MAAGE_RENDER_UI_VERTEX_BYTES &&
        sizeof(maage_render_ui_gpu_vertex) == MAAGE_RENDER_UI_GPU_VERTEX_BYTES &&
        sizeof(maage_render_uniforms) == MAAGE_RENDER_UNIFORM_BYTES;
}
maage_status maage_render_mesh_validate(const maage_mesh_data *data)
{
    size_t index, axis;
    if (!data || !data->vertices || !data->indices || !data->vertex_count ||
        !data->index_count || data->index_count % 3 || data->vertex_count > UINT_MAX ||
        data->vertex_count > (size_t)-1 / sizeof(*data->vertices) ||
        data->index_count > (size_t)-1 / sizeof(*data->indices))
        return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_RENDER,
            MAAGE_OPERATION_UPLOAD, 0);
    for (index = 0; index < data->index_count; ++index)
        if (data->indices[index] >= data->vertex_count) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_UPLOAD, 0);
    for (index = 0; index < data->index_count; index += 3) {
        float material = data->vertices[data->indices[index]].material;
        if (!(material == MAAGE_MESH_MATERIAL_OPAQUE || material == MAAGE_MESH_MATERIAL_CUTOUT ||
            material == MAAGE_MESH_MATERIAL_TRANSLUCENT || material == MAAGE_MESH_MATERIAL_MULTIPLY) ||
            data->vertices[data->indices[index + 1]].material != material ||
            data->vertices[data->indices[index + 2]].material != material)
            return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT,
                MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_UPLOAD, 1);
    }
    for (index = 0; index < data->vertex_count; ++index) {
        const float *animation = data->vertices[index].animation;
        if (!(animation[0] >= 0 && animation[0] <= 1 &&
            animation[1] >= -FLT_MAX && animation[1] <= FLT_MAX &&
            animation[2] >= 0 && animation[2] <= FLT_MAX) ||
            (animation[1] != 0 && !(animation[2] >= 1)))
            return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT,
                MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_UPLOAD, 1);
        for (axis = 0; axis < 3; ++axis)
            if (!(data->vertices[index].position[axis] >= -FLT_MAX &&
                data->vertices[index].position[axis] <= FLT_MAX))
                return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_RENDER,
                    MAAGE_OPERATION_UPLOAD, 1);
    }
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_UPLOAD, 0);
}
static maage_status maage_render_ui_validate(const maage_renderer *renderer,
    const maage_render_ui_data *batch)
{
    size_t index;
    if (!batch || !batch->index_count)
        return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_PRESENT, 0);
    if (batch->blend > MAAGE_RENDER_BLEND_ADDITIVE ||
        !batch->texture || batch->texture->renderer != renderer || !batch->texture->owned ||
        !batch->vertices || !batch->indices || !batch->vertex_count || batch->index_count % 3 ||
        batch->vertex_count > UINT_MAX ||
        batch->vertex_count > (size_t)-1 / sizeof(maage_render_ui_gpu_vertex) ||
        batch->index_count > (size_t)-1 / sizeof(*batch->indices) ||
        !(batch->logical_width > 0 && batch->logical_width <= FLT_MAX) ||
        !(batch->logical_height > 0 && batch->logical_height <= FLT_MAX))
        return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_PRESENT, 0);
    for (index = 0; index < batch->index_count; ++index)
        if (batch->indices[index] >= batch->vertex_count)
            return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_PRESENT, 0);
    if (batch->projective_depth)
        for (index = 0; index < batch->vertex_count; ++index)
            if (!(batch->projective_depth[index] > 0.0f &&
                batch->projective_depth[index] <= FLT_MAX))
                return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_RENDER,
                    MAAGE_OPERATION_PRESENT, 1);
    if (!(renderer->capabilities.features & MAAGE_RENDER_CAP_UI) || !renderer->api->frame)
        return maage_status_make(MAAGE_STATUS_NOT_IMPLEMENTED, MAAGE_CONTEXT_RENDER,
            MAAGE_OPERATION_PRESENT, 0);
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_PRESENT, 0);
}
