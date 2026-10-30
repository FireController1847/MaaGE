#include "gl_api.h"
#include "maage/log.h"
#include "maage_opengl_shaders.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAAGE_GL_FRAME_SLOTS 3
#define MAAGE_GL_TRY(expression) \
    do { \
        maage_status maage_gl_result_ = (expression); \
        if (maage_status_failed(maage_gl_result_)) return MAAGE_STATUS_PROPAGATE(renderer->operation_trace, maage_gl_result_, MAAGE_CONTEXT_RENDER, renderer->trace_operation); \
    } while (0)

typedef struct maage_gl_buffer {
    GLuint name;
    size_t capacity;
} maage_gl_buffer;
typedef struct maage_gl_stream {
    GLuint vao;
    maage_gl_buffer vertices, indices;
} maage_gl_stream;
typedef struct maage_gl_target {
    GLuint framebuffer, color, depth;
    int width, height;
} maage_gl_target;
typedef struct maage_gl_program {
    GLuint name;
    GLint matrix, lighting, parameters;
} maage_gl_program;
typedef struct maage_gl_frame {
    maage_gl_sync fence;
    maage_gl_stream ui[3];
    maage_gl_target scene, panorama[2];
} maage_gl_frame;
typedef struct maage_gl_renderer {
    maage_gl_api gl;
    int loaded, pipeline;
    GLuint program, texture, vao, empty_vao;
    maage_gl_program mesh_program, ui_program, fullscreen_program;
    GLint target_limit, viewport_limit[2];
    maage_gl_frame frames[MAAGE_GL_FRAME_SLOTS];
    unsigned int slot;
} maage_gl_renderer;
typedef struct maage_gl_mesh {
    GLuint vao, vertices, indices;
    /* Foreground and world can reference the same mesh with different cameras. */
    maage_gl_buffer sorted[MAAGE_GL_FRAME_SLOTS * 2];
    unsigned long sorted_revision[MAAGE_GL_FRAME_SLOTS * 2];
} maage_gl_mesh;
typedef struct maage_gl_texture {
    GLuint name;
} maage_gl_texture;

static maage_gl_renderer *maage_gl_gpu(maage_renderer *renderer) {
    return ((maage_opengl_renderer *) renderer->platform)->gpu;
}
static maage_status maage_gl_resource_failure(maage_renderer *renderer, unsigned char operation) {
    maage_status result = maage_opengl_check(renderer, operation);
    return maage_status_failed(result) ? result : MAAGE_STATUS_VALUE(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_RENDER, operation, 0);
}
static maage_status maage_gl_load(maage_renderer *renderer) {
    maage_gl_renderer *native = maage_gl_gpu(renderer);
    int missing = 0;
#define MAAGE_GL_LOAD(result, name, arguments) \
    native->gl.name = (maage_gl_##name) maage_opengl_context_proc(renderer, "gl" #name); \
    if (!native->gl.name) { maage_log_warning("maage.render.opengl", "Required function gl%s is unavailable.", #name); missing = 1; }
    MAAGE_GL_FUNCTIONS(MAAGE_GL_LOAD)
#undef MAAGE_GL_LOAD
    if (missing) return MAAGE_STATUS_VALUE(MAAGE_STATUS_SYSTEM_NOT_SUPPORTED, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_INITIALIZE, 0);
    native->loaded = 1;
    return MAAGE_STATUS_VALUE(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_INITIALIZE, 0);
}
static maage_status maage_gl_shader(maage_renderer *renderer, GLenum stage, const char *source, const char *label, GLuint *output) {
    maage_gl_renderer *native = maage_gl_gpu(renderer);
    char message[2048];
    GLint compiled = 0;
    GLuint shader = native->gl.CreateShader(stage);
    if (!shader) return maage_gl_resource_failure(renderer, MAAGE_OPERATION_INITIALIZE);
    native->gl.ShaderSource(shader, 1, &source, NULL);
    native->gl.CompileShader(shader);
    native->gl.GetShaderiv(shader, MAAGE_GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
        message[0] = 0;
        native->gl.GetShaderInfoLog(shader, sizeof(message), NULL, message);
        message[sizeof(message) - 1] = 0;
        maage_log_error("maage.render.opengl", "GLSL %s %s compilation failed: %s", label, stage == MAAGE_GL_VERTEX_SHADER ? "vertex" : "fragment", message);
        native->gl.DeleteShader(shader);
        return MAAGE_STATUS_VALUE(MAAGE_STATUS_DEPENDENCY_FAILURE, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_INITIALIZE, 0);
    }
    *output = shader;
    return maage_opengl_check(renderer, MAAGE_OPERATION_INITIALIZE);
}
static maage_status maage_gl_program_create(maage_renderer *renderer, maage_gl_program *program, const char *vertex, const char *fragment, int mesh, const char *label) {
    maage_gl_renderer *native = maage_gl_gpu(renderer);
    maage_status result;
    GLuint shaders[2] = {0, 0};
    GLint linked = 0, sampler;
    char message[2048];
    result = maage_gl_shader(renderer, MAAGE_GL_VERTEX_SHADER, vertex, label, &shaders[0]);
    if (!maage_status_failed(result)) result = maage_gl_shader(renderer, MAAGE_GL_FRAGMENT_SHADER, fragment, label, &shaders[1]);
    if (!maage_status_failed(result)) {
        program->name = native->gl.CreateProgram();
        if (!program->name) result = maage_gl_resource_failure(renderer, MAAGE_OPERATION_INITIALIZE);
        else {
            native->gl.AttachShader(program->name, shaders[0]);
            native->gl.AttachShader(program->name, shaders[1]);
            native->gl.LinkProgram(program->name);
            native->gl.GetProgramiv(program->name, MAAGE_GL_LINK_STATUS, &linked);
            if (!linked) {
                message[0] = 0;
                native->gl.GetProgramInfoLog(program->name, sizeof(message), NULL, message);
                message[sizeof(message) - 1] = 0;
                maage_log_error("maage.render.opengl", "GLSL %s program linking failed: %s", label, message);
                result = MAAGE_STATUS_VALUE(MAAGE_STATUS_DEPENDENCY_FAILURE, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_INITIALIZE, 0);
            }
        }
    }
    if (shaders[0]) native->gl.DeleteShader(shaders[0]);
    if (shaders[1]) native->gl.DeleteShader(shaders[1]);
    if (maage_status_failed(result)) return result;
    program->matrix = native->gl.GetUniformLocation(program->name, "maage.matrix");
    program->lighting = native->gl.GetUniformLocation(program->name, "maage.lighting");
    program->parameters = native->gl.GetUniformLocation(program->name, "maage.parameters");
    sampler = native->gl.GetUniformLocation(program->name, "maage_source");
    if (sampler < 0 || program->parameters < 0 || (mesh && (program->matrix < 0 || program->lighting < 0))) {
        maage_log_warning("maage.render.opengl", "GLSL %s program is missing required active uniforms.", label);
        return MAAGE_STATUS_VALUE(MAAGE_STATUS_DEPENDENCY_FAILURE, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_INITIALIZE, 0);
    }
    native->gl.UseProgram(program->name);
    native->gl.Uniform1i(sampler, 0);
    return maage_opengl_check(renderer, MAAGE_OPERATION_INITIALIZE);
}
static void maage_gl_target_destroy(maage_gl_renderer *native, maage_gl_target *target) {
    if (target->framebuffer) native->gl.DeleteFramebuffers(1, &target->framebuffer);
    if (target->depth) native->gl.DeleteRenderbuffers(1, &target->depth);
    if (target->color) glDeleteTextures(1, &target->color);
    memset(target, 0, sizeof(*target));
}
static maage_status maage_gl_target_size(maage_renderer *renderer, maage_gl_target *target, int width, int height, int depth, int linear) {
    maage_gl_renderer *native = maage_gl_gpu(renderer);
    maage_gl_target replacement;
    maage_status result;
    GLenum complete;
    GLint depth_bits = 0, stencil_bits = 0;
    if (target->framebuffer && target->width == width && target->height == height) return MAAGE_STATUS_VALUE(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_RESIZE, 0);
    if (width <= 0 || height <= 0 || width > native->target_limit || height > native->target_limit || width > native->viewport_limit[0] || height > native->viewport_limit[1]) {
        maage_log_warning("maage.render.opengl", "Framebuffer %dx%d exceeds texture/renderbuffer/viewport limits (%d, %dx%d).", width, height, native->target_limit, native->viewport_limit[0], native->viewport_limit[1]);
        return MAAGE_STATUS_VALUE(MAAGE_STATUS_SYSTEM_NOT_SUPPORTED, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_RESIZE, 0);
    }
    memset(&replacement, 0, sizeof(replacement));
    replacement.width = width;
    replacement.height = height;
    glGenTextures(1, &replacement.color);
    if (!replacement.color) {
        result = maage_gl_resource_failure(renderer, MAAGE_OPERATION_RESIZE);
        goto failed;
    }
    glBindTexture(GL_TEXTURE_2D, replacement.color);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, linear ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, linear ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, MAAGE_GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, MAAGE_GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, MAAGE_GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    native->gl.GenFramebuffers(1, &replacement.framebuffer);
    if (!replacement.framebuffer) {
        result = maage_gl_resource_failure(renderer, MAAGE_OPERATION_RESIZE);
        goto failed;
    }
    native->gl.BindFramebuffer(MAAGE_GL_FRAMEBUFFER, replacement.framebuffer);
    native->gl.FramebufferTexture2D(MAAGE_GL_FRAMEBUFFER, MAAGE_GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, replacement.color, 0);
    glDrawBuffer(MAAGE_GL_COLOR_ATTACHMENT0);
    if (depth) {
        native->gl.GenRenderbuffers(1, &replacement.depth);
        if (!replacement.depth) {
            result = maage_gl_resource_failure(renderer, MAAGE_OPERATION_RESIZE);
            goto failed;
        }
        native->gl.BindRenderbuffer(MAAGE_GL_RENDERBUFFER, replacement.depth);
        native->gl.RenderbufferStorage(MAAGE_GL_RENDERBUFFER, MAAGE_GL_DEPTH24_STENCIL8, width, height);
        native->gl.GetRenderbufferParameteriv(MAAGE_GL_RENDERBUFFER, MAAGE_GL_RENDERBUFFER_DEPTH_SIZE, &depth_bits);
        native->gl.GetRenderbufferParameteriv(MAAGE_GL_RENDERBUFFER, MAAGE_GL_RENDERBUFFER_STENCIL_SIZE, &stencil_bits);
        native->gl.FramebufferRenderbuffer(MAAGE_GL_FRAMEBUFFER, MAAGE_GL_DEPTH_STENCIL_ATTACHMENT, MAAGE_GL_RENDERBUFFER, replacement.depth);
    }
    complete = native->gl.CheckFramebufferStatus(MAAGE_GL_FRAMEBUFFER);
    result = maage_opengl_check(renderer, MAAGE_OPERATION_RESIZE);
    if (!maage_status_failed(result) && (!replacement.framebuffer || !replacement.color || (depth && (!replacement.depth || depth_bits < 24 || stencil_bits < 8)) || complete != MAAGE_GL_FRAMEBUFFER_COMPLETE)) {
        maage_log_warning("maage.render.opengl", "Framebuffer initialization failed: status 0x%04lX, depth %d, stencil %d.", (unsigned long) complete, depth_bits, stencil_bits);
        result = maage_status_trace_record(renderer->operation_trace, MAAGE_STATUS_VALUE(MAAGE_STATUS_SYSTEM_NOT_SUPPORTED, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_RESIZE, 0), MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_RESIZE, __FILE__, __LINE__, (long) complete);
    }
    if (!maage_status_failed(result)) {
        maage_gl_target_destroy(native, target);
        *target = replacement;
        native->gl.BindFramebuffer(MAAGE_GL_FRAMEBUFFER, 0);
        return result;
    }
failed:
    maage_gl_target_destroy(native, &replacement);
    native->gl.BindFramebuffer(MAAGE_GL_FRAMEBUFFER, 0);
    return result;
}
static maage_status maage_gl_buffer_map(maage_renderer *renderer, GLenum target, maage_gl_buffer *buffer, size_t bytes, void **mapped) {
    maage_gl_renderer *native = maage_gl_gpu(renderer);
    size_t capacity;
    *mapped = NULL;
    if (!bytes || (ptrdiff_t) bytes <= 0 || (size_t) (ptrdiff_t) bytes != bytes) return MAAGE_STATUS_VALUE(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_UPLOAD, 0);
    if (!buffer->name) native->gl.GenBuffers(1, &buffer->name);
    if (!buffer->name) return maage_gl_resource_failure(renderer, MAAGE_OPERATION_UPLOAD);
    native->gl.BindBuffer(target, buffer->name);
    if (buffer->capacity < bytes) {
        capacity = maage_render_transient_capacity(buffer->capacity, bytes);
        if ((ptrdiff_t) capacity <= 0 || (size_t) (ptrdiff_t) capacity != capacity) capacity = bytes;
        native->gl.BufferData(target, (ptrdiff_t) capacity, NULL, MAAGE_GL_STREAM_DRAW);
        MAAGE_GL_TRY(maage_opengl_check(renderer, MAAGE_OPERATION_UPLOAD));
        buffer->capacity = capacity;
    }
    /* These mutable buffers belong exclusively to a retired frame slot. Reuse
     * storage directly instead of asking the driver to rename it every frame. */
    *mapped = native->gl.MapBufferRange(target, 0, (ptrdiff_t) bytes, MAAGE_GL_MAP_WRITE_BIT | MAAGE_GL_MAP_UNSYNCHRONIZED_BIT);
    return *mapped ? maage_opengl_check(renderer, MAAGE_OPERATION_UPLOAD) : maage_gl_resource_failure(renderer, MAAGE_OPERATION_UPLOAD);
}
static maage_status maage_gl_unmap(maage_renderer *renderer, GLenum target) {
    maage_gl_renderer *native = maage_gl_gpu(renderer);
    if (!native->gl.UnmapBuffer(target)) {
        maage_status result = maage_opengl_check(renderer, MAAGE_OPERATION_UPLOAD);
        return maage_status_failed(result) ? result : MAAGE_STATUS_VALUE(MAAGE_STATUS_DEPENDENCY_FAILURE, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_UPLOAD, 0);
    }
    return maage_opengl_check(renderer, MAAGE_OPERATION_UPLOAD);
}
#define MAAGE_VERTEX_VEC1 1
#define MAAGE_VERTEX_VEC2 2
#define MAAGE_VERTEX_VEC3 3
#define MAAGE_VERTEX_VEC4 4
static void maage_gl_mesh_attributes(maage_gl_renderer *native) {
#define MAAGE_GL_ATTRIBUTE(slot, count, field) native->gl.EnableVertexAttribArray(slot); native->gl.VertexAttribPointer(slot, count, GL_FLOAT, GL_FALSE, sizeof(maage_mesh_vertex), (const void *) offsetof(maage_mesh_vertex, field));
    MAAGE_RENDER_MESH_ATTRIBUTES(MAAGE_GL_ATTRIBUTE)
#undef MAAGE_GL_ATTRIBUTE
}
static void maage_gl_ui_attributes(maage_gl_renderer *native) {
#define MAAGE_GL_ATTRIBUTE(slot, count, field) native->gl.EnableVertexAttribArray(slot); native->gl.VertexAttribPointer(slot, count, GL_FLOAT, GL_FALSE, sizeof(maage_render_ui_gpu_vertex), (const void *) (offsetof(maage_render_ui_gpu_vertex, vertex) + offsetof(maage_render_ui_vertex, field)));
    MAAGE_RENDER_UI_ATTRIBUTES(MAAGE_GL_ATTRIBUTE)
#undef MAAGE_GL_ATTRIBUTE
    native->gl.EnableVertexAttribArray(MAAGE_RENDER_UI_DEPTH_LOCATION);
    native->gl.VertexAttribPointer(MAAGE_RENDER_UI_DEPTH_LOCATION, 1, GL_FLOAT, GL_FALSE, sizeof(maage_render_ui_gpu_vertex), (const void *) offsetof(maage_render_ui_gpu_vertex, depth));
}
#undef MAAGE_VERTEX_VEC1
#undef MAAGE_VERTEX_VEC2
#undef MAAGE_VERTEX_VEC3
#undef MAAGE_VERTEX_VEC4
static maage_status maage_gl_ui_prepare(maage_renderer *renderer, maage_gl_stream *stream, const maage_render_ui_data *batch) {
    maage_gl_renderer *native = maage_gl_gpu(renderer);
    maage_render_ui_gpu_vertex *vertices;
    unsigned int *indices;
    void *mapped;
    maage_status result, released;
    if (!batch || !batch->index_count) return MAAGE_STATUS_VALUE(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_UPLOAD, 0);
    if (!stream->vao) native->gl.GenVertexArrays(1, &stream->vao);
    if (!stream->vao) return maage_gl_resource_failure(renderer, MAAGE_OPERATION_UPLOAD);
    native->gl.BindVertexArray(stream->vao);
    MAAGE_GL_TRY(maage_gl_buffer_map(renderer, MAAGE_GL_ARRAY_BUFFER, &stream->vertices, batch->vertex_count * sizeof(*vertices), &mapped));
    vertices = mapped;
    result = maage_gl_buffer_map(renderer, MAAGE_GL_ELEMENT_ARRAY_BUFFER, &stream->indices, batch->index_count * sizeof(*indices), &mapped);
    if (maage_status_failed(result)) {
        maage_gl_unmap(renderer, MAAGE_GL_ARRAY_BUFFER);
        return result;
    }
    indices = mapped;
    result = maage_render_ui_pack(batch, vertices, indices);
    released = maage_gl_unmap(renderer, MAAGE_GL_ARRAY_BUFFER);
    if (!maage_status_failed(result)) result = released;
    released = maage_gl_unmap(renderer, MAAGE_GL_ELEMENT_ARRAY_BUFFER);
    if (!maage_status_failed(result)) result = released;
    if (maage_status_failed(result)) return result;
    maage_gl_ui_attributes(native);
    return maage_opengl_check(renderer, MAAGE_OPERATION_UPLOAD);
}
static maage_status maage_gl_sort(maage_renderer *renderer, const maage_render_mesh *mesh, const float matrix[16], unsigned int slot) {
    maage_gl_renderer *native = maage_gl_gpu(renderer);
    maage_gl_mesh *geometry = mesh->platform;
    void *mapped;
    maage_status result = maage_render_geometry_sort_slot(mesh, matrix, geometry->sorted_revision, MAAGE_GL_FRAME_SLOTS * 2, slot);
    if (maage_status_code(result) != MAAGE_STATUS_SUCCESS) return result;
    native->gl.BindVertexArray(geometry->vao);
    MAAGE_GL_TRY(maage_gl_buffer_map(renderer, MAAGE_GL_ELEMENT_ARRAY_BUFFER, &geometry->sorted[slot], mesh->geometry.translucent_count * sizeof(unsigned int), &mapped));
    result = maage_render_geometry_write_sorted(&mesh->geometry, mapped);
    MAAGE_GL_TRY(maage_gl_unmap(renderer, MAAGE_GL_ELEMENT_ARRAY_BUFFER));
    if (maage_status_failed(result)) return result;
    geometry->sorted_revision[slot] = mesh->geometry.revision;
    return MAAGE_STATUS_VALUE(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_UPLOAD, 0);
}

static GLenum maage_gl_factor(int factor) {
    switch (factor) {
    case MAAGE_RENDER_FACTOR_SOURCE_ALPHA:
        return GL_SRC_ALPHA;
    case MAAGE_RENDER_FACTOR_INVERSE_ALPHA:
        return GL_ONE_MINUS_SRC_ALPHA;
    case MAAGE_RENDER_FACTOR_SOURCE_COLOR:
        return GL_SRC_COLOR;
    case MAAGE_RENDER_FACTOR_DESTINATION_COLOR:
        return GL_DST_COLOR;
    case MAAGE_RENDER_FACTOR_INVERSE_SOURCE_COLOR:
        return GL_ONE_MINUS_SRC_COLOR;
    case MAAGE_RENDER_FACTOR_INVERSE_DESTINATION_COLOR:
        return GL_ONE_MINUS_DST_COLOR;
    default:
        return GL_ONE;
    }
}
static void maage_gl_pipeline(maage_gl_renderer *native, unsigned int pipeline) {
    const maage_render_pipeline_state *state = &maage_render_pipeline_states[pipeline];
    if (native->pipeline == (int) pipeline) return;
    if (state->depth_test) glEnable(GL_DEPTH_TEST);
    else glDisable(GL_DEPTH_TEST);
    glDepthMask(state->depth_write ? GL_TRUE : GL_FALSE);
    if (state->cull) glEnable(GL_CULL_FACE);
    else glDisable(GL_CULL_FACE);
    if (state->blend) {
        glEnable(GL_BLEND);
        native->gl.BlendFuncSeparate(maage_gl_factor(state->source_rgb), maage_gl_factor(state->destination_rgb), GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    } else glDisable(GL_BLEND);
    native->pipeline = (int) pipeline;
}
static void maage_gl_uniforms(maage_gl_renderer *native, const maage_gl_program *program, const maage_render_uniforms *uniforms) {
    if (native->program != program->name) {
        native->gl.UseProgram(program->name);
        native->program = program->name;
    }
    if (program->matrix >= 0) native->gl.UniformMatrix4fv(program->matrix, 1, GL_FALSE, uniforms->matrix);
    if (program->lighting >= 0) native->gl.Uniform4fv(program->lighting, 1, uniforms->lighting);
    if (program->parameters >= 0) native->gl.Uniform4fv(program->parameters, 1, uniforms->parameters);
}
static void maage_gl_texture_bind(maage_gl_renderer *native, GLuint texture) {
    if (native->texture == texture) return;
    glBindTexture(GL_TEXTURE_2D, texture);
    native->texture = texture;
}
static void maage_gl_vao_bind(maage_gl_renderer *native, GLuint vao) {
    if (native->vao == vao) return;
    native->gl.BindVertexArray(vao);
    native->vao = vao;
}
static void maage_gl_indices(size_t first, size_t count) {
    while (count) {
        size_t amount = count > (size_t) INT_MAX ? (size_t) INT_MAX : count;
        amount -= amount % 3;
        glDrawElements(GL_TRIANGLES, (GLsizei) amount, GL_UNSIGNED_INT, (const void *) (first * sizeof(unsigned int)));
        first += amount;
        count -= amount;
    }
}
static void maage_gl_mesh_passes(maage_gl_renderer *native, const maage_render_frame_plan *plan, int foreground, unsigned int slot) {
    maage_render_mesh_cursor cursor = {0, 0, NULL, 0};
    maage_render_uniforms uniforms;
    const maage_render_mesh *mesh;
    size_t first, count;
    while ((mesh = maage_render_frame_next_mesh(plan, foreground, &cursor)) != NULL) {
        maage_gl_mesh *geometry = mesh->platform;
        maage_gl_texture *texture = mesh->texture->platform;
        maage_gl_pipeline(native, cursor.layer);
        maage_render_draw_uniforms(plan, foreground, &cursor, &uniforms);
        maage_gl_uniforms(native, &native->mesh_program, &uniforms);
        maage_gl_texture_bind(native, texture->name);
        maage_gl_vao_bind(native, geometry->vao);
        count = maage_render_geometry_range(&mesh->geometry, cursor.layer, &first);
        if (cursor.instance && cursor.layer == MAAGE_RENDER_TRANSLUCENT) {
            native->gl.BindBuffer(MAAGE_GL_ELEMENT_ARRAY_BUFFER, geometry->indices);
            maage_gl_indices(cursor.triangle, 3);
        } else {
            native->gl.BindBuffer(MAAGE_GL_ELEMENT_ARRAY_BUFFER, cursor.layer == MAAGE_RENDER_TRANSLUCENT ? geometry->sorted[slot + (foreground ? MAAGE_GL_FRAME_SLOTS : 0)].name : geometry->indices);
            maage_gl_indices(first, count);
        }
    }
}
static void maage_gl_ui_draw(maage_gl_renderer *native, const maage_gl_stream *stream, const maage_render_ui_data *batch, size_t first, size_t count, int panorama) {
    maage_render_uniforms uniforms;
    maage_render_ui_segment segments[3];
    unsigned int index, segment_count;
    maage_gl_texture *texture;
    if (!batch || !count) return;
    texture = batch->texture->platform;
    maage_render_ui_uniforms(batch, &uniforms);
    maage_gl_uniforms(native, &native->ui_program, &uniforms);
    maage_gl_texture_bind(native, texture->name);
    maage_gl_vao_bind(native, stream->vao);
    segment_count = maage_render_ui_segments(batch, first, count, segments);
    for (index = 0; index < segment_count; ++index) {
        maage_gl_pipeline(native, MAAGE_RENDER_UI + (panorama ? MAAGE_RENDER_BLEND_ALPHA : segments[index].blend));
        maage_gl_indices(segments[index].first, segments[index].count);
    }
}
static void maage_gl_fullscreen(maage_gl_renderer *native, GLuint texture, float x, float y, int blur) {
    maage_render_uniforms uniforms;
    memset(&uniforms, 0, sizeof(uniforms));
    uniforms.parameters[0] = x;
    uniforms.parameters[1] = y;
    uniforms.parameters[2] = (float) MAAGE_RENDER_BLUR_SIGMA;
    uniforms.parameters[3] = blur ? MAAGE_RENDER_BLUR_RADIUS : 0;
    maage_gl_pipeline(native, MAAGE_RENDER_FULLSCREEN);
    maage_gl_uniforms(native, &native->fullscreen_program, &uniforms);
    maage_gl_texture_bind(native, texture);
    maage_gl_vao_bind(native, native->empty_vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
}
static void maage_gl_pass(maage_gl_renderer *native, GLuint framebuffer, const float clear[4], int depth) {
    native->gl.BindFramebuffer(MAAGE_GL_FRAMEBUFFER, framebuffer);
    glClearColor(clear[0], clear[1], clear[2], clear[3]);
    if (depth) {
        glDepthMask(GL_TRUE);
        native->pipeline = -1;
    }
    glClear(GL_COLOR_BUFFER_BIT | (depth ? GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT : 0));
}
static maage_status maage_gl_frame_render(maage_renderer *renderer, const maage_window_state *state, const maage_render_frame_plan *plan) {
    maage_gl_renderer *native = maage_gl_gpu(renderer);
    maage_gl_frame *frame = &native->frames[native->slot];
    maage_status result;
    GLint signaled = 0;
    size_t index;
    unsigned int layer;
    MAAGE_GL_TRY(maage_opengl_context_bind(renderer));
    if (frame->fence) {
        /* Poll only. A busy slot retains its batches/targets and returns bounded
         * backpressure to Client pacing instead of waiting in the render loop. */
        native->gl.GetSynciv(frame->fence, MAAGE_GL_SYNC_STATUS, 1, NULL, &signaled);
        MAAGE_GL_TRY(maage_opengl_check(renderer, MAAGE_OPERATION_POLL));
        if (signaled != MAAGE_GL_SIGNALED) return MAAGE_STATUS_VALUE(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_PRESENT, 0);
        native->gl.DeleteSync(frame->fence);
        frame->fence = NULL;
    }
    MAAGE_GL_TRY(maage_gl_target_size(renderer, &frame->scene, state->framebuffer_width, state->framebuffer_height, 1, 0));
    if (plan->panorama_count) {
        for (layer = 0; layer < 2; ++layer)
            MAAGE_GL_TRY(maage_gl_target_size(renderer, &frame->panorama[layer], state->framebuffer_width, state->framebuffer_height, 0, 1));
    }
    for (layer = 0; layer < 3; ++layer)
        MAAGE_GL_TRY(maage_gl_ui_prepare(renderer, &frame->ui[layer], plan->layers[layer]));
    for (index = 0; index < plan->mesh_count; ++index)
        MAAGE_GL_TRY(maage_gl_sort(renderer, plan->meshes[index], plan->world.matrix, native->slot));
    if (plan->foreground) MAAGE_GL_TRY(maage_gl_sort(renderer, plan->foreground, plan->hand.matrix, native->slot + MAAGE_GL_FRAME_SLOTS));

    /* Uploads/target creation bind native objects outside the draw-state cache. */
    native->pipeline = -1;
    native->program = native->texture = native->vao = 0;
    native->gl.UseProgram(0);
    native->gl.BindVertexArray(0);
    native->gl.ActiveTexture(MAAGE_GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glViewport(0, 0, state->framebuffer_width, state->framebuffer_height);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(MAAGE_GL_FRAMEBUFFER_SRGB);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthFunc(GL_LEQUAL);
    glClearDepth(1.0);
    glClearStencil(0);
    glFrontFace(GL_CCW);
    glCullFace(GL_BACK);
    native->gl.BlendEquation(MAAGE_GL_FUNC_ADD);
    maage_gl_pass(native, frame->scene.framebuffer, renderer->clear, 1);
    maage_gl_ui_draw(native, &frame->ui[0], plan->layers[0], 0, plan->layers[0] ? plan->layers[0]->index_count : 0, 0);
    maage_gl_mesh_passes(native, plan, 0, native->slot);
    if (plan->panorama_count) {
        maage_gl_pass(native, frame->panorama[0].framebuffer, renderer->clear, 0);
        maage_gl_ui_draw(native, &frame->ui[2], plan->layers[2], 0, plan->panorama_count, 1);
        maage_gl_pass(native, frame->panorama[1].framebuffer, renderer->clear, 0);
        maage_gl_fullscreen(native, frame->panorama[0].color, 1.0f / state->framebuffer_width, 0, 1);
        maage_gl_pass(native, frame->panorama[0].framebuffer, renderer->clear, 0);
        maage_gl_fullscreen(native, frame->panorama[1].color, 0, 1.0f / state->framebuffer_height, 1);
        native->gl.BindFramebuffer(MAAGE_GL_FRAMEBUFFER, frame->scene.framebuffer);
        maage_gl_fullscreen(native, frame->panorama[0].color, 0, 0, 0);
    }
    /* Match the existing scene contract: environmental overlay, independent
     * foreground depth, then crisp HUD/menu geometry. */
    maage_gl_ui_draw(native, &frame->ui[1], plan->layers[1], 0, plan->layers[1] ? plan->layers[1]->index_count : 0, 0);
    if (plan->foreground) {
        glDepthMask(GL_TRUE);
        glClear(GL_DEPTH_BUFFER_BIT);
        native->pipeline = -1;
        maage_gl_mesh_passes(native, plan, 1, native->slot);
    }
    maage_gl_ui_draw(native, &frame->ui[2], plan->layers[2], plan->panorama_count, plan->layers[2] ? plan->layers[2]->index_count - plan->panorama_count : 0, 0);
    native->gl.BindFramebuffer(MAAGE_GL_FRAMEBUFFER, 0);
    glDrawBuffer(GL_BACK);
    maage_gl_fullscreen(native, frame->scene.color, 0, 0, 0);
    frame->fence = native->gl.FenceSync(MAAGE_GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    if (!frame->fence) return maage_gl_resource_failure(renderer, MAAGE_OPERATION_SUBMIT);
    glFlush();
    MAAGE_GL_TRY(maage_opengl_check(renderer, MAAGE_OPERATION_PRESENT));
    result = maage_opengl_context_swap(renderer);
    if (maage_status_failed(result)) return result;
    /* Only the hidden startup prime needs completion before window reveal. */
    if (plan->preparing) {
        glFinish();
        MAAGE_GL_TRY(maage_opengl_check(renderer, MAAGE_OPERATION_WAIT));
    }
    native->slot = (native->slot + 1) % MAAGE_GL_FRAME_SLOTS;
    renderer->refresh = 0;
    return MAAGE_STATUS_VALUE(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_PRESENT, 0);
}
static maage_status maage_gl_create_texture(maage_renderer *renderer, maage_render_texture *resource, const maage_pixel_buffer *atlas) {
    maage_gl_texture *texture;
    int row;
    size_t row_pixels = atlas->stride / 4;
    MAAGE_GL_TRY(maage_opengl_context_bind(renderer));
    if ((unsigned int) atlas->width > renderer->capabilities.max_texture_dimension_2d || (unsigned int) atlas->height > renderer->capabilities.max_texture_dimension_2d) return MAAGE_STATUS_VALUE(MAAGE_STATUS_SYSTEM_NOT_SUPPORTED, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_CREATE, 0);
    texture = (maage_gl_texture *) calloc(1, sizeof(*texture));
    if (!texture) return MAAGE_STATUS_VALUE(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_CREATE, 0);
    resource->platform = texture;
    glGenTextures(1, &texture->name);
    if (!texture->name) return maage_gl_resource_failure(renderer, MAAGE_OPERATION_CREATE);
    glBindTexture(GL_TEXTURE_2D, texture->name);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, MAAGE_GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, MAAGE_GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    if (!(atlas->stride % 4) && row_pixels <= (size_t) INT_MAX) {
        glPixelStorei(GL_UNPACK_ROW_LENGTH, (GLint) row_pixels);
        glTexImage2D(GL_TEXTURE_2D, 0, MAAGE_GL_RGBA8, atlas->width, atlas->height, 0, GL_RGBA, GL_UNSIGNED_BYTE, atlas->pixels);
    } else {
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        glTexImage2D(GL_TEXTURE_2D, 0, MAAGE_GL_RGBA8, atlas->width, atlas->height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        for (row = 0; row < atlas->height; ++row)
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, row, atlas->width, 1, GL_RGBA, GL_UNSIGNED_BYTE, atlas->pixels + (size_t) row * atlas->stride);
    }
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    return maage_opengl_check(renderer, MAAGE_OPERATION_CREATE);
}
static void maage_gl_release_result(maage_renderer *renderer, maage_status result) {
    if (!maage_status_failed(result) || maage_status_failed(renderer->failure)) return;
    renderer->failure = result;
    maage_log_trace_status(MAAGE_LOG_ERROR, "maage.render.opengl", result, renderer->operation_trace, "Native resource release failed; the renderer requires destruction.");
}
static void maage_gl_destroy_texture(maage_render_texture *resource) {
    maage_gl_texture *texture = resource->platform;
    maage_status_trace trace;
    maage_status_trace *caller_trace = resource->renderer->operation_trace;
    maage_status result;
    if (!texture) return;
    if (!caller_trace) {
        maage_status_trace_init(&trace);
        resource->renderer->operation_trace = &trace;
    }
    result = maage_opengl_context_bind(resource->renderer);
    if (!maage_status_failed(result)) {
        if (texture->name) glDeleteTextures(1, &texture->name);
        result = maage_opengl_check(resource->renderer, MAAGE_OPERATION_DESTROY);
    }
    maage_gl_release_result(resource->renderer, result);
    resource->renderer->operation_trace = caller_trace;
    /* OpenGL retains objects referenced by queued commands. Context destruction
     * also releases native objects if a lost binding prevents explicit deletion. */
    free(texture);
    resource->platform = NULL;
}
static maage_status maage_gl_upload_geometry(maage_renderer *renderer, maage_render_mesh *mesh, const maage_mesh_data *data) {
    maage_gl_renderer *native = maage_gl_gpu(renderer);
    maage_gl_mesh *geometry;
    maage_status result, released;
    size_t vertex_bytes = data->vertex_count * sizeof(*data->vertices), index_bytes = data->index_count * sizeof(*data->indices);
    void *mapped;
    MAAGE_GL_TRY(maage_opengl_context_bind(renderer));
    if ((ptrdiff_t) vertex_bytes <= 0 || (size_t) (ptrdiff_t) vertex_bytes != vertex_bytes || (ptrdiff_t) index_bytes <= 0 || (size_t) (ptrdiff_t) index_bytes != index_bytes) return MAAGE_STATUS_VALUE(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_UPLOAD, 0);
    geometry = (maage_gl_mesh *) calloc(1, sizeof(*geometry));
    if (!geometry) return MAAGE_STATUS_VALUE(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_UPLOAD, 0);
    mesh->platform = geometry;
    native->gl.GenVertexArrays(1, &geometry->vao);
    native->gl.GenBuffers(1, &geometry->vertices);
    native->gl.GenBuffers(1, &geometry->indices);
    if (!geometry->vao || !geometry->vertices || !geometry->indices) return maage_gl_resource_failure(renderer, MAAGE_OPERATION_UPLOAD);
    native->gl.BindVertexArray(geometry->vao);
    native->gl.BindBuffer(MAAGE_GL_ARRAY_BUFFER, geometry->vertices);
    native->gl.BufferData(MAAGE_GL_ARRAY_BUFFER, (ptrdiff_t) vertex_bytes, data->vertices, MAAGE_GL_STATIC_DRAW);
    maage_gl_mesh_attributes(native);
    native->gl.BindBuffer(MAAGE_GL_ELEMENT_ARRAY_BUFFER, geometry->indices);
    native->gl.BufferData(MAAGE_GL_ELEMENT_ARRAY_BUFFER, (ptrdiff_t) index_bytes, NULL, MAAGE_GL_STATIC_DRAW);
    MAAGE_GL_TRY(maage_opengl_check(renderer, MAAGE_OPERATION_UPLOAD));
    mapped = native->gl.MapBufferRange(MAAGE_GL_ELEMENT_ARRAY_BUFFER, 0, (ptrdiff_t) index_bytes, MAAGE_GL_MAP_WRITE_BIT | MAAGE_GL_MAP_INVALIDATE_BUFFER_BIT);
    if (!mapped) return maage_gl_resource_failure(renderer, MAAGE_OPERATION_UPLOAD);
    result = maage_render_geometry_prepare(&mesh->geometry, data, mapped);
    released = maage_gl_unmap(renderer, MAAGE_GL_ELEMENT_ARRAY_BUFFER);
    return maage_status_failed(result) ? result : released;
}
static void maage_gl_destroy_mesh(maage_render_mesh *mesh) {
    maage_gl_mesh *geometry = mesh->platform;
    maage_gl_renderer *native = maage_gl_gpu(mesh->renderer);
    unsigned int index;
    maage_status_trace trace;
    maage_status_trace *caller_trace = mesh->renderer->operation_trace;
    maage_status result;
    if (!geometry) return;
    if (!caller_trace) {
        maage_status_trace_init(&trace);
        mesh->renderer->operation_trace = &trace;
    }
    result = maage_opengl_context_bind(mesh->renderer);
    if (!maage_status_failed(result)) {
        native->gl.BindVertexArray(0);
        if (geometry->vao) native->gl.DeleteVertexArrays(1, &geometry->vao);
        if (geometry->vertices) native->gl.DeleteBuffers(1, &geometry->vertices);
        if (geometry->indices) native->gl.DeleteBuffers(1, &geometry->indices);
        for (index = 0; index < MAAGE_GL_FRAME_SLOTS * 2; ++index)
            if (geometry->sorted[index].name) native->gl.DeleteBuffers(1, &geometry->sorted[index].name);
        result = maage_opengl_check(mesh->renderer, MAAGE_OPERATION_DESTROY);
    }
    maage_gl_release_result(mesh->renderer, result);
    mesh->renderer->operation_trace = caller_trace;
    free(geometry);
    mesh->platform = NULL;
}

static maage_status maage_gl_limit(maage_renderer *renderer, GLenum parameter, const char *name, GLint *value) {
    maage_status result;
    glGetIntegerv(parameter, value);
    result = maage_opengl_check(renderer, MAAGE_OPERATION_INITIALIZE);
    if (maage_status_failed(result)) maage_log_status(MAAGE_LOG_WARNING, "maage.render.opengl", result, "Required capability query %s (0x%04lX) failed.", name, (unsigned long) parameter);
    return result;
}
static maage_status maage_gl_limits(maage_renderer *renderer) {
    maage_gl_renderer *native = maage_gl_gpu(renderer);
    const char *language;
    GLint profile = 0, attributes = 0, units = 0, vertex_uniforms = 0, fragment_uniforms = 0, vertex_outputs = 0, fragment_inputs = 0;
    GLboolean double_buffer = GL_FALSE;
    int major = 0, minor = 0;
    language = (const char *) glGetString(MAAGE_GL_SHADING_LANGUAGE_VERSION);
    MAAGE_GL_TRY(maage_opengl_check(renderer, MAAGE_OPERATION_INITIALIZE));
    MAAGE_GL_TRY(maage_gl_limit(renderer, MAAGE_GL_CONTEXT_PROFILE_MASK, "GL_CONTEXT_PROFILE_MASK", &profile));
    MAAGE_GL_TRY(maage_gl_limit(renderer, MAAGE_GL_MAX_VERTEX_ATTRIBS, "GL_MAX_VERTEX_ATTRIBS", &attributes));
    MAAGE_GL_TRY(maage_gl_limit(renderer, MAAGE_GL_MAX_TEXTURE_IMAGE_UNITS, "GL_MAX_TEXTURE_IMAGE_UNITS", &units));
    MAAGE_GL_TRY(maage_gl_limit(renderer, MAAGE_GL_MAX_VERTEX_UNIFORM_COMPONENTS, "GL_MAX_VERTEX_UNIFORM_COMPONENTS", &vertex_uniforms));
    MAAGE_GL_TRY(maage_gl_limit(renderer, MAAGE_GL_MAX_FRAGMENT_UNIFORM_COMPONENTS, "GL_MAX_FRAGMENT_UNIFORM_COMPONENTS", &fragment_uniforms));
    /* Forward-compatible core contexts remove the deprecated aggregate varying
     * query. The mesh writes nine user components plus four for gl_Position. */
    MAAGE_GL_TRY(maage_gl_limit(renderer, MAAGE_GL_MAX_VERTEX_OUTPUT_COMPONENTS, "GL_MAX_VERTEX_OUTPUT_COMPONENTS", &vertex_outputs));
    MAAGE_GL_TRY(maage_gl_limit(renderer, MAAGE_GL_MAX_FRAGMENT_INPUT_COMPONENTS, "GL_MAX_FRAGMENT_INPUT_COMPONENTS", &fragment_inputs));
    MAAGE_GL_TRY(maage_gl_limit(renderer, MAAGE_GL_MAX_RENDERBUFFER_SIZE, "GL_MAX_RENDERBUFFER_SIZE", &native->target_limit));
    MAAGE_GL_TRY(maage_gl_limit(renderer, GL_MAX_VIEWPORT_DIMS, "GL_MAX_VIEWPORT_DIMS", native->viewport_limit));
    glGetBooleanv(GL_DOUBLEBUFFER, &double_buffer);
    MAAGE_GL_TRY(maage_opengl_check(renderer, MAAGE_OPERATION_INITIALIZE));
    maage_log_info("maage.render.opengl", "GLSL %.120s; profile 0x%lX; attributes %d; fragment textures %d; vertex/fragment uniform components %d/%d; vertex-output/fragment-input components %d/%d; max texture %u; renderbuffer %d; viewport %dx%d.", language ? language : "unavailable", (unsigned long) profile, attributes, units, vertex_uniforms, fragment_uniforms, vertex_outputs, fragment_inputs, renderer->capabilities.max_texture_dimension_2d, native->target_limit, native->viewport_limit[0], native->viewport_limit[1]);
    if (!language || sscanf(language, "%d.%d", &major, &minor) != 2 || major < 3 || (major == 3 && minor < 30) || !(profile & MAAGE_GL_CONTEXT_CORE_PROFILE_BIT) || !double_buffer || attributes < 7 || units < 1 || vertex_uniforms < 24 || fragment_uniforms < 24 || vertex_outputs < 13 || fragment_inputs < 9 || !renderer->capabilities.max_texture_dimension_2d || native->target_limit <= 0 || native->viewport_limit[0] <= 0 || native->viewport_limit[1] <= 0) {
        maage_log_warning("maage.render.opengl", "Required GLSL 3.30, core profile, double buffering, vertex layout, uniform/varying capacity, or framebuffer limits are unavailable.");
        return MAAGE_STATUS_VALUE(MAAGE_STATUS_SYSTEM_NOT_SUPPORTED, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_INITIALIZE, 0);
    }
    if ((unsigned int) native->target_limit > renderer->capabilities.max_texture_dimension_2d) native->target_limit = (GLint) renderer->capabilities.max_texture_dimension_2d;
    return MAAGE_STATUS_VALUE(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_INITIALIZE, 0);
}
static maage_status maage_opengl_modern_create(maage_renderer *renderer) {
    maage_gl_renderer *native;
    maage_gl_stream *stream;
    void *mapped;
    maage_gl_sync fence;
    maage_status result;
    maage_status_trace vsync_trace;
    maage_status_trace *caller_trace;
    GLint signaled = 0;
    GLenum complete;
    MAAGE_GL_TRY(maage_opengl_create(renderer, 1));
    if (!maage_render_gpu_layout_supported()) return MAAGE_STATUS_VALUE(MAAGE_STATUS_SYSTEM_NOT_SUPPORTED, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_INITIALIZE, 0);
    native = (maage_gl_renderer *) calloc(1, sizeof(*native));
    if (!native) return MAAGE_STATUS_VALUE(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_INITIALIZE, 0);
    ((maage_opengl_renderer *) renderer->platform)->gpu = native;
    MAAGE_GL_TRY(maage_gl_load(renderer));
    MAAGE_GL_TRY(maage_gl_limits(renderer));
    native->gl.ActiveTexture(MAAGE_GL_TEXTURE0);
    native->gl.BindFramebuffer(MAAGE_GL_FRAMEBUFFER, 0);
    complete = native->gl.CheckFramebufferStatus(MAAGE_GL_FRAMEBUFFER);
    if (complete != MAAGE_GL_FRAMEBUFFER_COMPLETE) return maage_status_trace_record(renderer->operation_trace, MAAGE_STATUS_VALUE(MAAGE_STATUS_SYSTEM_NOT_SUPPORTED, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_INITIALIZE, 0), MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_INITIALIZE, __FILE__, __LINE__, (long) complete);
    MAAGE_GL_TRY(maage_gl_program_create(renderer, &native->mesh_program, maage_opengl_mesh_vert, maage_opengl_mesh_frag, 1, "mesh"));
    MAAGE_GL_TRY(maage_gl_program_create(renderer, &native->ui_program, maage_opengl_ui_vert, maage_opengl_ui_frag, 0, "UI"));
    MAAGE_GL_TRY(maage_gl_program_create(renderer, &native->fullscreen_program, maage_opengl_fullscreen_vert, maage_opengl_blur_frag, 0, "Gaussian/composite"));
    native->gl.GenVertexArrays(1, &native->empty_vao);
    if (!native->empty_vao) return maage_gl_resource_failure(renderer, MAAGE_OPERATION_INITIALIZE);
    /* Seed the actual first GUI stream, validating VAO/attribute setup and
     * mapped vertex/index storage before publishing buffered draw support. */
    stream = &native->frames[0].ui[2];
    native->gl.GenVertexArrays(1, &stream->vao);
    if (!stream->vao) return maage_gl_resource_failure(renderer, MAAGE_OPERATION_INITIALIZE);
    native->gl.BindVertexArray(stream->vao);
    MAAGE_GL_TRY(maage_gl_buffer_map(renderer, MAAGE_GL_ARRAY_BUFFER, &stream->vertices, sizeof(maage_render_ui_gpu_vertex), &mapped));
    memset(mapped, 0, sizeof(maage_render_ui_gpu_vertex));
    MAAGE_GL_TRY(maage_gl_unmap(renderer, MAAGE_GL_ARRAY_BUFFER));
    MAAGE_GL_TRY(maage_gl_buffer_map(renderer, MAAGE_GL_ELEMENT_ARRAY_BUFFER, &stream->indices, sizeof(unsigned int), &mapped));
    memset(mapped, 0, sizeof(unsigned int));
    MAAGE_GL_TRY(maage_gl_unmap(renderer, MAAGE_GL_ELEMENT_ARRAY_BUFFER));
    maage_gl_ui_attributes(native);
    MAAGE_GL_TRY(maage_opengl_check(renderer, MAAGE_OPERATION_INITIALIZE));
    /* Validate actual RGBA8 and packed depth/stencil renderability before
     * advertising scene support, including on virtualized implementations. */
    MAAGE_GL_TRY(maage_gl_target_size(renderer, &native->frames[0].scene, 1, 1, 1, 0));
    MAAGE_GL_TRY(maage_gl_target_size(renderer, &native->frames[0].panorama[0], 1, 1, 0, 1));
    MAAGE_GL_TRY(maage_gl_target_size(renderer, &native->frames[0].panorama[1], 1, 1, 0, 1));
    fence = native->gl.FenceSync(MAAGE_GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    if (!fence) return maage_gl_resource_failure(renderer, MAAGE_OPERATION_INITIALIZE);
    native->gl.GetSynciv(fence, MAAGE_GL_SYNC_STATUS, 1, NULL, &signaled);
    native->gl.DeleteSync(fence);
    MAAGE_GL_TRY(maage_opengl_check(renderer, MAAGE_OPERATION_INITIALIZE));
    MAAGE_GL_TRY(maage_opengl_context_attach(renderer));
    complete = native->gl.CheckFramebufferStatus(MAAGE_GL_FRAMEBUFFER);
    if (complete != MAAGE_GL_FRAMEBUFFER_COMPLETE) return maage_status_trace_record(renderer->operation_trace, MAAGE_STATUS_VALUE(MAAGE_STATUS_SYSTEM_NOT_SUPPORTED, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_INITIALIZE, 0), MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_INITIALIZE, __FILE__, __LINE__, (long) complete);
    glDisable(GL_DITHER);
    glDepthRange(0.0, 1.0);
    renderer->capabilities.features |= MAAGE_RENDER_CAP_SHADERS | MAAGE_RENDER_CAP_VERTEX_BUFFERS | MAAGE_RENDER_CAP_INDEX_BUFFERS | MAAGE_RENDER_CAP_RENDER_TARGETS | MAAGE_RENDER_CAP_DEPTH | MAAGE_RENDER_CAP_STENCIL | MAAGE_RENDER_CAP_TEXTURED_MESH | MAAGE_RENDER_CAP_UI | MAAGE_RENDER_CAP_SCENE;
    renderer->capabilities.synchronization = MAAGE_RENDER_SYNC_FENCE;
    renderer->capabilities.max_frames_in_flight = MAAGE_GL_FRAME_SLOTS;
    maage_status_trace_init(&vsync_trace);
    caller_trace = renderer->operation_trace;
    renderer->operation_trace = &vsync_trace;
    result = maage_opengl_context_vsync(renderer, 1);
    renderer->operation_trace = caller_trace;
    if (maage_status_failed(result)) maage_log_trace_status(MAAGE_LOG_VERBOSE, "maage.render.opengl", result, &vsync_trace, "Native VSync control is unavailable; driver synchronization policy remains active.");
    maage_log_info("maage.render.opengl", "Scene, UI, RGBA8/depth24/stencil8 framebuffers, Gaussian composition and fences initialized; capabilities 0x%08lX, %u frame slots.", renderer->capabilities.features, renderer->capabilities.max_frames_in_flight);
    return maage_opengl_check(renderer, MAAGE_OPERATION_INITIALIZE);
}
static maage_status maage_opengl_modern_destroy(maage_renderer *renderer) {
    maage_opengl_renderer *owner = renderer->platform;
    maage_gl_renderer *native;
    maage_status result = MAAGE_STATUS_VALUE(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_DESTROY, 0);
    unsigned int slot, layer;
    if (!owner) return result;
    native = owner->gpu;
    if (native && native->loaded) {
        result = maage_opengl_context_bind(renderer);
        if (!maage_status_failed(result)) {
            native->gl.UseProgram(0);
            native->gl.BindVertexArray(0);
            native->gl.BindBuffer(MAAGE_GL_ARRAY_BUFFER, 0);
            glBindTexture(GL_TEXTURE_2D, 0);
            native->gl.BindFramebuffer(MAAGE_GL_FRAMEBUFFER, 0);
            for (slot = 0; slot < MAAGE_GL_FRAME_SLOTS; ++slot) {
                maage_gl_frame *frame = &native->frames[slot];
                if (frame->fence) native->gl.DeleteSync(frame->fence);
                for (layer = 0; layer < 3; ++layer) {
                    maage_gl_stream *stream = &frame->ui[layer];
                    if (stream->vao) native->gl.DeleteVertexArrays(1, &stream->vao);
                    if (stream->vertices.name) native->gl.DeleteBuffers(1, &stream->vertices.name);
                    if (stream->indices.name) native->gl.DeleteBuffers(1, &stream->indices.name);
                }
                maage_gl_target_destroy(native, &frame->scene);
                maage_gl_target_destroy(native, &frame->panorama[0]);
                maage_gl_target_destroy(native, &frame->panorama[1]);
            }
            if (native->empty_vao) native->gl.DeleteVertexArrays(1, &native->empty_vao);
            if (native->mesh_program.name) native->gl.DeleteProgram(native->mesh_program.name);
            if (native->ui_program.name) native->gl.DeleteProgram(native->ui_program.name);
            if (native->fullscreen_program.name) native->gl.DeleteProgram(native->fullscreen_program.name);
            result = maage_opengl_check(renderer, MAAGE_OPERATION_DESTROY);
        }
    }
    free(native);
    owner->gpu = NULL;
    /* GL owns submitted commands and retains their deleted resources until
     * retirement. No per-resource wait or GPU-wide drain is required here. */
    maage_opengl_destroy(renderer);
    return result;
}
const maage_render_backend_api maage_render_opengl_api = {
    MAAGE_RENDER_OPENGL, maage_opengl_modern_create, maage_opengl_modern_destroy,
    MAAGE_RENDER_CAPABILITIES_VERSION, NULL, maage_gl_frame_render, maage_gl_upload_geometry,
    maage_gl_destroy_mesh, maage_gl_create_texture, maage_gl_destroy_texture, maage_opengl_context_vsync};
