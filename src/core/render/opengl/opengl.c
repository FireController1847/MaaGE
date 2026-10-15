#include "opengl_internal.h"
#include <stdio.h>

maage_render_result maage_opengl_create(maage_renderer *renderer, int modern)
{
    maage_render_result result;
    const char *version;
    int major = 0, minor = 0;
    GLint maximum = 0, bits = 0;
    result = maage_opengl_context_create(renderer, modern);
    if (result != MAAGE_RENDER_OK) return result;
    if (!maage_opengl_context_bind(renderer)) return MAAGE_RENDER_PLATFORM_ERROR;
    version = (const char *)glGetString(GL_VERSION);
    if (!version || sscanf(version, "%d.%d", &major, &minor) != 2) return MAAGE_RENDER_UNAVAILABLE;
    if (modern ? (major < 3 || (major == 3 && minor < 3)) : (major < 1 || (major == 1 && minor < 1)))
        return MAAGE_RENDER_UNSUPPORTED;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maximum);
    renderer->capabilities.max_texture_dimension_2d = maximum > 0 ? (unsigned int)maximum : 0;
    renderer->capabilities.texture_formats = MAAGE_RENDER_FORMAT_RGBA8;
    renderer->capabilities.synchronization = MAAGE_RENDER_SYNC_CPU_WAIT;
    if (modern) {
        renderer->capabilities.features |= MAAGE_RENDER_CAP_SHADERS | MAAGE_RENDER_CAP_VERTEX_BUFFERS |
            MAAGE_RENDER_CAP_INDEX_BUFFERS | MAAGE_RENDER_CAP_RENDER_TARGETS | MAAGE_RENDER_CAP_INSTANCING;
        /* Core 3.3 guarantees depth/stencil texture/renderbuffer formats. */
        renderer->capabilities.features |= MAAGE_RENDER_CAP_DEPTH | MAAGE_RENDER_CAP_STENCIL;
    } else {
        renderer->capabilities.features |= MAAGE_RENDER_CAP_FIXED_FUNCTION;
        glGetIntegerv(GL_DEPTH_BITS, &bits);
        if (bits > 0) renderer->capabilities.features |= MAAGE_RENDER_CAP_DEPTH;
        glGetIntegerv(GL_STENCIL_BITS, &bits);
        if (bits > 0) renderer->capabilities.features |= MAAGE_RENDER_CAP_STENCIL;
    }
    return glGetError() == GL_NO_ERROR ? MAAGE_RENDER_OK : MAAGE_RENDER_PLATFORM_ERROR;
}
void maage_opengl_destroy(maage_renderer *renderer) { maage_opengl_context_destroy(renderer); }
maage_render_result maage_opengl_present(maage_renderer *renderer, const maage_window_state *state)
{
    if (!maage_opengl_context_bind(renderer)) return MAAGE_RENDER_PLATFORM_ERROR;
    glViewport(0, 0, state->framebuffer_width, state->framebuffer_height);
    glClearColor(renderer->clear[0], renderer->clear[1], renderer->clear[2], renderer->clear[3]);
    glClear(GL_COLOR_BUFFER_BIT);
    glFinish();
    if (glGetError() != GL_NO_ERROR) return MAAGE_RENDER_PLATFORM_ERROR;
    renderer->refresh = 0;
    return maage_opengl_context_swap(renderer) ? MAAGE_RENDER_OK : MAAGE_RENDER_PLATFORM_ERROR;
}
