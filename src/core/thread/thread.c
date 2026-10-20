#if defined(__linux__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif
#include "maage/thread.h"
#include <stdlib.h>
#ifdef _WIN32
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
struct maage_mutex { CRITICAL_SECTION handle; };
struct maage_condition { CONDITION_VARIABLE handle; };
struct maage_thread { HANDLE handle; void (*function)(void *); void *context; };
static DWORD WINAPI maage_thread_entry(LPVOID context)
{
    maage_thread *thread = (maage_thread *)context;
    thread->function(thread->context); return 0;
}
#else
#include <pthread.h>
#include <unistd.h>
#include <limits.h>
#ifdef __linux__
#include <sched.h>
#endif
struct maage_mutex { pthread_mutex_t handle; };
struct maage_condition { pthread_cond_t handle; };
struct maage_thread { pthread_t handle; void (*function)(void *); void *context; };
static void *maage_thread_entry(void *context)
{
    maage_thread *thread = (maage_thread *)context;
    thread->function(thread->context); return NULL;
}
#endif
maage_mutex *maage_mutex_create(void)
{
    maage_mutex *mutex = (maage_mutex *)malloc(sizeof(*mutex));
    if (!mutex) return NULL;
#ifdef _WIN32
    InitializeCriticalSection(&mutex->handle);
#else
    if (pthread_mutex_init(&mutex->handle, NULL)) { free(mutex); return NULL; }
#endif
    return mutex;
}
void maage_mutex_destroy(maage_mutex *mutex)
{
    if (!mutex) return;
#ifdef _WIN32
    DeleteCriticalSection(&mutex->handle);
#else
    pthread_mutex_destroy(&mutex->handle);
#endif
    free(mutex);
}
void maage_mutex_lock(maage_mutex *mutex)
{
#ifdef _WIN32
    EnterCriticalSection(&mutex->handle);
#else
    pthread_mutex_lock(&mutex->handle);
#endif
}
void maage_mutex_unlock(maage_mutex *mutex)
{
#ifdef _WIN32
    LeaveCriticalSection(&mutex->handle);
#else
    pthread_mutex_unlock(&mutex->handle);
#endif
}
maage_condition *maage_condition_create(void)
{
    maage_condition *condition = (maage_condition *)malloc(sizeof(*condition));
    if (!condition) return NULL;
#ifdef _WIN32
    InitializeConditionVariable(&condition->handle);
#else
    if (pthread_cond_init(&condition->handle, NULL)) { free(condition); return NULL; }
#endif
    return condition;
}
void maage_condition_destroy(maage_condition *condition)
{
    if (!condition) return;
#ifdef _WIN32
    /* Native condition variables have no destruction operation. */
#else
    pthread_cond_destroy(&condition->handle);
#endif
    free(condition);
}
void maage_condition_wait(maage_condition *condition, maage_mutex *mutex)
{
#ifdef _WIN32
    SleepConditionVariableCS(&condition->handle, &mutex->handle, INFINITE);
#else
    pthread_cond_wait(&condition->handle, &mutex->handle);
#endif
}
void maage_condition_signal(maage_condition *condition)
{
#ifdef _WIN32
    WakeConditionVariable(&condition->handle);
#else
    pthread_cond_signal(&condition->handle);
#endif
}
void maage_condition_broadcast(maage_condition *condition)
{
#ifdef _WIN32
    WakeAllConditionVariable(&condition->handle);
#else
    pthread_cond_broadcast(&condition->handle);
#endif
}
maage_thread *maage_thread_start(void (*function)(void *), void *context)
{
    maage_thread *thread;
    if (!function) return NULL;
    thread = (maage_thread *)malloc(sizeof(*thread));
    if (!thread) return NULL;
    thread->function = function; thread->context = context;
#ifdef _WIN32
    thread->handle = CreateThread(NULL, 0, maage_thread_entry, thread, 0, NULL);
    if (!thread->handle) { free(thread); return NULL; }
#else
    if (pthread_create(&thread->handle, NULL, maage_thread_entry, thread)) { free(thread); return NULL; }
#endif
    return thread;
}
void maage_thread_join(maage_thread *thread)
{
    if (!thread) return;
#ifdef _WIN32
    WaitForSingleObject(thread->handle, INFINITE); CloseHandle(thread->handle);
#else
    pthread_join(thread->handle, NULL);
#endif
    free(thread);
}
unsigned maage_thread_processor_count(void)
{
#ifdef _WIN32
    typedef DWORD (WINAPI *maage_processor_count_function)(WORD);
    maage_processor_count_function active_processors;
    HMODULE kernel;
    DWORD processors;
    SYSTEM_INFO information;
    /* Optional Windows 7+ query includes every processor group without making
     * its symbol a load-time requirement on the threading adapter. */
    kernel = GetModuleHandleW(L"kernel32.dll");
    active_processors = kernel ? (maage_processor_count_function)GetProcAddress(kernel, "GetActiveProcessorCount") : NULL;
    if (active_processors) {
        processors = active_processors((WORD)0xffff);
        if (processors) return (unsigned)processors;
    }
    GetSystemInfo(&information);
    return (unsigned)information.dwNumberOfProcessors;
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
        for (index = 0; index < CPU_SETSIZE; ++index) if (CPU_ISSET(index, &affinity)) ++count;
        if (count) return count;
    }
#endif
#if defined(_SC_NPROCESSORS_ONLN)
    processors = sysconf(_SC_NPROCESSORS_ONLN);
    return processors > 0 && (unsigned long)processors <= UINT_MAX ? (unsigned)processors : 0;
#else
    return 0;
#endif
#endif
}
