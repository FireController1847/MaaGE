#include <stdio.h>
#include "maage_core.h"
#include "maage/server_version.h"

int main(void)
{
    printf("%s Server %s\n", maage_core_project_name, MAAGE_SERVER_VERSION);
    return 0;
}
