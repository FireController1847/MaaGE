#ifndef MAAGE_CLIENT_ENVIRONMENT_H
#define MAAGE_CLIENT_ENVIRONMENT_H
#include "maage/render.h"
#include "maage/player.h"
#include "maage/environment.h"
typedef struct maage_client_environment maage_client_environment;
maage_status maage_client_environment_create(maage_renderer *renderer,
    const char *application_data, maage_client_environment **output,
    maage_status_trace *trace);
/* First source failure is retained even when omission recovered successfully. */
maage_status maage_client_environment_resource_status(const maage_client_environment *view,
    unsigned int *failed_resources);
maage_status maage_client_environment_destroy(maage_client_environment *presentation);
/* Owner-frame presentation from copied simulation state. No world mutation,
 * resource acquisition, allocation, worker synchronization or filesystem I/O. */
maage_status maage_client_environment_update(maage_client_environment *presentation,
    maage_renderer *renderer, const maage_environment_state *state, const maage_world *world,
    const maage_player *player, double yaw, double pitch, float width, float height,
    double vertical_fov);
#endif
