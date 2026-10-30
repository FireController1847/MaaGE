#ifndef MAAGE_CLIENT_INTERACTION_H
#define MAAGE_CLIENT_INTERACTION_H
#include "maage/session_protocol.h"
#include "maage/render.h"
#include "resources.h"
#include "camera.h"
struct maage_client_prediction;
typedef struct maage_client_swing {
    unsigned long session, world, life, placement_tick;
    double age, clock, next_tick, miss_until;
    int running;
} maage_client_swing;
/* One Client-local action timeline shared by first and third person. */
double maage_client_swing_update(maage_client_swing *swing, const maage_session_snapshot *snapshot, double elapsed, int active, int held, int pressed, int has_target);

typedef struct maage_client_interaction_visual {
    maage_render_mesh *mesh;
    maage_world_target target;
    unsigned long session_epoch, world_epoch;
    int visible, stage;
    maage_status last_result;
    float breaking_uv[MAAGE_CLIENT_BREAK_STAGES][4], white_uv[4];
} maage_client_interaction_visual;
typedef struct maage_client_first_person {
    const maage_client_resources *resources;
    maage_render_mesh *mesh;
    maage_item_id item_id;
    unsigned int metadata;
    unsigned long session_epoch, world_epoch, life;
    int initialized, sprite;
    double equipped, arm_yaw, arm_pitch;
    maage_status last_result;
} maage_client_first_person;
/* Copies pre-resolved crack UVs. The frame path uses bounded stack geometry and
 * uploads only when the target or damage stage changes. */
maage_status maage_client_interaction_init(maage_client_interaction_visual *visual, const maage_client_resources *resources);
maage_status maage_client_interaction_update(maage_client_interaction_visual *visual, maage_renderer *renderer, maage_render_texture *atlas, const maage_block_registry *blocks, const maage_session_snapshot *snapshot, const struct maage_client_prediction *prediction, maage_status_trace *trace);
maage_status maage_client_first_person_init(maage_client_first_person *visual, const maage_client_resources *resources);
/* Geometry is cached across frames. Only item/metadata changes upload it;
 * camera, equip, swing, bobbing and sampled light are frame uniforms. Copied
 * snapshots/events remain the only source of player/gameplay state. */
maage_status maage_client_first_person_update(maage_client_first_person *visual, maage_renderer *renderer, maage_render_texture *atlas, const maage_item_registry *items, const maage_block_registry *blocks, const maage_session_snapshot *snapshot, const maage_client_camera_effects *effects, double yaw, double pitch, double elapsed, double aspect, double swing_progress, int visible, maage_status_trace *trace);
maage_status maage_client_first_person_destroy(maage_client_first_person *visual, maage_renderer *renderer);
#endif
