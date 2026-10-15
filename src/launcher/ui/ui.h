#ifndef MAAGE_LAUNCHER_UI_H
#define MAAGE_LAUNCHER_UI_H
#include "maage/render.h"
#include "maage/input.h"
#include "state.h"
typedef struct maage_launcher_ui maage_launcher_ui;
/* Owns presentation only; borrowed application state must outlive the UI. */
maage_render_result maage_launcher_ui_create(maage_launcher_state *state, maage_launcher_ui **ui);
void maage_launcher_ui_destroy(maage_launcher_ui *ui);
maage_render_result maage_launcher_ui_frame(maage_launcher_ui *ui, maage_renderer *renderer,
    const maage_window_state *window, const maage_input_state *input);
#endif
