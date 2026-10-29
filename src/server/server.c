#include "server.h"
#include "maage/log.h"
#include "maage/thread.h"
#include "maage_core.h"
#include "maage/server_version.h"
#include <signal.h>

static volatile sig_atomic_t maage_server_stop_signal = 0;

static void maage_server_signal_handler(int signo) {
    (void) signo;
    maage_server_stop_signal = 1;
}

maage_status maage_server_run(const maage_server_config *config, maage_status_trace *trace) {
    maage_server_runtime *runtime = NULL;
    maage_mutex *mutex = NULL;
    maage_condition *condition = NULL;
    maage_status result, closing, waited;
    maage_status_trace shutdown_trace;
    void (*old_interrupt)(int), (*old_terminate)(int);
    maage_log_info("maage.server", "%s Server %s", maage_core_project_name, MAAGE_SERVER_VERSION);
    result = maage_server_runtime_create(config, &runtime, trace);
    if (maage_status_failed(result)) return result;
    result = maage_mutex_create(&mutex);
    if (maage_status_failed(result)) goto done;
    result = maage_condition_create(&condition);
    if (maage_status_failed(result)) goto done;
    maage_server_stop_signal = 0;
    old_interrupt = signal(SIGINT, maage_server_signal_handler);
    old_terminate = signal(SIGTERM, maage_server_signal_handler);
    result = maage_server_runtime_start(runtime, trace);
    if (!maage_status_failed(result)) {
        /* A timed wait is used only for dedicated console shutdown. The
         * simulation is independently scheduled inside the runtime thread. */
        maage_mutex_lock(mutex);
        while (!maage_server_stop_signal && !maage_server_runtime_finished(runtime)) {
            waited = maage_condition_timed_wait(condition, mutex, 200UL);
            if (maage_status_failed(waited) && maage_status_code(waited) != MAAGE_STATUS_TIMEOUT) {
                result = MAAGE_STATUS_PROPAGATE(trace, waited, MAAGE_CONTEXT_SERVER, MAAGE_OPERATION_WAIT);
                break;
            }
        }
        maage_mutex_unlock(mutex);
        maage_server_runtime_request_stop(runtime);
        maage_status_trace_init(&shutdown_trace);
        closing = maage_server_runtime_join(runtime, &shutdown_trace);
        if (maage_status_failed(closing)) {
            result = closing;
            if (trace) *trace = shutdown_trace;
        }
    }
    signal(SIGINT, old_interrupt);
    signal(SIGTERM, old_terminate);
done:
    maage_condition_destroy(condition);
    maage_mutex_destroy(mutex);
    closing = maage_server_runtime_destroy(runtime);
    if (maage_status_failed(closing) && !maage_status_failed(result)) result = MAAGE_STATUS_PROPAGATE(trace, closing, MAAGE_CONTEXT_SERVER, MAAGE_OPERATION_DESTROY);
    return result;
}
