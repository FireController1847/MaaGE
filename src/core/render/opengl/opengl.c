#include "opengl_internal.h"
#include <stdio.h>

maage_status maage_opengl_create(maage_renderer *renderer, int modern) {
    maage_status result;
    const char *version;
    int major = 0, minor = 0;
    GLint maximum = 0, bits = 0;
    GLenum native;
    result = maage_opengl_context_create(renderer, modern);
    if (maage_status_code(result) != MAAGE_STATUS_SUCCESS) return result;
    result = maage_opengl_context_bind(renderer);
    if (maage_status_failed(result)) return MAAGE_STATUS_PROPAGATE(renderer->operation_trace, result, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_INITIALIZE);
    version = (const char *) glGetString(GL_VERSION);
    if (!version || sscanf(version, "%d.%d", &major, &minor) != 2) return MAAGE_STATUS_VALUE(MAAGE_STATUS_SYSTEM_NOT_SUPPORTED, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_INITIALIZE, 0);
    if (modern ? (major < 3 || (major == 3 && minor < 3)) : (major < 1 || (major == 1 && minor < 1))) return MAAGE_STATUS_VALUE(MAAGE_STATUS_NOT_IMPLEMENTED, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_INITIALIZE, 0);
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maximum);
    renderer->capabilities.max_texture_dimension_2d = maximum > 0 ? (unsigned int) maximum : 0;
    renderer->capabilities.texture_formats = MAAGE_RENDER_FORMAT_RGBA8;
    renderer->capabilities.synchronization = MAAGE_RENDER_SYNC_CPU_WAIT;
    if (modern) {
        renderer->capabilities.features |= MAAGE_RENDER_CAP_SHADERS | MAAGE_RENDER_CAP_VERTEX_BUFFERS | MAAGE_RENDER_CAP_INDEX_BUFFERS | MAAGE_RENDER_CAP_RENDER_TARGETS | MAAGE_RENDER_CAP_INSTANCING;
        /* Core 3.3 guarantees depth/stencil texture/renderbuffer formats. */
        renderer->capabilities.features |= MAAGE_RENDER_CAP_DEPTH | MAAGE_RENDER_CAP_STENCIL;
    } else {
        renderer->capabilities.features |= MAAGE_RENDER_CAP_FIXED_FUNCTION;
        glGetIntegerv(GL_DEPTH_BITS, &bits);
        if (bits > 0) renderer->capabilities.features |= MAAGE_RENDER_CAP_DEPTH;
        glGetIntegerv(GL_STENCIL_BITS, &bits);
        if (bits > 0) renderer->capabilities.features |= MAAGE_RENDER_CAP_STENCIL;
    }
    native = glGetError();
    result = maage_status_make(native == GL_NO_ERROR ? MAAGE_STATUS_SUCCESS : MAAGE_STATUS_ERROR, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_INITIALIZE, 0);
    if (native != GL_NO_ERROR) maage_status_trace_record(renderer->operation_trace, result, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_INITIALIZE, __FILE__, __LINE__, (long) native);
    return result;
}
maage_status maage_opengl_destroy(maage_renderer *renderer) {
    maage_opengl_context_destroy(renderer);
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_DESTROY, 0);
}
maage_status maage_opengl_present(maage_renderer *renderer, const maage_window_state *state) {
    GLenum native;
    maage_status result = maage_opengl_context_bind(renderer);
    if (maage_status_failed(result)) return MAAGE_STATUS_PROPAGATE(renderer->operation_trace, result, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_PRESENT);
    glViewport(0, 0, state->framebuffer_width, state->framebuffer_height);
    glClearColor(renderer->clear[0], renderer->clear[1], renderer->clear[2], renderer->clear[3]);
    glClear(GL_COLOR_BUFFER_BIT);
    glFinish();
    native = glGetError();
    if (native != GL_NO_ERROR) return maage_status_trace_record(renderer->operation_trace, maage_status_make(MAAGE_STATUS_ERROR, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_PRESENT, 0), MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_PRESENT, __FILE__, __LINE__, (long) native);
    renderer->refresh = 0;
    return maage_opengl_context_swap(renderer);
}
