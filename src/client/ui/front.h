#ifndef MAAGE_CLIENT_UI_FRONT_H
#define MAAGE_CLIENT_UI_FRONT_H
#include "ui.h"
#include "maage/world_store.h"
typedef struct maage_client_ui_frontend maage_client_ui_frontend;
typedef struct maage_client_ui_front_request {
    unsigned int action; /* list=1, rename=2, delete=3, play=4, create=5, quit=6 */
    size_t selected;
    int mode;
    char name[128], seed[64];
} maage_client_ui_front_request;
maage_status maage_client_ui_front_register(maage_client_ui *ui, maage_client_ui_frontend **output);
void maage_client_ui_front_destroy(maage_client_ui_frontend *front);
void maage_client_ui_front_begin(maage_client_ui_frontend *front);
void maage_client_ui_front_busy(maage_client_ui_frontend *front, int busy);
void maage_client_ui_front_complete(maage_client_ui_frontend *front,
                                    const maage_world_summary *entries, size_t count, unsigned int operation);
void maage_client_ui_front_error(maage_client_ui_frontend *front, const char *message);
int maage_client_ui_front_take(maage_client_ui_frontend *front, maage_client_ui_front_request *request);
#endif
