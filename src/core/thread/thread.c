#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif
#if defined(__linux__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif
#include "maage/thread.h"
#include <stdlib.h>
#include <errno.h>
#ifdef _WIN32
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
struct maage_mutex {
    CRITICAL_SECTION handle;
};
struct maage_condition {
    CONDITION_VARIABLE handle;
};
struct maage_thread {
    HANDLE handle;
    void (*function)(void *);
    void *context;
};
static DWORD WINAPI maage_thread_entry(LPVOID context) {
    maage_thread *thread = (maage_thread *) context;
    thread->function(thread->context);
    return 0;
}
#else
#include <pthread.h>
#include <unistd.h>
#include <limits.h>
#include <time.h>
#ifdef __linux__
#include <sched.h>
#endif
struct maage_mutex {
    pthread_mutex_t handle;
};
struct maage_condition {
    pthread_cond_t handle;
    int monotonic;
};
struct maage_thread {
    pthread_t handle;
    void (*function)(void *);
    void *context;
};
static void *maage_thread_entry(void *context) {
    maage_thread *thread = (maage_thread *) context;
    thread->function(thread->context);
    return NULL;
}
#endif
maage_status maage_mutex_create(maage_mutex **output) {
    maage_mutex *mutex;
    int native = 0;
    if (!output) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_THREAD, MAAGE_OPERATION_CREATE, 0);
    *output = NULL;
    mutex = (maage_mutex *) malloc(sizeof(*mutex));
    if (!mutex) return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_THREAD, MAAGE_OPERATION_CREATE, 0);
#ifdef _WIN32
    InitializeCriticalSection(&mutex->handle);
#else
    native = pthread_mutex_init(&mutex->handle, NULL);
#endif
    if (native) {
        free(mutex);
        return maage_status_from_errno(native, MAAGE_CONTEXT_THREAD, MAAGE_OPERATION_CREATE);
    }
    *output = mutex;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_CREATE, 0);
}
maage_status maage_mutex_destroy(maage_mutex *mutex) {
    int native = 0;
    if (!mutex) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_DESTROY, 0);
#ifdef _WIN32
    DeleteCriticalSection(&mutex->handle);
#else
    native = pthread_mutex_destroy(&mutex->handle);
#endif
    if (native) return maage_status_from_errno(native, MAAGE_CONTEXT_THREAD, MAAGE_OPERATION_DESTROY);
    free(mutex);
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_DESTROY, 0);
}
void maage_mutex_lock(maage_mutex *mutex) {
#ifdef _WIN32
    EnterCriticalSection(&mutex->handle);
#else
    pthread_mutex_lock(&mutex->handle);
#endif
}
void maage_mutex_unlock(maage_mutex *mutex) {
#ifdef _WIN32
    LeaveCriticalSection(&mutex->handle);
#else
    pthread_mutex_unlock(&mutex->handle);
#endif
}
maage_status maage_condition_create(maage_condition **output) {
    maage_condition *condition;
    int native = 0;
    if (!output) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_THREAD, MAAGE_OPERATION_CREATE, 0);
    *output = NULL;
    condition = (maage_condition *) malloc(sizeof(*condition));
    if (!condition) return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_THREAD, MAAGE_OPERATION_CREATE, 0);
#ifdef _WIN32
    InitializeConditionVariable(&condition->handle);
#else
    condition->monotonic = 0;
#if defined(__linux__) && defined(CLOCK_MONOTONIC)
    {
        pthread_condattr_t attributes;
        int attr_status = pthread_condattr_init(&attributes);
        if (!attr_status) {
            if (!pthread_condattr_setclock(&attributes, CLOCK_MONOTONIC)) {
                condition->monotonic = 1;
                native = pthread_cond_init(&condition->handle, &attributes);
            } else
                native = pthread_cond_init(&condition->handle, NULL);
            pthread_condattr_destroy(&attributes);
        } else
            native = pthread_cond_init(&condition->handle, NULL);
    }
#else
    native = pthread_cond_init(&condition->handle, NULL);
#endif
#endif
    if (native) {
        free(condition);
        return maage_status_from_errno(native, MAAGE_CONTEXT_THREAD, MAAGE_OPERATION_CREATE);
    }
    *output = condition;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_CREATE, 0);
}
maage_status maage_condition_destroy(maage_condition *condition) {
    int native = 0;
    if (!condition) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_DESTROY, 0);
#ifndef _WIN32
    native = pthread_cond_destroy(&condition->handle);
#endif
    if (native) return maage_status_from_errno(native, MAAGE_CONTEXT_THREAD, MAAGE_OPERATION_DESTROY);
    free(condition);
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_DESTROY, 0);
}
void maage_condition_wait(maage_condition *condition, maage_mutex *mutex) {
#ifdef _WIN32
    SleepConditionVariableCS(&condition->handle, &mutex->handle, INFINITE);
#else
    pthread_cond_wait(&condition->handle, &mutex->handle);
#endif
}
maage_status maage_condition_timed_wait(maage_condition *condition, maage_mutex *mutex, unsigned long timeout_ms) {
    int native;
    if (!condition || !mutex) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_THREAD, MAAGE_OPERATION_WAIT, !condition ? 0 : 1);
#ifdef _WIN32
    if (timeout_ms > 0xffffffffUL)
        return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_THREAD, MAAGE_OPERATION_WAIT, 2);
    if (SleepConditionVariableCS(&condition->handle, &mutex->handle, (DWORD) timeout_ms)) return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_WAIT, 0);
    native = (int) GetLastError();
    if (native == ERROR_TIMEOUT) return maage_status_make(MAAGE_STATUS_TIMEOUT, MAAGE_CONTEXT_THREAD, MAAGE_OPERATION_WAIT, 0);
    return maage_status_from_win32((unsigned long) native, MAAGE_CONTEXT_THREAD, MAAGE_OPERATION_WAIT);
#else
    {
        struct timespec deadline;
#if defined(__linux__) && defined(CLOCK_MONOTONIC)
        clockid_t clock_id = condition->monotonic ? CLOCK_MONOTONIC : CLOCK_REALTIME;
#else
        clockid_t clock_id = CLOCK_REALTIME;
#endif
        if (clock_gettime(clock_id, &deadline))
            return maage_status_from_errno(errno, MAAGE_CONTEXT_THREAD, MAAGE_OPERATION_WAIT);
        /* Detect conversion overflow rather than passing an already elapsed or
         * wrapped deadline to the native condition variable. */
        if (timeout_ms / 1000UL > (unsigned long) LONG_MAX - (unsigned long) deadline.tv_sec) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_THREAD, MAAGE_OPERATION_WAIT, 2);
        deadline.tv_sec += (time_t) (timeout_ms / 1000UL);
        deadline.tv_nsec += (long) ((timeout_ms % 1000UL) * 1000000UL);
        if (deadline.tv_nsec >= 1000000000L) {
            ++deadline.tv_sec;
            deadline.tv_nsec -= 1000000000L;
        }
        native = pthread_cond_timedwait(&condition->handle, &mutex->handle, &deadline);
        if (native == ETIMEDOUT) return maage_status_make(MAAGE_STATUS_TIMEOUT, MAAGE_CONTEXT_THREAD, MAAGE_OPERATION_WAIT, 0);
        return maage_status_from_errno(native, MAAGE_CONTEXT_THREAD, MAAGE_OPERATION_WAIT);
    }
#endif
}
void maage_condition_signal(maage_condition *condition) {
#ifdef _WIN32
    WakeConditionVariable(&condition->handle);
#else
    pthread_cond_signal(&condition->handle);
#endif
}
void maage_condition_broadcast(maage_condition *condition) {
#ifdef _WIN32
    WakeAllConditionVariable(&condition->handle);
#else
    pthread_cond_broadcast(&condition->handle);
#endif
}
maage_status maage_thread_start(void (*function)(void *), void *context, maage_thread **output) {
    maage_thread *thread;
    maage_status result;
    int native = 0;
    if (!function || !output) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_THREAD, MAAGE_OPERATION_START, !function ? 0 : 2);
    *output = NULL;
    thread = (maage_thread *) malloc(sizeof(*thread));
    if (!thread) return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_THREAD, MAAGE_OPERATION_START, 0);
    thread->function = function;
    thread->context = context;
#ifdef _WIN32
    thread->handle = CreateThread(NULL, 0, maage_thread_entry, thread, 0, NULL);
    result = maage_status_from_win32(thread->handle ? 0 : GetLastError(), MAAGE_CONTEXT_THREAD, MAAGE_OPERATION_START);
#else
    native = pthread_create(&thread->handle, NULL, maage_thread_entry, thread);
    result = maage_status_from_errno(native, MAAGE_CONTEXT_THREAD, MAAGE_OPERATION_START);
#endif
    (void) native;
    if (maage_status_failed(result)) {
        free(thread);
        return result;
    }
    *output = thread;
    return result;
}
maage_status maage_thread_join(maage_thread *thread) {
    maage_status result;
    if (!thread) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_WAIT, 0);
#ifdef _WIN32
    result = maage_status_from_win32(WaitForSingleObject(thread->handle, INFINITE) == WAIT_OBJECT_0 ? 0 : GetLastError(), MAAGE_CONTEXT_THREAD, MAAGE_OPERATION_WAIT);
    if (!maage_status_failed(result) && !CloseHandle(thread->handle)) result = maage_status_from_win32(GetLastError(), MAAGE_CONTEXT_THREAD, MAAGE_OPERATION_CLOSE);
#else
    result = maage_status_from_errno(pthread_join(thread->handle, NULL), MAAGE_CONTEXT_THREAD, MAAGE_OPERATION_WAIT);
#endif
    if (!maage_status_failed(result)) free(thread);
    return result;
}
unsigned maage_thread_processor_count(void) {
#ifdef _WIN32
    typedef DWORD(WINAPI * maage_processor_count_function)(WORD);
    maage_processor_count_function active_processors;
    HMODULE kernel;
    DWORD processors;
    SYSTEM_INFO information;
    /* Optional Windows 7+ query includes every processor group without making
     * its symbol a load-time requirement on the threading adapter. */
    kernel = GetModuleHandleW(L"kernel32.dll");
    active_processors = kernel ? (maage_processor_count_function) GetProcAddress(kernel, "GetActiveProcessorCount") : NULL;
    if (active_processors) {
        processors = active_processors((WORD) 0xffff);
        if (processors) return (unsigned) processors;
    }
    GetSystemInfo(&information);
    return (unsigned) information.dwNumberOfProcessors;
#else
#if defined(_SC_NPROCESSORS_ONLN)
    long processors;
#endif
#ifdef __linux__
    cpu_set_t affinity;
    unsigned count, index;
    /* Respect a process restricted to a smaller CPU set. If the platform set
     * cannot represent the machine, fall back to its online-processor query. */
    if (!sched_getaffinity(0, sizeof(affinity), &affinity)) {
        count = 0;
        for (index = 0; index < CPU_SETSIZE; ++index)
            if (CPU_ISSET(index, &affinity)) ++count;
        if (count) return count;
    }
#endif
#if defined(_SC_NPROCESSORS_ONLN)
    processors = sysconf(_SC_NPROCESSORS_ONLN);
    return processors > 0 && (unsigned long) processors <= UINT_MAX ? (unsigned) processors : 0;
#else
    return 0;
#endif
#endif
}
