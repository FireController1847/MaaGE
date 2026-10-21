#ifndef MAAGE_GEOMETRY_H
#define MAAGE_GEOMETRY_H
#include <stddef.h>
/* CPU geometry shared by mesh producers and graphics backends. UVs address a
 * top-left-origin RGBA texture; colors multiply the sampled texel. */
typedef struct maage_mesh_vertex {
    float position[3];
    float normal[3];
    float uv[2];
    float color[4];
    float light[2]; /* Independent normalized static skylight and block emission. */
} maage_mesh_vertex;
typedef struct maage_mesh_data {
    const maage_mesh_vertex *vertices;
    size_t vertex_count;
    const unsigned int *indices;
    size_t index_count;
} maage_mesh_data;
#endif
