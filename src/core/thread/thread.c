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
    SYSTEM_INFO information;
    GetSystemInfo(&information);
    return information.dwNumberOfProcessors ? (unsigned)information.dwNumberOfProcessors : 1;
#elif defined(_SC_NPROCESSORS_ONLN)
    long processors;
    processors = sysconf(_SC_NPROCESSORS_ONLN);
    return processors > 0 && (unsigned long)processors <= UINT_MAX ? (unsigned)processors : 1;
#else
    return 1;
#endif
}
