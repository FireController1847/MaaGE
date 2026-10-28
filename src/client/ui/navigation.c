#include "internal.h"
#include <stdlib.h>
#include <string.h>

maage_client_ui_screen *maage_client_ui_screen_get(const maage_client_ui *ui,
    maage_client_ui_screen_id id)
{
    void **entry;
    if (!ui) return NULL;
    entry = maage_registry_pages_find(&ui->navigation.screens, (unsigned long)id);
    return entry ? (maage_client_ui_screen *)*entry : NULL;
}
maage_status maage_client_ui_register_screen(maage_client_ui *ui, maage_client_ui_screen_id id,
    maage_client_ui_screen_draw draw, maage_client_ui_screen_input input, void *context)
{
    maage_client_ui_screen *screen;
    maage_client_ui_navigation *navigation;
    void **entry;
    if (!ui || !id || !draw || (unsigned long)id > 4294967295UL)
        return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_GUI,
            MAAGE_OPERATION_REGISTER, !ui ? 0 : (!id ? 1 : 2));
    navigation = &ui->navigation;
    if (navigation->finalized) return maage_status_make(MAAGE_STATUS_INVALID_STATE,
        MAAGE_CONTEXT_GUI, MAAGE_OPERATION_REGISTER, 0);
    if (maage_client_ui_screen_get(ui, id))
        return maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_GUI, MAAGE_OPERATION_REGISTER, 0);
    screen = (maage_client_ui_screen *)calloc(1, sizeof(*screen));
    if (!screen) return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED,
        MAAGE_CONTEXT_GUI, MAAGE_OPERATION_REGISTER, 0);
    entry = maage_registry_pages_ensure(&navigation->screens, (unsigned long)id);
    if (!entry) {
        free(screen);
        return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED,
            MAAGE_CONTEXT_GUI, MAAGE_OPERATION_REGISTER, 0);
    }
    screen->draw = draw; screen->input = input; screen->context = context;
    *entry = screen;
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
    if (!maage_client_ui_screen_get(ui, id))
        return maage_status_make(MAAGE_STATUS_NOT_FOUND, MAAGE_CONTEXT_GUI, MAAGE_OPERATION_NAVIGATE, 0);
    for (i = 0; i < navigation->depth; ++i) if (navigation->stack[i] == id)
        return maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_GUI, MAAGE_OPERATION_NAVIGATE, 0);
    navigation->stack[navigation->depth++] = id; ui->selected = 0;
    ui->selected_keyboard = 0;
    ui->dragging_fov = ui->dragging_fps = ui->dragging_volume = 0;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_NAVIGATE, 0);
}

maage_status maage_client_ui_back(maage_client_ui *ui)
{
    if (!ui) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_GUI, MAAGE_OPERATION_NAVIGATE, 0);
    if (!ui->navigation.depth) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_NAVIGATE, 0);
    if ((maage_client_ui_current(ui) == MAAGE_CLIENT_UI_INVENTORY ||
        maage_client_ui_current(ui) == MAAGE_CLIENT_UI_CRAFTING_TABLE) && ui->container_command_pending) {
        ui->container_close_requested = 1;
        return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_NAVIGATE, 0);
    }
    if ((maage_client_ui_current(ui) == MAAGE_CLIENT_UI_INVENTORY ||
        maage_client_ui_current(ui) == MAAGE_CLIENT_UI_CRAFTING_TABLE) &&
        ui->has_snapshot && ui->snapshot.player.crafting.kind) {
        maage_session_command command;
        memset(&command, 0, sizeof(command)); command.kind = MAAGE_CMD_CONTAINER_CLOSE;
        command.container_identity = ui->snapshot.player.crafting.identity;
        return maage_client_ui_queue_command(ui, &command);
    }
    --ui->navigation.depth; ui->selected = 0;
    ui->selected_keyboard = 0;
    ui->dragging_fov = ui->dragging_fps = ui->dragging_volume = 0;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_NAVIGATE, 0);
}

int maage_client_ui_modal(const maage_client_ui *ui)
{
    return ui && ui->navigation.depth != 0;
}
