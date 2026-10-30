#ifndef MAAGE_OPENGL_INTERNAL_H
#define MAAGE_OPENGL_INTERNAL_H
#include "../render_internal.h"
#ifdef __APPLE__
#define GL_SILENCE_DEPRECATION
#include <OpenGL/gl.h>
#else
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
#include <GL/gl.h>
#endif

/* Context adapters own context; modern rendering owns gpu, including partial
 * initialization. Legacy-only builds need no modern graphics SDK headers. */
typedef struct maage_opengl_renderer {
    void *context;
    void *gpu;
} maage_opengl_renderer;
#ifdef _WIN32
#define MAAGE_GL_CALL APIENTRY
#else
#define MAAGE_GL_CALL
#endif
typedef void(MAAGE_GL_CALL *maage_opengl_proc)(void);

maage_status maage_opengl_context_create(maage_renderer *renderer, int modern);
/* Publish after complete GPU initialization. WGL delays the real window's
 * irreversible pixel-format assignment; other adapters attach at creation. */
maage_status maage_opengl_context_attach(maage_renderer *renderer);
void maage_opengl_context_destroy(maage_renderer *renderer);
maage_status maage_opengl_context_bind(maage_renderer *renderer);
maage_status maage_opengl_context_swap(maage_renderer *renderer);
maage_opengl_proc maage_opengl_context_proc(maage_renderer *renderer, const char *name);
maage_status maage_opengl_context_vsync(maage_renderer *renderer, int enabled);
int maage_opengl_extension(const char *extensions, const char *name);
maage_status maage_opengl_check(maage_renderer *renderer, unsigned char operation);
maage_status maage_opengl_create(maage_renderer *renderer, int modern);
maage_status maage_opengl_destroy(maage_renderer *renderer);
maage_status maage_opengl_present(maage_renderer *renderer, const maage_window_state *state);
#endif
