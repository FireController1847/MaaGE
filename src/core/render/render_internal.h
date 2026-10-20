#ifndef MAAGE_RENDER_INTERNAL_H
#define MAAGE_RENDER_INTERNAL_H
#include "maage/render.h"

typedef struct maage_render_backend_api {
    maage_render_backend kind;
    maage_render_result (*create)(maage_renderer *renderer);
    void (*destroy)(maage_renderer *renderer);
    maage_render_result (*present)(maage_renderer *renderer, const maage_window_state *state);
    unsigned int version;
    maage_render_result (*prepare_frame)(maage_renderer *renderer, const maage_window_state *state);
    maage_render_result (*upload_mesh)(maage_renderer *renderer, maage_render_mesh *mesh,
        const maage_mesh_data *data, const maage_pixel_buffer *atlas);
    void (*destroy_mesh)(maage_render_mesh *mesh);
    maage_render_result (*draw_mesh)(maage_renderer *renderer, const maage_render_mesh *mesh,
        const maage_window_state *state, const float view_projection[16]);
} maage_render_backend_api;
struct maage_render_mesh {
    maage_renderer *renderer;
    void *platform;
    maage_render_mesh *next;
    maage_render_mesh *previous;
};
struct maage_renderer {
    maage_window *window;
    maage_window_native native;
    const maage_render_backend_api *api;
    void *platform;
    float clear[4];
    int refresh;
    maage_render_result failure;
    maage_render_capabilities capabilities;
    maage_render_mesh *meshes;
};
extern const maage_render_backend_api maage_render_metal_api;
extern const maage_render_backend_api maage_render_vulkan_api;
extern const maage_render_backend_api maage_render_opengl_api;
extern const maage_render_backend_api maage_render_opengl_legacy_api;
extern const maage_render_backend_api maage_render_software_api;
#endif
