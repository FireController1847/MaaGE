#ifndef MAAGE_GEOMETRY_H
#define MAAGE_GEOMETRY_H
#include <stddef.h>
#include "maage/status.h"
#include "maage/geometry_materials.h"
/* CPU geometry shared by mesh producers and graphics backends. UVs address a
 * top-left-origin straight-alpha RGBA texture; colors multiply sampled texels. */
typedef struct maage_mesh_vertex {
    float position[3];
    float uv[2];
    float color[4];
    float light[2]; /* Independent normalized static skylight and block emission. */
    float material;
    /* Atlas V stride, frame count (negative for ping-pong), ticks per frame.
     * Zero count is static. Frames and padding remain one immutable texture. */
    float animation[3];
    /* Optional atlas tile bounds (u0,v0,u1,v1). Positive spans repeat uv
     * inside this tile; zero spans preserve ordinary absolute atlas UVs.
     * Repeated triangles require identical bounds and animation at each corner. */
    float tile[4];
} maage_mesh_vertex;
typedef struct maage_mesh_data {
    const maage_mesh_vertex *vertices;
    size_t vertex_count;
    const unsigned int *indices;
    size_t index_count;
} maage_mesh_data;
/* CPU-only preparation may run on workers. It owns grouped indices, surface
 * tables and transparent metadata, borrowing immutable vertices through upload. */
typedef struct maage_mesh_preparation maage_mesh_preparation;
maage_status maage_mesh_prepare(const maage_mesh_data *data, maage_mesh_preparation **output);
maage_status maage_mesh_preparation_destroy(maage_mesh_preparation *prepared);
/* Conservative preparation peak (including table growth); excludes borrowed
 * source arrays. Resident bytes count only the preparation's owned storage. */
maage_status maage_mesh_preparation_estimate_bytes(size_t vertices, size_t indices, size_t *bytes);
size_t maage_mesh_preparation_resident_bytes(const maage_mesh_preparation *prepared);
#endif
