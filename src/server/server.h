#ifndef MAAGE_SERVER_INTERNAL_H
#define MAAGE_SERVER_INTERNAL_H
#include "maage/status.h"
/* Shared server startup; no windowing, rendering, or frontend ownership. */
maage_status maage_server_run(maage_status_trace *trace);
#endif
