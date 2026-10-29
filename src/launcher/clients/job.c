#include "job.h"
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <pthread.h>
#endif
struct maage_client_job {
    maage_client_registry registry;
    maage_client_candidate candidate;
    char source[MAAGE_PATH_CAPACITY], instance_id[37], error[256];
    int mode, done;
    maage_status result;
    maage_status_trace trace;
    maage_access *access;
    maage_process *process;
#ifdef _WIN32
    CRITICAL_SECTION mutex;
    HANDLE thread;
#else
    pthread_mutex_t mutex;
    pthread_t thread;
#endif
};
#ifdef _WIN32
static DWORD WINAPI maage_client_worker(LPVOID context)
#else
static void *maage_client_worker(void *context)
#endif
{
    maage_client_job *job = (maage_client_job *) context;
    maage_status result = maage_status_make(MAAGE_STATUS_NOT_IMPLEMENTED,
                                            MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_JOB, 0);
#if MAAGE_LAUNCHER_DEVELOPMENT
    if (job->mode == 1)
        result = maage_clients_probe(job->source, &job->candidate, job->error, &job->trace);
    else if (job->mode == 2)
        result = maage_clients_import(&job->registry, &job->candidate, job->error, &job->trace);
    else
#endif
        if (job->mode == 3)
        result = maage_clients_remove(&job->registry, job->source, job->error, &job->trace);
    else if (job->mode == 4)
        result = maage_client_launch(job->registry.root,
                                     job->source, job->instance_id, job->access, &job->process, job->error, &job->trace);
    if (!maage_status_is_success(result))
        MAAGE_STATUS_TRACE(&job->trace, result, MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_JOB);
#ifdef _WIN32
    EnterCriticalSection(&job->mutex);
    job->result = result;
    job->done = 1;
    LeaveCriticalSection(&job->mutex);
    return 0;
#else
    pthread_mutex_lock(&job->mutex);
    job->result = result;
    job->done = 1;
    pthread_mutex_unlock(&job->mutex);
    return NULL;
#endif
}
maage_status maage_client_job_start(const char *root, int mode, const char *source,
                                    const maage_client_candidate *candidate, maage_access *access,
                                    const char *instance_id, maage_client_job **output, const maage_status_trace *parent) {
    maage_client_job *job;
    maage_status result = maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_SUBMIT, 0);
    int native_error = 0;
    if (output) *output = NULL;
    if (!root || !output || mode < 1 || mode > 4 ||
        strlen(root) >= MAAGE_PATH_CAPACITY || (source && strlen(source) >= MAAGE_PATH_CAPACITY) ||
        (instance_id && !maage_storage_uuid_valid(instance_id)) || (mode == 4 && !instance_id))
        return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_SUBMIT, !root ? 0 : !output ? 6
                                                                                                                                    : 1);
#if !MAAGE_LAUNCHER_DEVELOPMENT
    if (mode != 3 && mode != 4) return maage_status_make(MAAGE_STATUS_NOT_IMPLEMENTED,
                                                         MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_SUBMIT, 0);
#endif
    job = (maage_client_job *) calloc(1, sizeof(*job));
    if (!job) return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED,
                                       MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_SUBMIT, 0);
    if (parent)
        job->trace = *parent;
    else
        maage_status_trace_init(&job->trace);
    MAAGE_STATUS_TRACE(&job->trace, maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_SUBMIT, 0), MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_SUBMIT);
    strcpy(job->registry.root, root);
    job->mode = mode;
    job->access = access;
    if (source) strcpy(job->source, source);
    if (instance_id) strcpy(job->instance_id, instance_id);
    if (candidate) job->candidate = *candidate;
#ifdef _WIN32
    InitializeCriticalSection(&job->mutex);
    job->thread = CreateThread(NULL, 0, maage_client_worker, job, 0, NULL);
    if (!job->thread) {
        result = maage_status_from_win32(GetLastError(), MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_SUBMIT);
        DeleteCriticalSection(&job->mutex);
        free(job);
        return result;
    }
#else
    native_error = pthread_mutex_init(&job->mutex, NULL);
    if (native_error) {
        free(job);
        return maage_status_from_errno(native_error, MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_SUBMIT);
    }
    native_error = pthread_create(&job->thread, NULL, maage_client_worker, job);
    if (native_error) {
        pthread_mutex_destroy(&job->mutex);
        free(job);
        return maage_status_from_errno(native_error, MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_SUBMIT);
    }
#endif
    (void) native_error;
    (void) result;
    *output = job;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_SUBMIT, 0);
}
maage_status maage_client_job_finish(maage_client_job **handle) {
    maage_client_job *job;
    int native_error = 0;
    if (!handle) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT,
                                          MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_DESTROY, 0);
    job = *handle;
    if (!job) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_DESTROY, 0);
#ifdef _WIN32
    if (WaitForSingleObject(job->thread, INFINITE) != WAIT_OBJECT_0)
        return maage_status_make(MAAGE_STATUS_FATAL, MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_WAIT, 0);
    CloseHandle(job->thread);
    DeleteCriticalSection(&job->mutex);
#else
    native_error = pthread_join(job->thread, NULL);
    if (native_error) return maage_status_from_errno(native_error, MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_WAIT);
    pthread_mutex_destroy(&job->mutex);
#endif
    (void) native_error;
    maage_process_close(job->process);
    maage_clients_dispose(&job->registry);
    free(job);
    *handle = NULL;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_DESTROY, 0);
}
maage_status maage_client_job_take(maage_client_job **handle,
                                   maage_client_candidate *candidate, maage_client_registry *registry,
                                   maage_process **process, char error[256], int *completed, maage_status_trace *trace) {
    maage_client_job *job;
    maage_status result, released;
    int done;
    if (completed) *completed = 0;
    if (!handle || !completed || !error) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT,
                                                                  MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_POLL, !handle ? 0 : !error ? 4
                                                                                                                                     : 5);
    job = *handle;
    if (!job) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_POLL, 0);
#ifdef _WIN32
    EnterCriticalSection(&job->mutex);
    done = job->done;
    LeaveCriticalSection(&job->mutex);
#else
    pthread_mutex_lock(&job->mutex);
    done = job->done;
    pthread_mutex_unlock(&job->mutex);
#endif
    if (!done) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_POLL, 0);
    result = job->result;
    if (trace) *trace = job->trace;
    if (registry && (job->mode == 2 || job->mode == 3) && !maage_status_failed(result)) {
        maage_clients_dispose(registry);
        *registry = job->registry;
        job->registry.builds = NULL;
    }
    if (candidate) *candidate = job->candidate;
    strcpy(error, job->error);
    if (process && job->mode == 4 && !maage_status_failed(result)) {
        *process = job->process;
        job->process = NULL;
    }
    released = maage_client_job_finish(handle);
    if (maage_status_failed(released)) return released;
    *completed = 1;
    return result;
}
