#include "../opengl_internal.h"
static maage_status maage_opengl_modern_create(maage_renderer *renderer) {
    return maage_opengl_create(renderer, 1);
}
const maage_render_backend_api maage_render_opengl_api = {
    MAAGE_RENDER_OPENGL, maage_opengl_modern_create, maage_opengl_destroy,
    MAAGE_RENDER_CAPABILITIES_VERSION, maage_opengl_present, NULL, NULL, NULL, NULL, NULL, NULL};
