#ifndef MAAGE_RENDER_H
#define MAAGE_RENDER_H
#include "maage/window.h"
#include "maage/geometry.h"
#include "maage/status.h"

typedef struct maage_renderer maage_renderer;
typedef struct maage_render_mesh maage_render_mesh;
typedef struct maage_render_texture maage_render_texture;
typedef enum maage_render_backend {
    MAAGE_RENDER_AUTO = 0,
    MAAGE_RENDER_VULKAN,
    MAAGE_RENDER_METAL,
    MAAGE_RENDER_OPENGL,
    MAAGE_RENDER_OPENGL_LEGACY,
    MAAGE_RENDER_SOFTWARE
} maage_render_backend;
/* Requested API family is separate from the concrete active backend above. */
typedef enum maage_render_family {
    MAAGE_RENDER_FAMILY_AUTO = 0,
    MAAGE_RENDER_FAMILY_METAL,
    MAAGE_RENDER_FAMILY_VULKAN,
    MAAGE_RENDER_FAMILY_OPENGL,
    MAAGE_RENDER_FAMILY_SOFTWARE
} maage_render_family;
typedef struct maage_render_desc {
    maage_window *window;
    maage_render_family family;
    unsigned long required_features; /* AUTO skips backends missing these flags. */
} maage_render_desc;

#define MAAGE_RENDER_CAPABILITIES_VERSION 1
#define MAAGE_RENDER_CAP_CLEAR 0x0001UL
#define MAAGE_RENDER_CAP_PRESENT 0x0002UL
#define MAAGE_RENDER_CAP_SHADERS 0x0004UL
#define MAAGE_RENDER_CAP_FIXED_FUNCTION 0x0008UL
#define MAAGE_RENDER_CAP_VERTEX_BUFFERS 0x0010UL
#define MAAGE_RENDER_CAP_INDEX_BUFFERS 0x0020UL
#define MAAGE_RENDER_CAP_RENDER_TARGETS 0x0040UL
#define MAAGE_RENDER_CAP_DEPTH 0x0080UL
#define MAAGE_RENDER_CAP_STENCIL 0x0100UL
#define MAAGE_RENDER_CAP_INSTANCING 0x0200UL
#define MAAGE_RENDER_CAP_COMPUTE 0x0400UL
#define MAAGE_RENDER_CAP_CPU_FRAMEBUFFER 0x0800UL
/* Implemented upload/draw commands, unlike the device capability flags above. */
#define MAAGE_RENDER_CAP_TEXTURED_MESH 0x1000UL
#define MAAGE_RENDER_CAP_UI 0x2000UL
#define MAAGE_RENDER_CAP_SCENE 0x4000UL
#define MAAGE_RENDER_FORMAT_RGBA8 0x01UL
#define MAAGE_RENDER_FORMAT_BGRA8 0x02UL
#define MAAGE_RENDER_SYNC_CPU_WAIT 0x01UL
#define MAAGE_RENDER_SYNC_FENCE 0x02UL
#define MAAGE_RENDER_SYNC_BINARY_SEMAPHORE 0x04UL

/* Conservative device/backend capabilities, not a promise that resource/draw APIs already
 * exist. Zero limits mean unknown or inapplicable; never assume a modern GPU.
 * Texture formats describe sampled 2D textures; CPU_FRAMEBUFFER instead uses
 * the neutral RGBA8 pixel-buffer representation in maage/pixels.h. */
typedef struct maage_render_capabilities {
    unsigned int version;
    unsigned long features;
    unsigned long texture_formats;
    unsigned long synchronization;
    unsigned int max_texture_dimension_2d;
    unsigned int max_frames_in_flight;
} maage_render_capabilities;

/* Transient indexed UI triangles use top-left logical coordinates, normalized
 * atlas coordinates and straight-alpha RGBA color components in [0, 1]. */
typedef struct maage_render_ui_vertex {
    float position[2];
    float uv[2];
    float color[4];
} maage_render_ui_vertex;
enum {
    MAAGE_RENDER_BLEND_ALPHA = 0,
    MAAGE_RENDER_BLEND_ADDITIVE = 1,
    MAAGE_RENDER_BLEND_INVERT = 2
};
typedef struct maage_render_ui_data {
    const maage_render_ui_vertex *vertices;
    size_t vertex_count;
    const unsigned int *indices;
    size_t index_count;
    maage_render_texture *texture;
    float logical_width;
    float logical_height;
    /* The first N indices compose an opaque background. Supporting backends
     * Gaussian-blur their completed render target before drawing the remaining
     * indices at native resolution. Others simply render the batch unchanged. */
    size_t gaussian_background_indices;
    /* Optional triangle-aligned range uses destination-color inversion. */
    size_t invert_first_index, invert_index_count;
    unsigned int blend; /* Straight alpha, source-alpha emission, or color inversion. */
    /* Optional positive camera depths, one per vertex. NULL keeps ordinary
     * screen-space interpolation; supplied depths preserve projective UVs.
     * Borrowed with vertices through draw_frame, then copied by the backend. */
    const float *projective_depth;
} maage_render_ui_data;

const char *maage_render_backend_name(maage_render_backend backend);
/* Compile-time availability only, not runtime device/display availability.
 * Unavailable backends return zero. */
int maage_render_backend_compiled(maage_render_backend backend);
/* Parse --renderer=auto|metal|vulkan|opengl|software; last occurrence wins.
 * OpenGL negotiates modern/legacy internally. Explicit selection stays within
 * its family. Removed --renderer-version flags are rejected. */
maage_status maage_render_desc_parse_args(int argc, char *const argv[],
                                          maage_render_desc *desc);
maage_status maage_renderer_get_capabilities(const maage_renderer *renderer,
                                             maage_render_capabilities *capabilities);

/* Use the window's initialization thread. The caller owns the window and must
 * keep it alive until renderer destruction. One renderer owns a window's native
 * content at a time. No native graphics types escape this interface. */
maage_status maage_render_desc_init(maage_render_desc *desc);
/* Failed creation clears *renderer. PARTIAL owns a usable fallback renderer
 * with the original candidate failure and recovery retained in trace. */
maage_status maage_renderer_create(const maage_render_desc *desc,
                                   maage_renderer **renderer, maage_status_trace *trace);
maage_status maage_renderer_destroy(maage_renderer *renderer, maage_status_trace *trace);
maage_render_backend maage_renderer_get_backend(const maage_renderer *renderer);
/* Runtime presentation synchronization. Disabling allows independently paced
 * rendering; the display may still present only the latest completed frame.
 * Unsupported implementations return NOT_IMPLEMENTED. */
maage_status maage_renderer_set_vsync(maage_renderer *renderer, int enabled);
/* Borrowed device name, valid until renderer destruction; NULL if unavailable. */
const char *maage_renderer_device_name(const maage_renderer *renderer);
/* Components must be finite values in [0, 1]. */
maage_status maage_renderer_set_clear_color(maage_renderer *renderer,
                                            float red, float green, float blue, float alpha);
/* Static vertex sky/block channels are combined with this frame's sky factor.
 * Optional screen batches draw behind geometry and before the main UI. Borrow
 * data/textures through subsequent draw_frame calls; clear before destruction.
 * Implemented mesh/UI backends only; others return UNSUPPORTED. */
maage_status maage_renderer_set_scene(maage_renderer *renderer, float sky_light,
                                      const maage_render_ui_data *background, const maage_render_ui_data *overlay);
/* Borrow a camera-local presentation mesh until cleared/destroyed; copy its
 * column-major transform. Draw after the scene with independent depth, before
 * GUI. NULL clears the pass. Requires TEXTURED_MESH and SCENE support. */
maage_status maage_renderer_set_foreground(maage_renderer *renderer,
                                           const maage_render_mesh *mesh, const float view_projection[16],
                                           float sky_light, float block_light);
/* Repeated immutable geometry with lightweight model transforms and copied
 * light. Borrowed through draw_frame; both primary backends encode the same
 * shared material/depth order and copy uniforms into their command resources.
 * Clear the array before destroying any referenced mesh; draw_frame needs a
 * world view-projection matrix while instances are installed. */
typedef struct maage_render_instance {
    const maage_render_mesh *mesh;
    float transform[16];
    float sky_light, block_light;
} maage_render_instance;
maage_status maage_renderer_set_instances(maage_renderer *renderer,
                                          const maage_render_instance *instances, size_t count);
/* Presentation clock in 20 Hz ticks; caller may wrap at a common animation
 * period. No texture uploads or mesh invalidation are needed to advance it. */
maage_status maage_renderer_set_animation_tick(maage_renderer *renderer, float tick);
/* Prime the initial clear frame before showing a hidden window. Metal and
 * Vulkan support this; UNSUPPORTED leaves others unchanged. SKIPPED means no drawable
 * yet. Does not change window visibility; normal present still skips hidden windows. */
maage_status maage_renderer_prepare_frame(maage_renderer *renderer, maage_status_trace *trace);
/* Prime a complete GUI frame while hidden, with initial-presentation sync.
 * Borrows the batch only through this call. */
maage_status maage_renderer_prepare_ui_frame(maage_renderer *renderer,
                                             const maage_render_ui_data *ui, maage_status_trace *trace);
/* Presents a cleared frame; automatically observes framebuffer size changes.
 * SKIPPED is nonfatal (hidden/minimized/unconfigured, no drawable, or a pending
 * swapchain rebuild). Keep processing events and use a bounded event wait.
 * Fatal errors require destruction; create a new renderer/window to recover. */
maage_status maage_renderer_present(maage_renderer *renderer, maage_status_trace *trace);
/* Copies indexed triangles and a top-left-origin RGBA8 atlas into an immutable
 * resource owned by this renderer. CPU data may be released after upload.
 * Destroy meshes before their renderer; renderer destruction also releases any
 * remaining meshes and invalidates their handles. Unsupported backends return
 * UNSUPPORTED rather than drawing a placeholder. */
maage_status maage_renderer_upload_mesh(maage_renderer *renderer,
                                        const maage_mesh_data *data, const maage_pixel_buffer *atlas,
                                        maage_render_mesh **mesh, maage_status_trace *trace);
/* Copies one immutable RGBA8 atlas. Geometry retains it, allowing the caller to
 * release its handle after upload. All resources belong to the renderer and are
 * invalidated by renderer destruction. Use the window initialization thread. */
maage_status maage_renderer_create_texture(maage_renderer *renderer,
                                           const maage_pixel_buffer *atlas, maage_render_texture **texture, maage_status_trace *trace);
maage_status maage_render_texture_destroy(maage_render_texture *texture);
maage_status maage_renderer_upload_geometry(maage_renderer *renderer,
                                            const maage_mesh_data *data, maage_render_texture *texture,
                                            maage_render_mesh **mesh, maage_status_trace *trace);
maage_status maage_render_mesh_destroy(maage_render_mesh *mesh);
/* Clears, draws, and presents one mesh. Uses the same visibility/error behavior
 * as present, nearest texture sampling, depth testing and CCW front faces.
 * view_projection is a column-major matrix for a right-handed world with clip
 * depth 0..W (normalized depth 0..1). No graphics API types enter the mesh. */
maage_status maage_renderer_draw_mesh(maage_renderer *renderer,
                                      const maage_render_mesh *mesh, const float view_projection[16], maage_status_trace *trace);
/* Clears and presents once for the entire batch. Meshes borrow a shared atlas;
 * zero meshes presents the clear color. Mesh order is preserved. */
maage_status maage_renderer_draw_meshes(maage_renderer *renderer,
                                        const maage_render_mesh *const *meshes, size_t count,
                                        const float view_projection[16], maage_status_trace *trace);
/* Draws the world batch, then optional UI, and presents once. UI has nearest
 * sampling, straight-alpha blending and no depth test/writes or face culling.
 * Data and the owned texture handle are borrowed through this call. Reuse CPU
 * batches afterward. Empty UI is permitted; unsupported UI returns UNSUPPORTED.
 * Meshes retain the same matrix/depth contract as draw_meshes. SUCCESS accepts
 * the frame; GPU completion/presentation may be asynchronous. With VSync off,
 * presentation may coalesce completed frames. A visible Metal window may return
 * NOOP for temporary mailbox/GPU backpressure; this is not a rendered frame.
 * Continue polling events and retry with a brief event wait rather than using
 * the long idle wait appropriate for hidden/minimized windows. Backend-owned
 * copies/resources outlive submitted work; deferred failures surface on a later
 * call or destroy. */
maage_status maage_renderer_draw_frame(maage_renderer *renderer,
                                       const maage_render_mesh *const *meshes, size_t count,
                                       const float view_projection[16], const maage_render_ui_data *ui,
                                       maage_status_trace *trace);
/* Force presentation resources to refresh on the next frame, if needed. */
maage_status maage_renderer_resize(maage_renderer *renderer);
#endif
