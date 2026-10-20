#include "maage/thread.h"
#include <stdlib.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
struct maage_mutex { CRITICAL_SECTION handle; };
struct maage_condition { HANDLE handle; };
struct maage_thread { HANDLE handle; void (*function)(void *); void *context; };
static DWORD WINAPI maage_thread_entry(LPVOID context)
{
    maage_thread *thread = (maage_thread *)context;
    thread->function(thread->context); return 0;
}
#else
#include <pthread.h>
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
    condition->handle = CreateEventW(NULL, FALSE, FALSE, NULL);
    if (!condition->handle) { free(condition); return NULL; }
#else
    if (pthread_cond_init(&condition->handle, NULL)) { free(condition); return NULL; }
#endif
    return condition;
}
void maage_condition_destroy(maage_condition *condition)
{
    if (!condition) return;
#ifdef _WIN32
    CloseHandle(condition->handle);
#else
    pthread_cond_destroy(&condition->handle);
#endif
    free(condition);
}
void maage_condition_wait(maage_condition *condition, maage_mutex *mutex)
{
#ifdef _WIN32
    LeaveCriticalSection(&mutex->handle);
    WaitForSingleObject(condition->handle, INFINITE);
    EnterCriticalSection(&mutex->handle);
#else
    pthread_cond_wait(&condition->handle, &mutex->handle);
#endif
}
void maage_condition_signal(maage_condition *condition)
{
#ifdef _WIN32
    SetEvent(condition->handle);
#else
    pthread_cond_signal(&condition->handle);
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
