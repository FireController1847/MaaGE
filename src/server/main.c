#include "server.h"
#include "maage/log.h"
#include <stddef.h>

int main(int argc, char **argv)
{
    int result;
    maage_log_init(NULL, argc, argv);
    result = maage_server_run();
    maage_log_shutdown();
    return result;
}
