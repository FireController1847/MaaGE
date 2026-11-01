#ifndef MAAGE_CLIENT_FRONTEND_H
#define MAAGE_CLIENT_FRONTEND_H
#include "ui/ui.h"
#include "audio_service.h"
#include "maage/window.h"
#include "maage/server_runtime.h"
#include "maage/local_session.h"
#include "maage/world.h"
#include "options.h"
typedef struct maage_client_frontend maage_client_frontend;
typedef struct maage_client_world_choice {
    char id[37], name[128], seed[64];
    int create, mode;
} maage_client_world_choice;
/* Registers numeric screens before UI finalization; borrows content/instance
 * paths until destroy. Catalog/admin I/O runs on a bounded Jobs worker. */
maage_status maage_client_frontend_create(maage_client_ui *ui, const maage_instance *instance, const maage_item_registry *items, maage_client_frontend **output, maage_status_trace *trace);
/* SUCCESS selects a world; NOOP closes Client. No Server exists in this loop. */
maage_status maage_client_frontend_run(maage_client_frontend *front, maage_window_system *system, maage_client_audio_service *audio, maage_client_world_choice *choice, maage_status_trace *trace);
maage_status maage_client_frontend_loading(maage_client_frontend *front, maage_status_trace *trace);
/* Transfer ownership of an integrated session and its presentation replica
 * to a background cleanup job. The menu remains live while its authoritative
 * Server flushes; starting another world waits for release of its file locks. */
maage_status maage_client_frontend_leave_world(maage_client_frontend *front, maage_server_runtime *server, maage_local_session *session, maage_world *visual, maage_status_trace *trace);
/* Coalesce latest Client options on one independently owned disk writer. */
maage_status maage_client_frontend_save_options(maage_client_frontend *front, const maage_client_options *options);
maage_status maage_client_frontend_destroy(maage_client_frontend *front);
#endif
