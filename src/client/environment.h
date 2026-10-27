#ifndef MAAGE_CLIENT_ENVIRONMENT_H
#define MAAGE_CLIENT_ENVIRONMENT_H
#include "maage/render.h"
#include "maage/session_protocol.h"
typedef struct maage_client_environment maage_client_environment;
maage_status maage_client_environment_create(maage_renderer *renderer,
    const char *application_data, maage_client_environment **output,
    maage_status_trace *trace);
/* First source failure is retained even when omission recovered successfully. */
maage_status maage_client_environment_resource_status(const maage_client_environment *view,
    unsigned int *failed_resources);
maage_status maage_client_environment_destroy(maage_client_environment *presentation);
/* Owner-frame presentation from copied simulation state and the current world
 * view-projection matrix. Sky directions ignore its translation. Width/height
 * select logical drawing coordinates; the matrix supplies framebuffer aspect
 * and effective FOV. No allocation, synchronization, resource acquisition or I/O. */
maage_status maage_client_environment_update(maage_client_environment *presentation,
    maage_renderer *renderer, const maage_session_snapshot *snapshot,
    float width, float height, const float view_projection[16]);
#endif
