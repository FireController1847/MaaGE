#ifndef MAAGE_OPENGL_API_H
#define MAAGE_OPENGL_API_H
#include "../opengl_internal.h"
#include <stddef.h>

/* Private 3.3 ABI declarations keep Windows' 1.1 SDK and legacy-only builds
 * independent of extension headers/loaders. Every entry is resolved per context. */
typedef struct __GLsync *maage_gl_sync;
#define MAAGE_GL_FUNCTIONS(F) \
    F(void, GenBuffers, (GLsizei, GLuint *)) \
    F(void, DeleteBuffers, (GLsizei, const GLuint *)) \
    F(void, BindBuffer, (GLenum, GLuint)) \
    F(void, BufferData, (GLenum, ptrdiff_t, const void *, GLenum)) \
    F(void *, MapBufferRange, (GLenum, ptrdiff_t, ptrdiff_t, GLbitfield)) \
    F(GLboolean, UnmapBuffer, (GLenum)) \
    F(void, GenVertexArrays, (GLsizei, GLuint *)) \
    F(void, DeleteVertexArrays, (GLsizei, const GLuint *)) \
    F(void, BindVertexArray, (GLuint)) \
    F(void, EnableVertexAttribArray, (GLuint)) \
    F(void, VertexAttribPointer, (GLuint, GLint, GLenum, GLboolean, GLsizei, const void *)) \
    F(GLuint, CreateShader, (GLenum)) \
    F(void, DeleteShader, (GLuint)) \
    F(void, ShaderSource, (GLuint, GLsizei, const char *const *, const GLint *)) \
    F(void, CompileShader, (GLuint)) \
    F(void, GetShaderiv, (GLuint, GLenum, GLint *)) \
    F(void, GetShaderInfoLog, (GLuint, GLsizei, GLsizei *, char *)) \
    F(GLuint, CreateProgram, (void)) \
    F(void, DeleteProgram, (GLuint)) \
    F(void, AttachShader, (GLuint, GLuint)) \
    F(void, LinkProgram, (GLuint)) \
    F(void, GetProgramiv, (GLuint, GLenum, GLint *)) \
    F(void, GetProgramInfoLog, (GLuint, GLsizei, GLsizei *, char *)) \
    F(void, UseProgram, (GLuint)) \
    F(GLint, GetUniformLocation, (GLuint, const char *)) \
    F(void, UniformMatrix4fv, (GLint, GLsizei, GLboolean, const GLfloat *)) \
    F(void, Uniform4fv, (GLint, GLsizei, const GLfloat *)) \
    F(void, Uniform1i, (GLint, GLint)) \
    F(void, ActiveTexture, (GLenum)) \
    F(void, BlendFuncSeparate, (GLenum, GLenum, GLenum, GLenum)) \
    F(void, BlendEquation, (GLenum)) \
    F(void, GenFramebuffers, (GLsizei, GLuint *)) \
    F(void, DeleteFramebuffers, (GLsizei, const GLuint *)) \
    F(void, BindFramebuffer, (GLenum, GLuint)) \
    F(void, FramebufferTexture2D, (GLenum, GLenum, GLenum, GLuint, GLint)) \
    F(GLenum, CheckFramebufferStatus, (GLenum)) \
    F(void, GenRenderbuffers, (GLsizei, GLuint *)) \
    F(void, DeleteRenderbuffers, (GLsizei, const GLuint *)) \
    F(void, BindRenderbuffer, (GLenum, GLuint)) \
    F(void, RenderbufferStorage, (GLenum, GLenum, GLsizei, GLsizei)) \
    F(void, GetRenderbufferParameteriv, (GLenum, GLenum, GLint *)) \
    F(void, FramebufferRenderbuffer, (GLenum, GLenum, GLenum, GLuint)) \
    F(maage_gl_sync, FenceSync, (GLenum, GLbitfield)) \
    F(void, GetSynciv, (maage_gl_sync, GLenum, GLsizei, GLsizei *, GLint *)) \
    F(void, DeleteSync, (maage_gl_sync))
#define MAAGE_GL_DECLARE(result, name, arguments) typedef result (MAAGE_GL_CALL *maage_gl_##name) arguments;
MAAGE_GL_FUNCTIONS(MAAGE_GL_DECLARE)
#undef MAAGE_GL_DECLARE

typedef struct maage_gl_api {
#define MAAGE_GL_FIELD(result, name, arguments) maage_gl_##name name;
    MAAGE_GL_FUNCTIONS(MAAGE_GL_FIELD)
#undef MAAGE_GL_FIELD
} maage_gl_api;

enum {
    MAAGE_GL_ARRAY_BUFFER = 0x8892,
    MAAGE_GL_ELEMENT_ARRAY_BUFFER = 0x8893,
    MAAGE_GL_STATIC_DRAW = 0x88E4,
    MAAGE_GL_STREAM_DRAW = 0x88E0,
    MAAGE_GL_MAP_WRITE_BIT = 0x0002,
    MAAGE_GL_MAP_INVALIDATE_BUFFER_BIT = 0x0008,
    MAAGE_GL_MAP_UNSYNCHRONIZED_BIT = 0x0020,
    MAAGE_GL_VERTEX_SHADER = 0x8B31,
    MAAGE_GL_FRAGMENT_SHADER = 0x8B30,
    MAAGE_GL_COMPILE_STATUS = 0x8B81,
    MAAGE_GL_LINK_STATUS = 0x8B82,
    MAAGE_GL_TEXTURE0 = 0x84C0,
    MAAGE_GL_CLAMP_TO_EDGE = 0x812F,
    MAAGE_GL_RGBA8 = 0x8058,
    MAAGE_GL_FUNC_ADD = 0x8006,
    MAAGE_GL_FRAMEBUFFER = 0x8D40,
    MAAGE_GL_FRAMEBUFFER_COMPLETE = 0x8CD5,
    MAAGE_GL_FRAMEBUFFER_SRGB = 0x8DB9,
    MAAGE_GL_COLOR_ATTACHMENT0 = 0x8CE0,
    MAAGE_GL_DEPTH_STENCIL_ATTACHMENT = 0x821A,
    MAAGE_GL_DEPTH24_STENCIL8 = 0x88F0,
    MAAGE_GL_RENDERBUFFER = 0x8D41,
    MAAGE_GL_RENDERBUFFER_DEPTH_SIZE = 0x8D54,
    MAAGE_GL_RENDERBUFFER_STENCIL_SIZE = 0x8D55,
    MAAGE_GL_SYNC_GPU_COMMANDS_COMPLETE = 0x9117,
    MAAGE_GL_SYNC_STATUS = 0x9114,
    MAAGE_GL_SIGNALED = 0x9119,
    MAAGE_GL_MAX_VERTEX_ATTRIBS = 0x8869,
    MAAGE_GL_MAX_TEXTURE_IMAGE_UNITS = 0x8872,
    MAAGE_GL_MAX_VERTEX_UNIFORM_COMPONENTS = 0x8B4A,
    MAAGE_GL_MAX_FRAGMENT_UNIFORM_COMPONENTS = 0x8B49,
    MAAGE_GL_MAX_VARYING_COMPONENTS = 0x8B4B,
    MAAGE_GL_MAX_RENDERBUFFER_SIZE = 0x84E8,
    MAAGE_GL_CONTEXT_PROFILE_MASK = 0x9126,
    MAAGE_GL_CONTEXT_CORE_PROFILE_BIT = 0x0001,
    MAAGE_GL_SHADING_LANGUAGE_VERSION = 0x8B8C
};
#endif
