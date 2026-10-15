#include <stdio.h>
#include "server.h"
#include "maage_core.h"
#include "maage/server_version.h"

int maage_server_run(void)
{
    printf("%s Server %s\n", maage_core_project_name, MAAGE_SERVER_VERSION);
    return 0;
}
