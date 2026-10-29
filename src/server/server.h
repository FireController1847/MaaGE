#ifndef MAAGE_SERVER_INTERNAL_H
#define MAAGE_SERVER_INTERNAL_H
#include "maage/server_runtime.h"
/* Headless entry point, reusing the same runtime as integrated play. */
maage_status maage_server_run(const maage_server_config *config,
                              maage_status_trace *trace);
#endif
