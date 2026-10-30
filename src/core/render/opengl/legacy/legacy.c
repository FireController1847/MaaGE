#include "../opengl_internal.h"
static maage_status maage_opengl_legacy_create(maage_renderer *renderer) {
    return maage_opengl_create(renderer, 0);
}
const maage_render_backend_api maage_render_opengl_legacy_api = {
    MAAGE_RENDER_OPENGL_LEGACY, maage_opengl_legacy_create, maage_opengl_destroy,
    MAAGE_RENDER_CAPABILITIES_VERSION, maage_opengl_present, NULL, NULL, NULL, NULL, NULL, maage_opengl_context_vsync};
