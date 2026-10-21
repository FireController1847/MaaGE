#ifndef MAAGE_CLIENT_LAUNCH_H
#define MAAGE_CLIENT_LAUNCH_H
#include "registry.h"
#include "../access/access.h"
#include "maage/process.h"
/* Worker-only: reload/verify the exact managed build, check current access,
 * invoke directly with structured arguments and merge startup output into the
 * instance's client.log. Client Core logging owns rotation and live stdio capture. */
maage_status maage_client_launch(const char *root, const char *id, const char *instance_id, maage_access *access,
    maage_process **process, char error[256], maage_status_trace *trace);
#endif
