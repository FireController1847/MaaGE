#ifndef MAAGE_THREAD_H
#define MAAGE_THREAD_H
#include "maage/status.h"
/* Small native synchronization boundary. No OS types in portable consumers.
 * Conditions support multiple waiters. Always recheck the caller's predicate
 * while holding its mutex after a wake; signal/broadcast do not store wakes. */
typedef struct maage_mutex maage_mutex;
typedef struct maage_condition maage_condition;
typedef struct maage_thread maage_thread;
maage_status maage_mutex_create(maage_mutex **output);
maage_status maage_mutex_destroy(maage_mutex *mutex);
/* Lock/unlock/wake are invariant-only native primitives: valid owned handles
 * are mandatory, with no recoverable output contract on the scheduler hot path. */
void maage_mutex_lock(maage_mutex *mutex);
void maage_mutex_unlock(maage_mutex *mutex);
maage_status maage_condition_create(maage_condition **output);
maage_status maage_condition_destroy(maage_condition *condition);
void maage_condition_wait(maage_condition *condition, maage_mutex *mutex);
/* Wait for a signal or broadcast, releasing the mutex while blocked.  The
 * timeout is a relative duration in milliseconds (zero polls the condition).
 * SUCCESS means woken, TIMEOUT means the deadline elapsed, other results
 * indicate a native wait error. As with wait, callers recheck their predicate. */
maage_status maage_condition_timed_wait(maage_condition *condition, maage_mutex *mutex, unsigned long timeout_ms);
void maage_condition_signal(maage_condition *condition);
void maage_condition_broadcast(maage_condition *condition);
maage_status maage_thread_start(void (*function)(void *), void *context, maage_thread **output);
maage_status maage_thread_join(maage_thread *thread);
/* Available logical processors, respecting Linux process affinity and including
 * all Windows processor groups when supported; zero if platform queries fail. */
unsigned maage_thread_processor_count(void);
#endif
