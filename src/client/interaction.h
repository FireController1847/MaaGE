#ifndef MAAGE_CLIENT_INTERACTION_H
#define MAAGE_CLIENT_INTERACTION_H
#include "maage/session_protocol.h"
#include "maage/render.h"
#include "resources.h"
typedef struct maage_client_interaction_visual {
    maage_render_mesh *mesh;
    maage_world_target target;
    unsigned long session_epoch, world_epoch;
    int visible, stage;
    maage_status last_result;
    float breaking_uv[MAAGE_CLIENT_BREAK_STAGES][MAAGE_CLIENT_TEXTURE_COUNT][4];
} maage_client_interaction_visual;
/* Copies pre-resolved crack UVs. The frame path uses bounded stack geometry and
 * uploads only when the target or damage stage changes. */
maage_status maage_client_interaction_init(maage_client_interaction_visual *visual,
    const maage_client_resources *resources);
maage_status maage_client_interaction_update(maage_client_interaction_visual *visual,
    maage_renderer *renderer, maage_render_texture *atlas,
    const maage_block_registry *blocks, const maage_session_snapshot *snapshot,
    maage_status_trace *trace);
#endif
