#ifndef MAAGE_CLIENT_DOCK_NAME_H
#define MAAGE_CLIENT_DOCK_NAME_H
#include "maage/status.h"
/* Optional desktop branding. Call on the main thread after AppKit launch. */
maage_status maage_client_dock_name(maage_status_trace *trace);
#endif
