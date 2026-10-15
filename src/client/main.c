#include <stdio.h>
#include "maage_core.h"
#include "maage/client_version.h"

int main(void)
{
    printf("%s Client %s\n", maage_core_project_name, MAAGE_CLIENT_VERSION);
    return 0;
}
