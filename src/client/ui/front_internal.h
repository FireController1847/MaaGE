#ifndef MAAGE_CLIENT_UI_FRONT_INTERNAL_H
#define MAAGE_CLIENT_UI_FRONT_INTERNAL_H
#include "internal.h"
#include "front.h"
struct maage_client_ui_frontend {
    maage_client_ui *ui;
    const maage_world_summary *entries;
    size_t count, selected, first, last_row;
    unsigned int action, focus;
    int focus_keyboard;
    int busy, quit, play, mode;
    char name[128], seed[64], message[256];
    double last_click;
};
void maage_client_ui_title_draw(maage_client_ui *ui,void *context);
unsigned int maage_client_ui_title_input(maage_client_ui *ui,
    const maage_client_ui_input *input,void *context);
#endif
