#include "maage/internal/geometry.h"
#include <float.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

static maage_status maage_geometry_validate(const maage_mesh_data *data) {
    size_t index, axis, corner;
    if (!data || !data->vertices || !data->indices || !data->vertex_count ||
        !data->index_count || data->index_count % 3 || data->vertex_count > UINT_MAX ||
        data->vertex_count > (size_t) - 1 / sizeof(*data->vertices) ||
        data->index_count > (size_t) - 1 / sizeof(*data->indices))
        return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_UPLOAD, 0);
    for (index = 0; index < data->index_count; ++index)
        if (data->indices[index] >= data->vertex_count) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_UPLOAD, 0);
    for (index = 0; index < data->index_count; index += 3) {
        const maage_mesh_vertex *first = &data->vertices[data->indices[index]];
        float material = first->material;
        for (corner = 1; corner < 3; ++corner) {
            const maage_mesh_vertex *vertex = &data->vertices[data->indices[index + corner]];
            for (axis = 0; axis < 4; ++axis)
                if (first->tile[axis] != vertex->tile[axis]) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_UPLOAD, 1);
            if (first->tile[2] > first->tile[0])
                for (axis = 0; axis < 3; ++axis)
                    if (first->animation[axis] != vertex->animation[axis]) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_UPLOAD, 1);
        }
        if (!(material == MAAGE_MESH_MATERIAL_OPAQUE || material == MAAGE_MESH_MATERIAL_CUTOUT ||
              material == MAAGE_MESH_MATERIAL_TRANSLUCENT || material == MAAGE_MESH_MATERIAL_MULTIPLY) ||
            data->vertices[data->indices[index + 1]].material != material ||
            data->vertices[data->indices[index + 2]].material != material)
            return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_UPLOAD, 1);
    }
    for (index = 0; index < data->vertex_count; ++index) {
        const float *animation = data->vertices[index].animation;
        const float *tile = data->vertices[index].tile;
        for (axis = 0; axis < 4; ++axis)
            if (!(tile[axis] >= 0 && tile[axis] <= 1)) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_UPLOAD, 1);
        if (tile[2] < tile[0] || tile[3] < tile[1] || (tile[2] > tile[0]) != (tile[3] > tile[1])) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_UPLOAD, 1);
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

/* Material tuples are deduplicated once on CPU workers, never in draw loops.
 * IDs are per-mesh numeric indices; table growth has no content-count ceiling. */
static unsigned long maage_surface_hash(const maage_mesh_surface *surface) {
    const unsigned char *bytes = (const unsigned char *) surface;
    unsigned long hash = 2166136261UL;
    size_t i;
    for (i = 0; i < sizeof(*surface); ++i)
        hash = ((hash ^ bytes[i]) * 16777619UL) & 0xffffffffUL;
    return hash;
}
static maage_status maage_geometry_surfaces(maage_mesh_preparation *prepared, const maage_mesh_data *data) {
    maage_mesh_surface surface, *grown;
    unsigned int *buckets = NULL, *new_buckets;
    size_t capacity = 0, bucket_count = 0, index, bucket, i;
    prepared->surface_indices = (unsigned int *) malloc(data->vertex_count * sizeof(*prepared->surface_indices));
    if (!prepared->surface_indices) goto allocation;
    for (index = 0; index < data->vertex_count; ++index) {
        memset(&surface, 0, sizeof(surface));
        memcpy(surface.animation, data->vertices[index].animation, sizeof(surface.animation));
        surface.material = data->vertices[index].material;
        memcpy(surface.tile, data->vertices[index].tile, sizeof(surface.tile));
        if (prepared->surface_count == capacity) {
            size_t next = capacity ? capacity * 2 : 16;
            if (next < capacity || next > (size_t) - 1 / (2 * sizeof(*buckets)) || next > (size_t) - 1 / sizeof(*grown)) goto overflow;
            grown = (maage_mesh_surface *) realloc(prepared->surfaces, next * sizeof(*grown));
            if (!grown) goto allocation;
            prepared->surfaces = grown;
            new_buckets = (unsigned int *) calloc(next * 2, sizeof(*new_buckets));
            if (!new_buckets) goto allocation;
            free(buckets);
            buckets = new_buckets;
            capacity = next;
            prepared->surface_capacity = capacity;
            bucket_count = next * 2;
            for (i = 0; i < prepared->surface_count; ++i) {
                bucket = maage_surface_hash(&prepared->surfaces[i]) & (bucket_count - 1);
                while (buckets[bucket])
                    bucket = (bucket + 1) & (bucket_count - 1);
                buckets[bucket] = (unsigned int) i + 1U;
            }
        }
        bucket = maage_surface_hash(&surface) & (bucket_count - 1);
        while (buckets[bucket] && memcmp(&prepared->surfaces[buckets[bucket] - 1U], &surface, sizeof(surface)))
            bucket = (bucket + 1) & (bucket_count - 1);
        if (!buckets[bucket]) {
            if (prepared->surface_count >= UINT_MAX) goto overflow;
            prepared->surfaces[prepared->surface_count++] = surface;
            buckets[bucket] = (unsigned int) prepared->surface_count;
        }
        prepared->surface_indices[index] = buckets[bucket] - 1U;
    }
    free(buckets);
    grown = (maage_mesh_surface *) realloc(prepared->surfaces, prepared->surface_count * sizeof(*grown));
    if (grown) {
        prepared->surfaces = grown;
        prepared->surface_capacity = prepared->surface_count;
    }
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_UPLOAD, 0);
overflow:
    free(buckets);
    return maage_status_make(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_UPLOAD, 0);
allocation:
    free(buckets);
    return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_UPLOAD, 0);
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
    if (!maage_status_failed(result)) result = maage_geometry_surfaces(prepared, data);
    if (maage_status_failed(result)) {
        maage_mesh_preparation_destroy(prepared);
        return result;
    }
    prepared->data = *data;
    prepared->data.indices = prepared->indices;
    *output = prepared;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_UPLOAD, 0);
}
/* Index-only views are derived from a source mesh whose CPU vertex array
 * has already been released. Every generated quad owns four vertices, and
 * its six indices remain in source-face order. The base renderer geometry
 * retains translucent triangle centers and original order even after camera
 * sorting shuffles its triangle array. */
maage_status maage_mesh_prepare_index_view(const maage_mesh_data *data, const unsigned char *face_flags, const maage_geometry *base, maage_mesh_preparation **output) {
    maage_mesh_preparation *prepared = NULL;
    const maage_geometry_triangle **original = NULL;
    size_t source_faces, source_triangles, faces, counts[3] = {0, 0, 0}, destination[3];
    size_t face, previous = 0, ordinal = 0, written = 0, source, triangle, axis, category, corner;
    maage_status result = maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_UPLOAD, 0);
    if (output) *output = NULL;
    if (!data || !face_flags || !base || !output || !data->indices || !data->index_count ||
        data->index_count % 6 || !data->vertex_count || data->vertex_count % 4 ||
        data->vertex_count > UINT_MAX || data->index_count > (size_t) - 1 / sizeof(unsigned int))
        return result;
    source_faces = data->vertex_count / 4;
    faces = data->index_count / 6;
    if (source_faces > (size_t) - 1 / 6 || base->solid_count > (size_t) - 1 - base->translucent_count ||
        base->solid_count + base->translucent_count > (size_t) - 1 - base->multiply_count ||
        base->solid_count + base->translucent_count + base->multiply_count != source_faces * 6 ||
        base->translucent_count % 6 || (base->translucent_count && !base->triangles)) return result;
    source_triangles = base->translucent_count / 3;
    for (face = 0; face < faces; ++face) {
        source = data->indices[face * 6] / 4;
        if (source >= source_faces || (face && source <= previous)) return result;
        category = face_flags[source] & 3U;
        if (category > 2) return result;
        for (corner = 0; corner < 6; ++corner)
            if (data->indices[face * 6 + corner] / 4 != source) return result;
        counts[category] += 6;
        previous = source;
    }
    prepared = (maage_mesh_preparation *) calloc(1, sizeof(*prepared));
    if (!prepared) goto allocation_failed;
    prepared->indices = (unsigned int *) malloc(data->index_count * sizeof(*prepared->indices));
    if (!prepared->indices) goto allocation_failed;
    prepared->geometry.solid_count = counts[0];
    prepared->geometry.translucent_count = counts[1];
    prepared->geometry.multiply_count = counts[2];
    if (counts[1]) {
        if (counts[1] / 3 > (size_t) - 1 / sizeof(*prepared->geometry.triangles)) goto overflow;
        prepared->geometry.triangles = (maage_geometry_triangle *) calloc(counts[1] / 3, sizeof(*prepared->geometry.triangles));
        if (!prepared->geometry.triangles) goto allocation_failed;
    }
    if (source_triangles) {
        if (source_triangles > (size_t) - 1 / sizeof(*original)) goto overflow;
        original = (const maage_geometry_triangle **) calloc(source_triangles, sizeof(*original));
        if (!original) goto allocation_failed;
        for (triangle = 0; triangle < source_triangles; ++triangle) {
            source = base->triangles[triangle].order;
            if (source >= source_triangles || original[source]) goto malformed;
            original[source] = &base->triangles[triangle];
        }
    }
    destination[0] = 0;
    destination[1] = counts[0];
    destination[2] = counts[0] + counts[1];
    previous = ordinal = 0;
    for (face = 0; face < faces; ++face) {
        source = data->indices[face * 6] / 4;
        while (previous < source) {
            if ((face_flags[previous] & 3U) == 1U) ordinal += 2;
            ++previous;
        }
        category = face_flags[source] & 3U;
        memcpy(prepared->indices + destination[category], data->indices + face * 6, 6 * sizeof(*prepared->indices));
        destination[category] += 6;
        if (category == 1U) {
            for (triangle = 0; triangle < 2; ++triangle) {
                maage_geometry_triangle *target;
                if (ordinal + triangle >= source_triangles || !original[ordinal + triangle]) goto malformed;
                target = &prepared->geometry.triangles[written];
                *target = *original[ordinal + triangle];
                memcpy(target->indices, data->indices + face * 6 + triangle * 3, sizeof(target->indices));
                target->order = written++;
                for (axis = 0; axis < 3; ++axis)
                    prepared->geometry.center[axis] += target->center[axis] / (counts[1] / 3);
            }
            ordinal += 2;
        }
        previous = source + 1;
    }
    free(original);
    prepared->data = *data;
    prepared->data.indices = prepared->indices;
    *output = prepared;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_UPLOAD, 0);
malformed:
    result = maage_status_make(MAAGE_STATUS_INVARIANT_VIOLATION, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_UPLOAD, 0);
    goto failed;
overflow:
    result = maage_status_make(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_UPLOAD, 0);
    goto failed;
allocation_failed:
    result = maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_UPLOAD, 0);
failed:
    free(original);
    maage_mesh_preparation_destroy(prepared);
    return result;
}

maage_status maage_mesh_preparation_estimate_bytes(size_t vertices, size_t indices, size_t *bytes) {
    size_t per_vertex = 3 * sizeof(maage_mesh_surface) + 7 * sizeof(unsigned int);
    size_t total = sizeof(maage_mesh_preparation), triangles = indices / 3;
    size_t table_vertices = vertices < 16 ? 16 : vertices;
    if (!bytes) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_QUERY, 0);
    *bytes = 0;
    /* Realloc may overlap old/new palettes, and rehash overlaps both maps. */
    if (table_vertices > ((size_t) - 1 - total) / per_vertex) goto overflow;
    total += table_vertices * per_vertex;
    if (indices > ((size_t) - 1 - total) / sizeof(unsigned int)) goto overflow;
    total += indices * sizeof(unsigned int);
    if (triangles > ((size_t) - 1 - total) / sizeof(maage_geometry_triangle)) goto overflow;
    *bytes = total + triangles * sizeof(maage_geometry_triangle);
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_QUERY, 0);
overflow:
    return maage_status_make(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_QUERY, 0);
}
size_t maage_mesh_preparation_resident_bytes(const maage_mesh_preparation *prepared) {
    if (!prepared) return 0;
    return sizeof(*prepared) + prepared->data.index_count * sizeof(*prepared->indices) + (prepared->surface_indices ? prepared->data.vertex_count * sizeof(*prepared->surface_indices) : 0) + prepared->surface_capacity * sizeof(*prepared->surfaces) + prepared->geometry.translucent_count / 3 * sizeof(*prepared->geometry.triangles);
}
maage_status maage_mesh_preparation_destroy(maage_mesh_preparation *prepared) {
    if (!prepared) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_DESTROY, 0);
    maage_geometry_dispose(&prepared->geometry);
    free(prepared->surfaces);
    free(prepared->surface_indices);
    free(prepared->indices);
    free(prepared);
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_DESTROY, 0);
}
