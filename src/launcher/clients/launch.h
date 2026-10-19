#ifndef MAAGE_CLIENT_LAUNCH_H
#define MAAGE_CLIENT_LAUNCH_H
#include "registry.h"
#include "../access/access.h"
#include "maage/process.h"
/* Worker-only: reload/verify the exact managed build, check current access,
 * invoke directly with structured arguments and capture output beneath logs/. */
int maage_client_launch(const char *root, const char *id, maage_access *access,
    maage_process **process, char error[256]);
#endif
