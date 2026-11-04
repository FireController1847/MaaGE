#ifndef MAAGE_CLIENT_ITEM_ENTITIES_H
#define MAAGE_CLIENT_ITEM_ENTITIES_H
#include "item_models.h"
#include "maage/local_session.h"
#include "prediction.h"
typedef struct maage_client_item_entities maage_client_item_entities;
maage_status maage_client_item_entities_create(maage_renderer *renderer, maage_render_texture *atlas, const maage_client_resources *resources, const maage_item_registry *items, const maage_block_registry *blocks, maage_client_item_entities **output, maage_status_trace *trace);
/* Receive once before prediction, then draw from that same copied publication. */
maage_status maage_client_item_entities_update(maage_client_item_entities *view, maage_local_session *session, maage_world *visual_world, const maage_session_snapshot *snapshot, double interpolation, const double eye[3], double now, const maage_render_instance *additional, size_t additional_count, const maage_client_chunk_mesh *columns, size_t column_count, maage_status_trace *trace);
/* Receive copied views before targeting. Retains pose scratch across publications. */
maage_status maage_client_item_entities_receive(maage_client_item_entities *, maage_local_session *, const maage_session_snapshot *, double now);
int maage_client_item_entities_target(const maage_client_item_entities *, const maage_session_snapshot *, const double[3], const double[3], double limit, double now);
/* Borrow the same immutable cached geometry used by dropped item instances. */
maage_status maage_client_item_entities_mesh(maage_client_item_entities *view, const maage_item_stack *stack, const maage_render_mesh **mesh, maage_status_trace *trace);
maage_status maage_client_item_entities_break(maage_client_item_entities *, maage_world *, const maage_session_event *, double, maage_status_trace *);
maage_status maage_client_item_entities_pickup(maage_client_item_entities *view, const maage_session_event *event, double now);
/* Diagnostic submission marker; does no work without world diagnostics. */
void maage_client_item_entities_submitted(maage_client_item_entities *view);
maage_status maage_client_item_entities_clear(maage_client_item_entities *view);
maage_status maage_client_item_entities_destroy(maage_client_item_entities *view);
#endif
