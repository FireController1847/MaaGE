#ifndef MAAGE_CLIENT_PLAYER_VIEW_H
#define MAAGE_CLIENT_PLAYER_VIEW_H
#include "player_model.h"
#include "item_entities.h"
typedef struct maage_client_player_view {
    maage_render_mesh *parts[MAAGE_PLAYER_PARTS];
    maage_status results[MAAGE_PLAYER_PARTS], item_failure;
    unsigned long session, world, tick, life;
    int initialized, attempted;
    double body_yaw, phase, previous_phase, amount, previous_amount, gait_sign;
    maage_render_instance draws[MAAGE_PLAYER_PARTS + 1];
    size_t count;
} maage_client_player_view;
maage_status maage_client_player_view_update(maage_client_player_view *view, maage_renderer *renderer, maage_render_texture *atlas, const maage_client_resources *resources, const maage_item_registry *items, maage_client_item_entities *models, const maage_session_snapshot *previous, const maage_session_snapshot *snapshot, double interpolation, const double eye[3], double yaw, double pitch, double elapsed, double swing_progress, int visible, double camera_distance, maage_status_trace *trace);
maage_status maage_client_player_view_destroy(maage_client_player_view *view);
#endif
