#ifndef MAAGE_RENDER_SHARED_H
#define MAAGE_RENDER_SHARED_H
#include "maage/render.h"
#include "render_shader_defs.h"

/* Owned CPU preparation; GPU objects and retired-frame tracking stay native. */
typedef struct maage_render_triangle {
    unsigned int indices[3];
    float center[3], depth;
    size_t order;
} maage_render_triangle;
typedef struct maage_render_geometry {
    size_t solid_count, translucent_count, multiply_count;
    maage_render_triangle *triangles;
    float center[3];
    double sort_origin[3];
    unsigned long revision;
} maage_render_geometry;
typedef struct maage_render_ordered_mesh {
    const maage_render_mesh *mesh;
    float depth;
    size_t order;
} maage_render_ordered_mesh;
typedef struct maage_render_frame_plan {
    const maage_render_ui_data *layers[3];
    const maage_render_mesh *foreground;
    size_t panorama_count;
} maage_render_frame_plan;

/* Destination holds data->index_count indices; geometry must start zeroed. */
maage_status maage_render_geometry_prepare(maage_render_geometry *geometry,
    const maage_mesh_data *data, unsigned int *grouped_indices);
maage_status maage_render_geometry_dispose(maage_render_geometry *geometry);
/* SUCCESS changes ordering: invalidate native slot revisions before reuse.
 * NOOP keeps ordering. Half-block movement gates perspective re-sorts. */
maage_status maage_render_geometry_sort(maage_render_geometry *geometry,
    const float matrix[16]);
maage_status maage_render_geometry_write_sorted(const maage_render_geometry *geometry,
    unsigned int *indices);
int maage_render_camera_origin(const float matrix[16], double origin[3]);
float maage_render_depth(const float center[3], const float matrix[16],
    const double origin[3], int perspective);
int maage_render_mesh_order(const void *left, const void *right);
size_t maage_render_transient_capacity(size_t current, size_t required);
maage_status maage_render_frame_plan_prepare(const maage_renderer *renderer,
    const maage_render_ui_data *ui, int preparing, maage_render_frame_plan *plan);
#endif
