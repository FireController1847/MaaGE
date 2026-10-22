#ifndef MAAGE_CLIENT_STORAGE_CONTEXT_H
#define MAAGE_CLIENT_STORAGE_CONTEXT_H
#include "maage/instance.h"
/* Startup-owned immutable paths and lifetime lease; no frame-loop probing. */
maage_instance *maage_client_storage_instance(void);
const char *maage_client_storage_data_root(void);
/* Retain the use lease until process exit if worker shutdown cannot complete. */
void maage_client_storage_retain(void);
#endif
