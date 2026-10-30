#ifndef MAAGE_CLIENT_PREDICTION_H
#define MAAGE_CLIENT_PREDICTION_H
#include "maage/session_protocol.h"
#include "maage/render.h"
#define MAAGE_CLIENT_PREDICTIONS 32U
/* Rendering owns both the immutable authoritative mesh and a temporary view.
 * CPU sources transfer from the existing mesh mailbox; no World pointers. */
typedef struct maage_client_chunk_mesh {
    int x, z, used, filtered, prepared_changed, prepared_filtered;
    unsigned long revision, visual_revision, presentation_generation, filtered_revision, filtered_generation;
    maage_render_mesh *mesh, *prediction, *prepared;
    maage_world_mesh *source;
    size_t vertices, indices;
} maage_client_chunk_mesh;
typedef struct maage_client_prediction {
    struct {
        int active, confirmed, submitted;
        unsigned long id, revisions[9], generation;
        double predicted_at;
        unsigned long frame;
        maage_world_target target;
        maage_block_state_id replacement;
    } entries[MAAGE_CLIENT_PREDICTIONS];
    unsigned long next_id, session, world, generation, applied_generation, visual_revision;
    unsigned long observed_tick, frame, backlog_tick;
    double input_at;
    int diagnostics;
    maage_leaves_mode leaves;
    maage_world_target target;
    maage_item_stack held;
    double progress, delay, use_delay;
    int targeting, dirty;
    maage_status failure;
    maage_block_position published[MAAGE_CLIENT_PREDICTIONS];
    size_t published_count;
    maage_render_mesh *reveal;
} maage_client_prediction;
/* Resolve input/effective states without GPU work, so reliable commands can
 * be flushed before resource preparation and frame submission. */
maage_status maage_client_prediction_update(maage_client_prediction *prediction, const maage_item_registry *items, const maage_block_registry *blocks, maage_world *visual, const maage_session_snapshot *snapshot, maage_client_chunk_mesh *meshes, size_t capacity, const double eye[3], double yaw, double pitch, double elapsed, double input_time, maage_leaves_mode leaves, int active, int held, int pressed, int use, int use_pressed, int sneaking, unsigned int hotbar, maage_session_command *command);
/* Atomically publish the prepared patch and filtered draw resources. */
maage_status maage_client_prediction_prepare(maage_client_prediction *prediction, maage_renderer *renderer, maage_render_texture *atlas, maage_world *visual, maage_client_chunk_mesh *meshes, size_t capacity, maage_status_trace *trace);
/* Prepare a streamed source against exactly the currently published patch.
 * The caller publishes both resources together only after successful upload. */
maage_status maage_client_prediction_mesh(const maage_client_prediction *prediction, maage_renderer *renderer, maage_render_texture *atlas, const maage_world_mesh *source, int x, int z, maage_render_mesh **replacement, int *filtered, maage_status_trace *trace);
void maage_client_prediction_confirm(maage_client_prediction *prediction, const maage_session_event *event);
maage_status maage_client_prediction_destroy(maage_client_prediction *prediction);
#endif
