#ifndef MAAGE_CLIENT_ITEM_ENTITIES_H
#define MAAGE_CLIENT_ITEM_ENTITIES_H
#include "item_models.h"
#include "maage/local_session.h"
typedef struct maage_client_item_entities maage_client_item_entities;
maage_status maage_client_item_entities_create(maage_renderer *renderer,
    maage_render_texture *atlas, const maage_client_resources *resources,
    const maage_item_registry *items, const maage_block_registry *blocks,
    maage_client_item_entities **output, maage_status_trace *trace);
maage_status maage_client_item_entities_update(maage_client_item_entities *view,
    maage_local_session *session, const maage_session_snapshot *snapshot,
    double interpolation, const double eye[3], double now, maage_status_trace *trace);
maage_status maage_client_item_entities_clear(maage_client_item_entities *view);
maage_status maage_client_item_entities_destroy(maage_client_item_entities *view);
#endif
