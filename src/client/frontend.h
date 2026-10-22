#ifndef MAAGE_CLIENT_FRONTEND_H
#define MAAGE_CLIENT_FRONTEND_H
#include "ui/ui.h"
#include "audio_service.h"
#include "maage/window.h"
typedef struct maage_client_frontend maage_client_frontend;
typedef struct maage_client_world_choice {
    char id[37], name[128], seed[64];
    int create, mode;
} maage_client_world_choice;
/* Registers numeric screens before UI finalization; borrows content/instance
 * paths until destroy. Catalog/admin I/O runs on a bounded Jobs worker. */
maage_status maage_client_frontend_create(maage_client_ui *ui, const maage_instance *instance,
    const maage_item_registry *items, maage_client_frontend **output, maage_status_trace *trace);
/* SUCCESS selects a world; NOOP closes Client. No Server exists in this loop. */
maage_status maage_client_frontend_run(maage_client_frontend *front,
    maage_window_system *system, maage_client_audio_service *audio,
    maage_client_world_choice *choice, maage_status_trace *trace);
maage_status maage_client_frontend_loading(maage_client_frontend *front, maage_status_trace *trace);
maage_status maage_client_frontend_destroy(maage_client_frontend *front);
#endif
