#ifndef MAAGE_RENDER_INTERNAL_H
#define MAAGE_RENDER_INTERNAL_H
#include "render_shared.h"

typedef struct maage_render_backend_api {
    maage_render_backend kind;
    maage_status (*create)(maage_renderer *renderer);
    maage_status (*destroy)(maage_renderer *renderer);
    unsigned int version;
    maage_status (*present)(maage_renderer *renderer, const maage_window_state *state);
    maage_status (*frame)(maage_renderer *renderer, const maage_window_state *state,
        const maage_render_frame_plan *plan);
    maage_status (*upload_geometry)(maage_renderer *renderer, maage_render_mesh *mesh,
        const maage_mesh_data *data);
    void (*destroy_mesh)(maage_render_mesh *mesh);
    maage_status (*create_texture)(maage_renderer *renderer,
        maage_render_texture *texture, const maage_pixel_buffer *atlas);
    void (*destroy_texture)(maage_render_texture *texture);
    maage_status (*set_vsync)(maage_renderer *renderer, int enabled);
} maage_render_backend_api;
struct maage_render_texture {
    maage_renderer *renderer;
    void *platform;
    size_t references;
    int owned;
    maage_render_texture *next;
    maage_render_texture *previous;
};
struct maage_render_mesh {
    maage_renderer *renderer;
    maage_render_texture *texture;
    void *platform;
    maage_render_geometry geometry;
    maage_render_mesh *next;
    maage_render_mesh *previous;
};
struct maage_renderer {
    maage_window *window;
    maage_window_native native;
    const maage_render_backend_api *api;
    void *platform;
    float clear[4], sky_light;
    const maage_render_ui_data *background, *overlay;
    const maage_render_mesh *foreground;
    const maage_render_instance *instances;
    size_t instance_count;
    float foreground_matrix[16];
    float foreground_light[2], animation_tick;
    int refresh;
    maage_status failure;
    /* Borrowed only during a synchronous backend call; always cleared before
     * returning to the caller. Backends never retain it in GPU work. */
    maage_status_trace *operation_trace;
    unsigned char trace_operation;
    maage_render_capabilities capabilities;
    maage_render_mesh *meshes;
    maage_render_texture *textures;
    maage_render_ordered_mesh *ordered;
    size_t ordered_capacity;
    char device_name[256];
};
extern const maage_render_backend_api maage_render_metal_api;
extern const maage_render_backend_api maage_render_vulkan_api;
extern const maage_render_backend_api maage_render_opengl_api;
extern const maage_render_backend_api maage_render_opengl_legacy_api;
extern const maage_render_backend_api maage_render_software_api;
#endif
