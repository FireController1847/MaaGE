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

maage_render_result maage_opengl_context_create(maage_renderer *renderer, int modern);
void maage_opengl_context_destroy(maage_renderer *renderer);
int maage_opengl_context_bind(maage_renderer *renderer);
int maage_opengl_context_swap(maage_renderer *renderer);
maage_render_result maage_opengl_create(maage_renderer *renderer, int modern);
void maage_opengl_destroy(maage_renderer *renderer);
maage_render_result maage_opengl_present(maage_renderer *renderer, const maage_window_state *state);
#endif
