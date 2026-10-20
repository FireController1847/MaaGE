#include "internal.h"
#include <stdlib.h>
#include <string.h>

int maage_client_ui_register_screen(maage_client_ui *ui, maage_client_ui_screen_id id,
    maage_client_ui_screen_draw draw, maage_client_ui_screen_input input, void *context)
{
    maage_client_ui_screen *screens;
    maage_client_ui_navigation *navigation;
    size_t capacity;
    if (!ui || !id || !draw || (maage_client_ui_screen_id)(size_t)id != id) return 0;
    navigation = &ui->navigation;
    if (navigation->finalized) return 0;
    capacity = (size_t)id + 1;
    if (capacity <= (size_t)id || capacity > (size_t)-1 / sizeof(*screens)) return 0;
    if (capacity > navigation->capacity) {
        screens = (maage_client_ui_screen *)realloc(navigation->screens, capacity * sizeof(*screens));
        if (!screens) return 0;
        memset(screens + navigation->capacity, 0, (capacity - navigation->capacity) * sizeof(*screens));
        navigation->screens = screens; navigation->capacity = capacity;
    }
    screens = navigation->screens;
    if (screens[id].draw) return 0;
    screens[id].draw = draw; screens[id].input = input; screens[id].context = context;
    ++navigation->registered;
    return 1;
}

int maage_client_ui_finalize(maage_client_ui *ui)
{
    maage_client_ui_navigation *navigation;
    if (!ui) return 0;
    navigation = &ui->navigation;
    if (navigation->finalized) return 1;
    if (!navigation->registered || navigation->registered > (size_t)-1 / sizeof(*navigation->stack)) return 0;
    navigation->stack = (maage_client_ui_screen_id *)calloc(navigation->registered, sizeof(*navigation->stack));
    if (!navigation->stack) return 0;
    navigation->finalized = 1;
    return 1;
}

maage_client_ui_screen_id maage_client_ui_current(const maage_client_ui *ui)
{
    return ui && ui->navigation.depth ? ui->navigation.stack[ui->navigation.depth - 1] : MAAGE_CLIENT_UI_GAMEPLAY;
}

int maage_client_ui_open(maage_client_ui *ui, maage_client_ui_screen_id id)
{
    maage_client_ui_navigation *navigation;
    size_t i;
    if (!ui) return 0;
    navigation = &ui->navigation;
    if (!navigation->finalized || !id || (maage_client_ui_screen_id)(size_t)id != id ||
        (size_t)id >= navigation->capacity ||
        !navigation->screens[id].draw || navigation->depth >= navigation->registered) return 0;
    /* A modal may occur only once in its ancestry; stack storage is therefore
     * bounded by the finalized number of screens, without a fixed depth cap. */
    for (i = 0; i < navigation->depth; ++i) if (navigation->stack[i] == id) return 0;
    navigation->stack[navigation->depth++] = id;
    ui->selected = 0;
    return 1;
}

int maage_client_ui_back(maage_client_ui *ui)
{
    if (!ui || !ui->navigation.depth) return 0;
    --ui->navigation.depth; ui->selected = 0;
    return 1;
}

int maage_client_ui_modal(const maage_client_ui *ui)
{
    return ui && ui->navigation.depth != 0;
}
