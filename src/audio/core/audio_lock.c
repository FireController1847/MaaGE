/* The only platform synchronization shim: the device callback uses trylock,
 * never a potentially blocking maage_mutex_lock. Other calls may wait. */
#include "audio_internal.h"
#include <stdlib.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
struct maage_audio_lock { CRITICAL_SECTION mutex; };
maage_status maage_audio_lock_create(maage_audio_lock **out)
{
    maage_audio_lock *lock;
    if (!out) return maage_audio_result(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_CREATE, 0);
    *out = NULL;
    lock = (maage_audio_lock *)malloc(sizeof(*lock));
    if (!lock) return maage_audio_result(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_CREATE, 0);
    InitializeCriticalSection(&lock->mutex);
    *out = lock;
    return maage_audio_result(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_CREATE, 0);
}
void maage_audio_lock_destroy(maage_audio_lock *lock) { if (lock) { DeleteCriticalSection(&lock->mutex); free(lock); } }
void maage_audio_lock_enter(maage_audio_lock *lock) { EnterCriticalSection(&lock->mutex); }
int maage_audio_lock_try(maage_audio_lock *lock) { return TryEnterCriticalSection(&lock->mutex) != 0; }
void maage_audio_lock_leave(maage_audio_lock *lock) { LeaveCriticalSection(&lock->mutex); }
#else
#include <pthread.h>
struct maage_audio_lock { pthread_mutex_t mutex; };
maage_status maage_audio_lock_create(maage_audio_lock **out)
{
    maage_audio_lock *lock;
    int err;
    if (!out) return maage_audio_result(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_CREATE, 0);
    *out = NULL;
    lock = (maage_audio_lock *)malloc(sizeof(*lock));
    if (!lock) return maage_audio_result(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_CREATE, 0);
    err = pthread_mutex_init(&lock->mutex, NULL);
    if (err) { free(lock); return maage_status_from_errno(err, MAAGE_CONTEXT_AUDIO, MAAGE_OPERATION_CREATE); }
    *out = lock;
    return maage_audio_result(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_CREATE, 0);
}
void maage_audio_lock_destroy(maage_audio_lock *lock) { if (lock) { pthread_mutex_destroy(&lock->mutex); free(lock); } }
void maage_audio_lock_enter(maage_audio_lock *lock) { pthread_mutex_lock(&lock->mutex); }
int maage_audio_lock_try(maage_audio_lock *lock) { return pthread_mutex_trylock(&lock->mutex) == 0; }
void maage_audio_lock_leave(maage_audio_lock *lock) { pthread_mutex_unlock(&lock->mutex); }
#endif
