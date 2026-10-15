#include "maage/application.h"
#include "server.h"

/* Frontend entry point only. The administration interface is not implemented. */
int maage_application_main(void)
{
    return maage_server_run();
}
