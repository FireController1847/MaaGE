#ifndef MAAGE_GEOMETRY_H
#define MAAGE_GEOMETRY_H
#include <stddef.h>
/* Numeric surface behavior, constant across a triangle. Translucent surfaces
 * blend after solid/cutout geometry with depth testing and no depth writes. */
#define MAAGE_MESH_MATERIAL_OPAQUE 0
#define MAAGE_MESH_MATERIAL_CUTOUT 1
#define MAAGE_MESH_MATERIAL_TRANSLUCENT 2
#define MAAGE_MESH_MATERIAL_MULTIPLY 3
/* CPU geometry shared by mesh producers and graphics backends. UVs address a
 * top-left-origin straight-alpha RGBA texture; colors multiply sampled texels. */
typedef struct maage_mesh_vertex {
    float position[3];
    float normal[3];
    float uv[2];
    float color[4];
    float light[2]; /* Independent normalized static skylight and block emission. */
    float material;
    /* Atlas V stride, frame count (negative for ping-pong), ticks per frame.
     * Zero count is static. Frames and padding remain one immutable texture. */
    float animation[3];
} maage_mesh_vertex;
typedef struct maage_mesh_data {
    const maage_mesh_vertex *vertices;
    size_t vertex_count;
    const unsigned int *indices;
    size_t index_count;
} maage_mesh_data;
#endif
