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
    const maage_render_instance *instance;
    size_t triangle; /* Original immutable index offset for a transformed triangle. */
    float depth;
    size_t order;
} maage_render_ordered_mesh;
enum {
    MAAGE_RENDER_SOLID, MAAGE_RENDER_TRANSLUCENT, MAAGE_RENDER_MULTIPLY,
    MAAGE_RENDER_UI, MAAGE_RENDER_ADDITIVE, MAAGE_RENDER_FULLSCREEN,
    MAAGE_RENDER_PIPELINE_COUNT
};
enum {
    MAAGE_RENDER_FACTOR_ONE, MAAGE_RENDER_FACTOR_SOURCE_ALPHA,
    MAAGE_RENDER_FACTOR_INVERSE_ALPHA, MAAGE_RENDER_FACTOR_SOURCE_COLOR,
    MAAGE_RENDER_FACTOR_DESTINATION_COLOR
};
typedef struct maage_render_pipeline_state {
    int ui, fullscreen, depth_test, depth_write, cull, blend;
    int source_rgb, destination_rgb;
} maage_render_pipeline_state;
extern const maage_render_pipeline_state maage_render_pipeline_states[MAAGE_RENDER_PIPELINE_COUNT];
typedef float maage_render_matrix[16];
typedef float maage_render_vector[4];
typedef struct maage_render_uniforms {
    MAAGE_RENDER_UNIFORM_FIELDS(maage_render_matrix, maage_render_vector)
} maage_render_uniforms;
typedef struct maage_render_ui_gpu_vertex {
    maage_render_ui_vertex vertex;
    float depth;
} maage_render_ui_gpu_vertex;
typedef struct maage_render_frame_plan {
    int preparing;
    const maage_render_mesh *const *meshes;
    size_t mesh_count;
    const maage_render_instance *instances;
    size_t instance_count;
    const maage_render_ordered_mesh *ordered;
    size_t ordered_count;
    const maage_render_ui_data *layers[3];
    const maage_render_mesh *foreground;
    maage_render_uniforms world, hand;
    size_t panorama_count;
} maage_render_frame_plan;
typedef struct maage_render_mesh_cursor {
    unsigned int layer;
    size_t index;
    const maage_render_instance *instance;
    size_t triangle;
} maage_render_mesh_cursor;

/* Input passed mesh_validate; destination holds index_count indices and geometry
 * starts zeroed. Native mapped destinations avoid an intermediate CPU copy. */
maage_status maage_render_geometry_prepare(maage_render_geometry *geometry,
    const maage_mesh_data *data, unsigned int *grouped_indices);
maage_status maage_render_geometry_dispose(maage_render_geometry *geometry);
/* Half-block movement gates perspective re-sorts. SUCCESS needs a native upload;
 * mark revisions[slot] only after it succeeds. NOOP keeps the slot unchanged. */
maage_status maage_render_geometry_sort_slot(const maage_render_mesh *mesh,
    const float matrix[16], unsigned long *revisions, size_t slots, size_t slot);
maage_status maage_render_geometry_write_sorted(const maage_render_geometry *geometry,
    unsigned int *indices);
size_t maage_render_geometry_range(const maage_render_geometry *geometry,
    unsigned int layer, size_t *first);
size_t maage_render_transient_capacity(size_t current, size_t required);
maage_status maage_render_frame_plan_prepare(maage_renderer *renderer,
    const maage_render_mesh *const *meshes, size_t count, const float matrix[16],
    const maage_render_ui_data *ui, int preparing, maage_render_frame_plan *plan);
maage_status maage_render_mesh_validate(const maage_mesh_data *data);
void maage_render_ui_uniforms(const maage_render_ui_data *batch,
    maage_render_uniforms *uniforms);
maage_status maage_render_ui_pack(const maage_render_ui_data *batch,
    maage_render_ui_gpu_vertex *vertices, unsigned int *indices);
/* Zero the cursor first. Visits nonempty material ranges in canonical order;
 * only world translucent meshes use distance order. NULL ends traversal. */
const maage_render_mesh *maage_render_frame_next_mesh(const maage_render_frame_plan *plan,
    int foreground, maage_render_mesh_cursor *cursor);
void maage_render_draw_uniforms(const maage_render_frame_plan *plan, int foreground,
    const maage_render_mesh_cursor *cursor, maage_render_uniforms *uniforms);
int maage_render_gpu_layout_supported(void);
#endif
