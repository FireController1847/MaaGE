#include "opengl_internal.h"
#include "maage/log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int maage_opengl_extension(const char *extensions, const char *name) {
    const char *match;
    size_t length = strlen(name);
    if (!extensions || !length) return 0;
    match = extensions;
    while ((match = strstr(match, name)) != NULL) {
        if ((match == extensions || match[-1] == ' ') && (match[length] == ' ' || !match[length])) return 1;
        match += length;
    }
    return 0;
}
maage_status maage_opengl_check(maage_renderer *renderer, unsigned char operation) {
    GLenum native = glGetError();
    maage_status result;
    unsigned int remaining = 16;
    if (native == GL_NO_ERROR) return maage_status_make(MAAGE_STATUS_SUCCESS, 0, operation, 0);
    result = maage_status_make(native == GL_OUT_OF_MEMORY ? MAAGE_STATUS_ALLOCATION_FAILED : MAAGE_STATUS_ERROR, MAAGE_CONTEXT_RENDER, operation, 0);
    /* Retain the first cause and consume this operation's remaining flags. */
    while (remaining-- && glGetError() != GL_NO_ERROR) {
    }
    return maage_status_trace_record(renderer->operation_trace, result, MAAGE_CONTEXT_RENDER, operation, __FILE__, __LINE__, (long) native);
}
maage_status maage_opengl_create(maage_renderer *renderer, int modern) {
    maage_opengl_renderer *native;
    maage_status result;
    const char *version, *device, *vendor;
    int major = 0, minor = 0;
    GLint maximum = 0, bits = 0;
    native = (maage_opengl_renderer *) calloc(1, sizeof(*native));
    if (!native) return MAAGE_STATUS_VALUE(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_INITIALIZE, 0);
    renderer->platform = native;
    result = maage_opengl_context_create(renderer, modern);
    if (maage_status_code(result) != MAAGE_STATUS_SUCCESS) return MAAGE_STATUS_PROPAGATE(renderer->operation_trace, result, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_INITIALIZE);
    result = maage_opengl_context_bind(renderer);
    if (maage_status_failed(result)) return MAAGE_STATUS_PROPAGATE(renderer->operation_trace, result, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_INITIALIZE);
    version = (const char *) glGetString(GL_VERSION);
    device = (const char *) glGetString(GL_RENDERER);
    vendor = (const char *) glGetString(GL_VENDOR);
    maage_log_info("maage.render.opengl", "OpenGL version: %.160s; device: %.200s; vendor: %.160s.", version ? version : "unavailable", device ? device : "unavailable", vendor ? vendor : "unavailable");
    if (!version || sscanf(version, "%d.%d", &major, &minor) != 2 || (modern ? (major < 3 || (major == 3 && minor < 3)) : (major < 1 || (major == 1 && minor < 1)))) {
        maage_log_warning("maage.render.opengl", "Required OpenGL version %s is unavailable.", modern ? "3.3 Core" : "1.1");
        return MAAGE_STATUS_VALUE(MAAGE_STATUS_SYSTEM_NOT_SUPPORTED, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_INITIALIZE, 0);
    }
    if (device) {
        /* F3 borrows this existing interface; complete driver strings also go
         * to the log. Bound each component so version/vendor remain visible. */
        sprintf(renderer->device_name, "%.128s | %.64s | GL %.48s", device, vendor ? vendor : "unknown", version);
    }
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maximum);
    renderer->capabilities.max_texture_dimension_2d = maximum > 0 ? (unsigned int) maximum : 0;
    renderer->capabilities.texture_formats = MAAGE_RENDER_FORMAT_RGBA8;
    if (!modern) {
        renderer->capabilities.features |= MAAGE_RENDER_CAP_FIXED_FUNCTION;
        glGetIntegerv(GL_DEPTH_BITS, &bits);
        if (bits > 0) renderer->capabilities.features |= MAAGE_RENDER_CAP_DEPTH;
        glGetIntegerv(GL_STENCIL_BITS, &bits);
        if (bits > 0) renderer->capabilities.features |= MAAGE_RENDER_CAP_STENCIL;
        result = maage_opengl_context_attach(renderer);
        if (maage_status_failed(result)) return result;
    }
    return maage_opengl_check(renderer, MAAGE_OPERATION_INITIALIZE);
}
maage_status maage_opengl_destroy(maage_renderer *renderer) {
    if (renderer->platform) {
        maage_opengl_context_destroy(renderer);
        free(renderer->platform);
        renderer->platform = NULL;
    }
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_DESTROY, 0);
}
maage_status maage_opengl_present(maage_renderer *renderer, const maage_window_state *state) {
    maage_status result = maage_opengl_context_bind(renderer);
    if (maage_status_failed(result)) return MAAGE_STATUS_PROPAGATE(renderer->operation_trace, result, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_PRESENT);
    glViewport(0, 0, state->framebuffer_width, state->framebuffer_height);
    glClearColor(renderer->clear[0], renderer->clear[1], renderer->clear[2], renderer->clear[3]);
    glClear(GL_COLOR_BUFFER_BIT);
    result = maage_opengl_check(renderer, MAAGE_OPERATION_PRESENT);
    if (maage_status_failed(result)) return result;
    renderer->refresh = 0;
    return maage_opengl_context_swap(renderer);
}
