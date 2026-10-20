#ifndef MAAGE_THREAD_H
#define MAAGE_THREAD_H
/* Small native synchronization boundary. No OS types in portable consumers.
 * Conditions support multiple waiters. Always recheck the caller's predicate
 * while holding its mutex after a wake; signal/broadcast do not store wakes. */
typedef struct maage_mutex maage_mutex;
typedef struct maage_condition maage_condition;
typedef struct maage_thread maage_thread;
maage_mutex *maage_mutex_create(void);
void maage_mutex_destroy(maage_mutex *mutex);
void maage_mutex_lock(maage_mutex *mutex);
void maage_mutex_unlock(maage_mutex *mutex);
maage_condition *maage_condition_create(void);
void maage_condition_destroy(maage_condition *condition);
void maage_condition_wait(maage_condition *condition, maage_mutex *mutex);
void maage_condition_signal(maage_condition *condition);
void maage_condition_broadcast(maage_condition *condition);
maage_thread *maage_thread_start(void (*function)(void *), void *context);
void maage_thread_join(maage_thread *thread);
/* Available logical processors, or one when the platform query fails. */
unsigned maage_thread_processor_count(void);
#endif
