#include "state.h"
#include <string.h>

static int maage_launcher_same_id(const char *left, const char *right)
{
    return left && right && strcmp(left, right) == 0;
}
void maage_launcher_state_init(maage_launcher_state *state)
{
    memset(state, 0, sizeof(*state));
    maage_launcher_settings_init(&state->settings);
}
const maage_launcher_account *maage_launcher_selected_account(const maage_launcher_state *state)
{
    size_t index;
    if (state->accounts.items) for (index = 0; index < state->accounts.count; ++index)
        if (maage_launcher_same_id(state->accounts.items[index].id, state->accounts.selected_id)) return &state->accounts.items[index];
    return NULL;
}
const maage_launcher_installation *maage_launcher_selected_installation(const maage_launcher_state *state)
{
    size_t index;
    if (state->installations.items) for (index = 0; index < state->installations.count; ++index)
        if (maage_launcher_same_id(state->installations.items[index].id, state->installations.selected_id)) return &state->installations.items[index];
    return NULL;
}
static const maage_launcher_service *maage_launcher_service_for(const maage_launcher_state *state, maage_launcher_action action)
{
    if (!state) return NULL;
    switch (action) {
    case MAAGE_ACTION_SIGN_IN: case MAAGE_ACTION_CANCEL_SIGN_IN:
    case MAAGE_ACTION_SELECT_ACCOUNT: case MAAGE_ACTION_SIGN_OUT: return &state->accounts.service;
    case MAAGE_ACTION_CREATE_INSTALLATION: case MAAGE_ACTION_EDIT_INSTALLATION:
    case MAAGE_ACTION_REMOVE_INSTALLATION: case MAAGE_ACTION_SELECT_INSTALLATION:
    case MAAGE_ACTION_PLAY: return &state->installations.service;
    case MAAGE_ACTION_REFRESH_NEWS: return &state->news.service;
    default: return NULL;
    }
}
int maage_launcher_can_request(const maage_launcher_state *state, maage_launcher_action action, const char *id)
{
    const maage_launcher_service *service = maage_launcher_service_for(state, action);
    const maage_launcher_installation *installation;
    size_t index;
    if (!service || !service->request || !(service->actions & action)) return 0;
    switch (action) {
    case MAAGE_ACTION_SIGN_IN: return state->accounts.status != MAAGE_ACCOUNT_AUTHENTICATING;
    case MAAGE_ACTION_CANCEL_SIGN_IN: return state->accounts.status == MAAGE_ACCOUNT_AUTHENTICATING;
    case MAAGE_ACTION_SIGN_OUT: case MAAGE_ACTION_SELECT_ACCOUNT:
        if (state->accounts.items) for (index = 0; index < state->accounts.count; ++index)
            if (maage_launcher_same_id(id, state->accounts.items[index].id)) return 1;
        return 0;
    case MAAGE_ACTION_CREATE_INSTALLATION:
        return state->installations.status != MAAGE_CONTENT_LOADING;
    case MAAGE_ACTION_EDIT_INSTALLATION: case MAAGE_ACTION_REMOVE_INSTALLATION:
    case MAAGE_ACTION_SELECT_INSTALLATION: case MAAGE_ACTION_PLAY:
        if (state->installations.status != MAAGE_CONTENT_READY || !state->installations.items) return 0;
        for (index = 0; index < state->installations.count; ++index) {
            installation = &state->installations.items[index];
            if (maage_launcher_same_id(id, installation->id))
                return action != MAAGE_ACTION_PLAY || (installation->launchable && maage_launcher_same_id(id, state->installations.selected_id));
        }
        return 0;
    case MAAGE_ACTION_REFRESH_NEWS: return state->news.status != MAAGE_CONTENT_LOADING;
    default: return 0;
    }
}
maage_launcher_result maage_launcher_request(maage_launcher_state *state, maage_launcher_action action, const char *id)
{
    const maage_launcher_service *service = maage_launcher_service_for(state, action);
    maage_launcher_result result;
    if (!service) return MAAGE_LAUNCHER_INVALID_ARGUMENT;
    result = maage_launcher_can_request(state, action, id) ? service->request(service->context, action, id) : MAAGE_LAUNCHER_UNAVAILABLE;
    state->notice = result == MAAGE_LAUNCHER_OK ? NULL : result == MAAGE_LAUNCHER_UNAVAILABLE ?
        "This action is not available yet." : "The action could not be completed. Please try again.";
    return result;
}
