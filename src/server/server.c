#include "maage/log.h"
#include "server.h"
#include "maage_core.h"
#include "maage/server_version.h"

int maage_server_run(void)
{
    maage_log_info("maage.server", "%s Server %s", maage_core_project_name, MAAGE_SERVER_VERSION);
    return 0;
}
