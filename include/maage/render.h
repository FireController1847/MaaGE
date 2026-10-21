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
typedef enum maage_render_result {
    MAAGE_RENDER_OK = 0,
    MAAGE_RENDER_SKIPPED,
    MAAGE_RENDER_INVALID_ARGUMENT,
    MAAGE_RENDER_OUT_OF_MEMORY,
    MAAGE_RENDER_UNSUPPORTED,
    MAAGE_RENDER_UNAVAILABLE,
    MAAGE_RENDER_WINDOW_ERROR,
    MAAGE_RENDER_DEVICE_LOST,
    MAAGE_RENDER_SURFACE_LOST,
    MAAGE_RENDER_PLATFORM_ERROR
} maage_render_result;
/* Compatibility adapter; preserves backend-neutral results without truncating
 * native diagnostics into the detail byte. Pass a registered operation code. */
maage_status maage_render_operation_status(maage_render_result result,
    unsigned char operation);
typedef struct maage_render_desc {
    maage_window *window;
    maage_render_family family;
    unsigned long required_features; /* AUTO skips backends missing these flags. */
} maage_render_desc;

#define MAAGE_RENDER_CAPABILITIES_VERSION 1
#define MAAGE_RENDER_CAP_CLEAR             0x0001UL
#define MAAGE_RENDER_CAP_PRESENT           0x0002UL
#define MAAGE_RENDER_CAP_SHADERS           0x0004UL
#define MAAGE_RENDER_CAP_FIXED_FUNCTION    0x0008UL
#define MAAGE_RENDER_CAP_VERTEX_BUFFERS    0x0010UL
#define MAAGE_RENDER_CAP_INDEX_BUFFERS     0x0020UL
#define MAAGE_RENDER_CAP_RENDER_TARGETS    0x0040UL
#define MAAGE_RENDER_CAP_DEPTH             0x0080UL
#define MAAGE_RENDER_CAP_STENCIL           0x0100UL
#define MAAGE_RENDER_CAP_INSTANCING        0x0200UL
#define MAAGE_RENDER_CAP_COMPUTE           0x0400UL
#define MAAGE_RENDER_CAP_CPU_FRAMEBUFFER   0x0800UL
/* Implemented upload/draw commands, unlike the device capability flags above. */
#define MAAGE_RENDER_CAP_TEXTURED_MESH     0x1000UL
#define MAAGE_RENDER_CAP_UI                0x2000UL
#define MAAGE_RENDER_CAP_SCENE             0x4000UL
#define MAAGE_RENDER_FORMAT_RGBA8          0x01UL
#define MAAGE_RENDER_FORMAT_BGRA8          0x02UL
#define MAAGE_RENDER_SYNC_CPU_WAIT         0x01UL
#define MAAGE_RENDER_SYNC_FENCE            0x02UL
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
typedef struct maage_render_ui_data {
    const maage_render_ui_vertex *vertices;
    size_t vertex_count;
    const unsigned int *indices;
    size_t index_count;
    maage_render_texture *texture;
    float logical_width;
    float logical_height;
} maage_render_ui_data;

const char *maage_render_backend_name(maage_render_backend backend);
/* Compile-time availability only, not runtime device/display availability.
 * Unavailable backends return zero. */
int maage_render_backend_compiled(maage_render_backend backend);
/* Parse --renderer=auto|metal|vulkan|opengl|software; last occurrence wins.
 * OpenGL negotiates modern/legacy internally. Explicit selection stays within
 * its family. Removed --renderer-version flags are rejected. */
maage_render_result maage_render_desc_parse_args(int argc, char *const argv[],
    maage_render_desc *desc);
maage_render_result maage_renderer_get_capabilities(const maage_renderer *renderer,
    maage_render_capabilities *capabilities);

/* Use the window's initialization thread. The caller owns the window and must
 * keep it alive until renderer destruction. One renderer owns a window's native
 * content at a time. No native graphics types escape this interface. */
void maage_render_desc_init(maage_render_desc *desc);
const char *maage_render_result_string(maage_render_result result);
maage_render_result maage_renderer_create(const maage_render_desc *desc,
    maage_renderer **renderer);
void maage_renderer_destroy(maage_renderer *renderer);
maage_render_backend maage_renderer_get_backend(const maage_renderer *renderer);
/* Borrowed device name, valid until renderer destruction; NULL if unavailable. */
const char *maage_renderer_device_name(const maage_renderer *renderer);
/* Components must be finite values in [0, 1]. */
maage_render_result maage_renderer_set_clear_color(maage_renderer *renderer,
    float red, float green, float blue, float alpha);
/* Static vertex sky/block channels are combined with this frame's sky factor.
 * Optional screen batches draw behind geometry and before the main UI. Borrow
 * data/textures through subsequent draw_frame calls; clear before destruction.
 * Implemented mesh/UI backends only; others return UNSUPPORTED. */
maage_render_result maage_renderer_set_scene(maage_renderer *renderer, float sky_light,
    const maage_render_ui_data *background, const maage_render_ui_data *overlay);
/* Prime the initial clear frame before showing a hidden window. Currently Metal
 * only; UNSUPPORTED leaves other backends unchanged. SKIPPED means no drawable
 * yet. Does not change window visibility; normal present still skips hidden windows. */
maage_render_result maage_renderer_prepare_frame(maage_renderer *renderer);
/* Presents a cleared frame; automatically observes framebuffer size changes.
 * SKIPPED is nonfatal (hidden/minimized/unconfigured, no drawable, or a pending
 * swapchain rebuild). Keep processing events and use a bounded event wait.
 * Fatal errors require destruction; create a new renderer/window to recover. */
maage_render_result maage_renderer_present(maage_renderer *renderer);
/* Copies indexed triangles and a top-left-origin RGBA8 atlas into an immutable
 * resource owned by this renderer. CPU data may be released after upload.
 * Destroy meshes before their renderer; renderer destruction also releases any
 * remaining meshes and invalidates their handles. Unsupported backends return
 * UNSUPPORTED rather than drawing a placeholder. */
maage_render_result maage_renderer_upload_mesh(maage_renderer *renderer,
    const maage_mesh_data *data, const maage_pixel_buffer *atlas,
    maage_render_mesh **mesh);
/* Copies one immutable RGBA8 atlas. Geometry retains it, allowing the caller to
 * release its handle after upload. All resources belong to the renderer and are
 * invalidated by renderer destruction. Use the window initialization thread. */
maage_render_result maage_renderer_create_texture(maage_renderer *renderer,
    const maage_pixel_buffer *atlas, maage_render_texture **texture);
void maage_render_texture_destroy(maage_render_texture *texture);
maage_render_result maage_renderer_upload_geometry(maage_renderer *renderer,
    const maage_mesh_data *data, maage_render_texture *texture,
    maage_render_mesh **mesh);
void maage_render_mesh_destroy(maage_render_mesh *mesh);
/* Clears, draws, and presents one mesh. Uses the same visibility/error behavior
 * as present, nearest texture sampling, depth testing and CCW front faces.
 * view_projection is a column-major matrix for a right-handed world with clip
 * depth 0..W (normalized depth 0..1). No graphics API types enter the mesh. */
maage_render_result maage_renderer_draw_mesh(maage_renderer *renderer,
    const maage_render_mesh *mesh, const float view_projection[16]);
/* Clears and presents once for the entire batch. Meshes borrow a shared atlas;
 * zero meshes presents the clear color. Mesh order is preserved. */
maage_render_result maage_renderer_draw_meshes(maage_renderer *renderer,
    const maage_render_mesh *const *meshes, size_t count,
    const float view_projection[16]);
/* Draws the world batch, then optional UI, and presents once. UI has nearest
 * sampling, straight-alpha blending and no depth test/writes or face culling.
 * Data and the owned texture handle are borrowed through this call. Reuse CPU
 * batches afterward. Empty UI is permitted; unsupported UI returns UNSUPPORTED.
 * Meshes retain the same matrix/depth contract as draw_meshes. */
maage_render_result maage_renderer_draw_frame(maage_renderer *renderer,
    const maage_render_mesh *const *meshes, size_t count,
    const float view_projection[16], const maage_render_ui_data *ui);
/* Force presentation resources to refresh on the next frame, if needed. */
void maage_renderer_resize(maage_renderer *renderer);
#endif
