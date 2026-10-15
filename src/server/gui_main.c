#include "maage/application.h"
#include "server.h"

/* Frontend entry point only. The administration interface is not implemented. */
int maage_application_main(int argc, char *const argv[])
{
    (void)argc; (void)argv;
    return maage_server_run();
}
