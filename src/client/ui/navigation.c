#include "internal.h"
#include <stdlib.h>
#include <string.h>

maage_status maage_client_ui_register_screen(maage_client_ui *ui, maage_client_ui_screen_id id,
    maage_client_ui_screen_draw draw, maage_client_ui_screen_input input, void *context)
{
    maage_client_ui_screen *screens;
    maage_client_ui_navigation *navigation;
    size_t capacity;
    if (!ui || !id || !draw || (maage_client_ui_screen_id)(size_t)id != id)
        return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_GUI, MAAGE_OPERATION_REGISTER, !ui ? 0 : (!id ? 1 : 2));
    navigation = &ui->navigation;
    if (navigation->finalized) return maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_GUI, MAAGE_OPERATION_REGISTER, 0);
    capacity = (size_t)id + 1;
    if (capacity <= (size_t)id || capacity > (size_t)-1 / sizeof(*screens))
        return maage_status_make(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_CONTEXT_GUI, MAAGE_OPERATION_REGISTER, 0);
    if (capacity > navigation->capacity) {
        screens = (maage_client_ui_screen *)realloc(navigation->screens, capacity * sizeof(*screens));
        if (!screens) return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_GUI, MAAGE_OPERATION_REGISTER, 0);
        memset(screens + navigation->capacity, 0, (capacity - navigation->capacity) * sizeof(*screens));
        navigation->screens = screens; navigation->capacity = capacity;
    }
    screens = navigation->screens;
    if (screens[id].draw) return maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_GUI, MAAGE_OPERATION_REGISTER, 0);
    screens[id].draw = draw; screens[id].input = input; screens[id].context = context;
    ++navigation->registered;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_REGISTER, 0);
}

maage_status maage_client_ui_finalize(maage_client_ui *ui)
{
    maage_client_ui_navigation *navigation;
    if (!ui) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_GUI, MAAGE_OPERATION_FINALIZE, 0);
    navigation = &ui->navigation;
    if (navigation->finalized) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_FINALIZE, 0);
    if (!navigation->registered) return maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_GUI, MAAGE_OPERATION_FINALIZE, 0);
    if (navigation->registered > (size_t)-1 / sizeof(*navigation->stack))
        return maage_status_make(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_CONTEXT_GUI, MAAGE_OPERATION_FINALIZE, 0);
    navigation->stack = (maage_client_ui_screen_id *)calloc(navigation->registered, sizeof(*navigation->stack));
    if (!navigation->stack) return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_GUI, MAAGE_OPERATION_FINALIZE, 0);
    navigation->finalized = 1;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_FINALIZE, 0);
}

maage_client_ui_screen_id maage_client_ui_current(const maage_client_ui *ui)
{
    return ui && ui->navigation.depth ? ui->navigation.stack[ui->navigation.depth - 1] : MAAGE_CLIENT_UI_GAMEPLAY;
}

maage_status maage_client_ui_open(maage_client_ui *ui, maage_client_ui_screen_id id)
{
    maage_client_ui_navigation *navigation;
    size_t i;
    if (!ui || !id || (maage_client_ui_screen_id)(size_t)id != id)
        return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_GUI, MAAGE_OPERATION_NAVIGATE, !ui ? 0 : 1);
    navigation = &ui->navigation;
    if (!navigation->finalized || navigation->depth >= navigation->registered)
        return maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_GUI, MAAGE_OPERATION_NAVIGATE, 0);
    if ((size_t)id >= navigation->capacity || !navigation->screens[id].draw)
        return maage_status_make(MAAGE_STATUS_NOT_FOUND, MAAGE_CONTEXT_GUI, MAAGE_OPERATION_NAVIGATE, 0);
    for (i = 0; i < navigation->depth; ++i) if (navigation->stack[i] == id)
        return maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_GUI, MAAGE_OPERATION_NAVIGATE, 0);
    navigation->stack[navigation->depth++] = id; ui->selected = 0;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_NAVIGATE, 0);
}

maage_status maage_client_ui_back(maage_client_ui *ui)
{
    if (!ui) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_GUI, MAAGE_OPERATION_NAVIGATE, 0);
    if (!ui->navigation.depth) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_NAVIGATE, 0);
    --ui->navigation.depth; ui->selected = 0;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_NAVIGATE, 0);
}

int maage_client_ui_modal(const maage_client_ui *ui)
{
    return ui && ui->navigation.depth != 0;
}
