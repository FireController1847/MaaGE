#include "server.h"
#include "maage/log.h"
#include <stddef.h>

int main(int argc, char **argv)
{
    maage_status result, shutdown;
    maage_status_trace trace;
    maage_status_trace_init(&trace);
    result = maage_log_init(NULL, argc, argv);
    if (!maage_status_failed(result)) result = maage_server_run(&trace);
    if (maage_status_failed(result))
        maage_log_trace_status(MAAGE_LOG_ERROR, "maage.server", result, &trace, "Server stopped.");
    shutdown = maage_log_shutdown();
    return maage_status_failed(result) || maage_status_failed(shutdown) ? 1 : 0;
}
