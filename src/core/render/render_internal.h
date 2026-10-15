#ifndef MAAGE_RENDER_INTERNAL_H
#define MAAGE_RENDER_INTERNAL_H
#include "maage/render.h"

typedef struct maage_render_backend_api {
    maage_render_backend kind;
    maage_render_result (*create)(maage_renderer *renderer);
    void (*destroy)(maage_renderer *renderer);
    maage_render_result (*present)(maage_renderer *renderer, const maage_window_state *state);
} maage_render_backend_api;
struct maage_renderer {
    maage_window *window;
    maage_window_native native;
    const maage_render_backend_api *api;
    void *platform;
    float clear[4];
    int refresh;
    maage_render_result failure;
};
extern const maage_render_backend_api maage_render_metal_api;
extern const maage_render_backend_api maage_render_vulkan_api;
#endif
