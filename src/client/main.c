#include "maage/log.h"
#include "maage_core.h"
#include "maage/client_version.h"
#include "maage/application.h"

int maage_application_main(int argc, char *const argv[])
{
    (void)argc; (void)argv;
    maage_log_info("maage.client", "%s Client %s", maage_core_project_name, MAAGE_CLIENT_VERSION);
    return 0;
}
