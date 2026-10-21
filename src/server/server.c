#include "maage/log.h"
#include "server.h"
#include "maage_core.h"
#include "maage/server_version.h"

maage_status maage_server_run(maage_status_trace *trace)
{
    maage_log_info("maage.server", "%s Server %s", maage_core_project_name, MAAGE_SERVER_VERSION);
    return MAAGE_STATUS_TRACE(trace, maage_status_make(MAAGE_STATUS_SUCCESS, 0,
        MAAGE_OPERATION_INITIALIZE, 0), MAAGE_CONTEXT_SERVER, MAAGE_OPERATION_INITIALIZE);
}
