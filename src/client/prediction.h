#ifndef MAAGE_CLIENT_PREDICTION_H
#define MAAGE_CLIENT_PREDICTION_H
#include "maage/session_protocol.h"
#include "maage/render.h"
#define MAAGE_CLIENT_PREDICTIONS 32U
/* Rendering owns both the immutable authoritative mesh and a temporary view.
 * CPU sources transfer from the existing mesh mailbox; no World pointers. */
typedef struct maage_client_chunk_mesh {
    int x, z, used, filtered;
    unsigned long revision, visual_revision, presentation_generation, filtered_revision, filtered_generation;
    maage_render_mesh *mesh, *prediction;
    maage_world_mesh *source;
    size_t vertices, indices;
} maage_client_chunk_mesh;
typedef struct maage_client_prediction {
    struct {
        int active, confirmed;
        unsigned long id, revisions[9], generation;
        double predicted_at;
        unsigned long frame;
        maage_world_target target;
    } entries[MAAGE_CLIENT_PREDICTIONS];
    unsigned long next_id, session, world, generation, applied_generation, visual_revision;
    unsigned long observed_tick, frame, backlog_tick;
    double input_at;
    int diagnostics;
    maage_leaves_mode leaves;
    maage_world_target target;
    maage_item_stack held;
    double progress, delay;
    int targeting, dirty, suspended;
    maage_render_mesh *reveal;
} maage_client_prediction;
maage_status maage_client_prediction_update(maage_client_prediction *prediction, maage_renderer *renderer, maage_render_texture *atlas, const maage_item_registry *items, const maage_block_registry *blocks, maage_world *visual, const maage_session_snapshot *snapshot, maage_client_chunk_mesh *meshes, size_t capacity, const double eye[3], double yaw, double pitch, double elapsed, maage_leaves_mode leaves, int active, int held, int pressed, maage_session_command *command, maage_status_trace *trace);
int maage_client_prediction_hides(const maage_client_prediction *prediction, int x, int y, int z);
void maage_client_prediction_confirm(maage_client_prediction *prediction, const maage_session_event *event);
maage_status maage_client_prediction_destroy(maage_client_prediction *prediction);
#endif
