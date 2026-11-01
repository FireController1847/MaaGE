#ifndef MAAGE_INTERNAL_GEOMETRY_H
#define MAAGE_INTERNAL_GEOMETRY_H
#include "maage/geometry.h"

/* CPU-only material grouping and transparent triangle metadata. */
typedef struct maage_geometry_triangle {
    unsigned int indices[3];
    float center[3], depth;
    unsigned int order; /* Source triangle order fits the validated 32-bit mesh index domain. */
} maage_geometry_triangle;
typedef struct maage_geometry {
    size_t solid_count, translucent_count, multiply_count;
    maage_geometry_triangle *triangles;
    float center[3];
    double sort_origin[3];
    unsigned long revision;
} maage_geometry;

struct maage_mesh_preparation {
    maage_mesh_data data;
    unsigned int *indices;
    maage_geometry geometry;
};
maage_status maage_geometry_dispose(maage_geometry *geometry);
/* Prepare filtered, index-only geometry from a retained GPU mesh's immutable
 * triangle metadata after the source CPU vertex arrays have been released. */
maage_status maage_mesh_prepare_index_view(const maage_mesh_data *data, const unsigned char *face_flags, const maage_geometry *base, maage_mesh_preparation **output);
#endif
