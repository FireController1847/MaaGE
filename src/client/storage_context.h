#ifndef MAAGE_CLIENT_STORAGE_CONTEXT_H
#define MAAGE_CLIENT_STORAGE_CONTEXT_H
#include "maage/instance.h"
/* Startup-owned immutable paths and lifetime lease; no frame-loop probing. */
maage_instance *maage_client_storage_instance(void);
const char *maage_client_storage_data_root(void);
int maage_client_storage_migrate_requested(void);
#endif
