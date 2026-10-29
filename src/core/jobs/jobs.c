#include "maage/jobs.h"
#include "maage/thread.h"
#include "maage/time.h"
#include <stdlib.h>
#include <string.h>

#define MAAGE_JOB_FREE 0
#define MAAGE_JOB_QUEUED MAAGE_JOB_STATE_QUEUED
#define MAAGE_JOB_RUNNING MAAGE_JOB_STATE_RUNNING
#define MAAGE_JOB_READY MAAGE_JOB_STATE_READY

typedef struct maage_job_record {
    maage_job_id id;
    unsigned priority, heap_index, stage;
    unsigned long order_high, order_low;
    int state, cancelled;
    double submitted;
    maage_job_run run;
    void *context;
    maage_status result;
    maage_status_trace trace;
} maage_job_record;

struct maage_job_token {
    maage_jobs *jobs;
    maage_job_record *record;
};

struct maage_jobs {
    maage_mutex *mutex;
    maage_condition *work, *completion;
    maage_thread **threads;
    maage_job_record *records;
    unsigned *heap, *free_slots, *ready_slots;
    unsigned logical_processors, capacity, workers, free_count, queued, running, ready, ready_head;
    int stopped, ids_wrapped;
    maage_job_id next_id;
    unsigned long order_high, order_low, completed, cancelled, started;
    double mean_latency, maximum_latency;
};

/* Only the short queue/ownership transitions use this mutex. User callbacks,
 * their CPU work, and completion disposal always execute without it. */
static int maage_jobs_before(maage_jobs *jobs, unsigned a, unsigned b) {
    const maage_job_record *left, *right;
    left = &jobs->records[a];
    right = &jobs->records[b];
    if (left->priority != right->priority) return left->priority < right->priority;
    if (left->order_high != right->order_high) return left->order_high < right->order_high;
    return left->order_low < right->order_low;
}

static void maage_jobs_heap_place(maage_jobs *jobs, unsigned index, unsigned slot) {
    jobs->heap[index] = slot;
    jobs->records[slot].heap_index = index;
}

static void maage_jobs_heap_up(maage_jobs *jobs, unsigned index, unsigned slot) {
    unsigned parent;
    while (index) {
        parent = (index - 1) / 2;
        if (!maage_jobs_before(jobs, slot, jobs->heap[parent])) break;
        maage_jobs_heap_place(jobs, index, jobs->heap[parent]);
        index = parent;
    }
    maage_jobs_heap_place(jobs, index, slot);
}

static void maage_jobs_heap_remove(maage_jobs *jobs, unsigned index) {
    unsigned slot, child;
    --jobs->queued;
    if (index == jobs->queued) return;
    slot = jobs->heap[jobs->queued];
    if (index && maage_jobs_before(jobs, slot, jobs->heap[(index - 1) / 2])) {
        maage_jobs_heap_up(jobs, index, slot);
        return;
    }
    while (index < jobs->queued / 2) {
        child = index * 2 + 1;
        if (child + 1 < jobs->queued && maage_jobs_before(jobs, jobs->heap[child + 1], jobs->heap[child])) ++child;
        if (!maage_jobs_before(jobs, jobs->heap[child], slot)) break;
        maage_jobs_heap_place(jobs, index, jobs->heap[child]);
        index = child;
    }
    maage_jobs_heap_place(jobs, index, slot);
}

static void maage_jobs_complete(maage_jobs *jobs, unsigned slot) {
    maage_job_record *record;
    unsigned tail;
    record = &jobs->records[slot];
    record->state = MAAGE_JOB_READY;
    tail = jobs->ready >= jobs->capacity - jobs->ready_head ? jobs->ready - (jobs->capacity - jobs->ready_head) : jobs->ready_head + jobs->ready;
    jobs->ready_slots[tail] = slot;
    ++jobs->ready;
    ++jobs->completed;
    if (record->cancelled) ++jobs->cancelled;
    maage_condition_signal(jobs->completion);
}

static void maage_jobs_worker(void *context) {
    maage_jobs *jobs;
    maage_job_record *record;
    maage_job_token token;
    unsigned slot;
    double now, latency;
    jobs = (maage_jobs *) context;
    maage_mutex_lock(jobs->mutex);
    for (;;) {
        while (!jobs->queued && !jobs->stopped)
            maage_condition_wait(jobs->work, jobs->mutex);
        if (jobs->stopped) break;
        slot = jobs->heap[0];
        record = &jobs->records[slot];
        maage_jobs_heap_remove(jobs, 0);
        record->state = MAAGE_JOB_RUNNING;
        ++jobs->running;
        record->result = maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_JOB, 0);
        now = maage_time_monotonic_seconds();
        if (record->submitted >= 0.0 && now >= record->submitted) {
            latency = now - record->submitted;
            ++jobs->started;
            if (!jobs->started) jobs->started = 1;
            jobs->mean_latency += (latency - jobs->mean_latency) / (double) jobs->started;
            if (latency > jobs->maximum_latency) jobs->maximum_latency = latency;
        }
        token.jobs = jobs;
        token.record = record;
        maage_mutex_unlock(jobs->mutex);
        record->run(record->context, &token);
        maage_mutex_lock(jobs->mutex);
        --jobs->running;
        maage_jobs_complete(jobs, slot);
        if (jobs->stopped && !jobs->running) maage_condition_broadcast(jobs->completion);
    }
    maage_mutex_unlock(jobs->mutex);
}

unsigned maage_jobs_resolve_workers(unsigned requested) {
    unsigned processors;
    if (requested) return requested;
    processors = maage_thread_processor_count();
    if (!processors) return MAAGE_JOBS_FALLBACK_WORKERS;
    return processors > 1 ? processors - 1 : 1;
}

maage_status maage_jobs_create(unsigned workers, unsigned capacity, maage_jobs **output, maage_status_trace *trace) {
    maage_jobs *jobs;
    maage_status result;
    unsigned index;
    size_t worker_slots, task_slots;
    if (!output) return MAAGE_STATUS_TRACE(trace, maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_SCHEDULER, MAAGE_OPERATION_INITIALIZE, 2), MAAGE_CONTEXT_SCHEDULER, MAAGE_OPERATION_INITIALIZE);
    *output = NULL;
    workers = maage_jobs_resolve_workers(workers);
    worker_slots = (size_t) workers;
    task_slots = (size_t) capacity;
    if (!capacity || (unsigned) worker_slots != workers || (unsigned) task_slots != capacity ||
        worker_slots > (size_t) - 1 / sizeof(maage_thread *) ||
        task_slots > (size_t) - 1 / sizeof(maage_job_record) ||
        task_slots > (size_t) - 1 / sizeof(unsigned))
        return MAAGE_STATUS_TRACE(trace, maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_SCHEDULER, MAAGE_OPERATION_INITIALIZE, 1), MAAGE_CONTEXT_SCHEDULER, MAAGE_OPERATION_INITIALIZE);
    jobs = (maage_jobs *) calloc(1, sizeof(*jobs));
    if (!jobs) return MAAGE_STATUS_TRACE(trace, maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_SCHEDULER, MAAGE_OPERATION_INITIALIZE, 0), MAAGE_CONTEXT_SCHEDULER, MAAGE_OPERATION_INITIALIZE);
    jobs->logical_processors = maage_thread_processor_count();
    jobs->capacity = capacity;
    jobs->threads = (maage_thread **) calloc(workers, sizeof(*jobs->threads));
    jobs->records = (maage_job_record *) calloc(capacity, sizeof(*jobs->records));
    jobs->heap = (unsigned *) malloc(capacity * sizeof(*jobs->heap));
    jobs->free_slots = (unsigned *) malloc(capacity * sizeof(*jobs->free_slots));
    jobs->ready_slots = (unsigned *) malloc(capacity * sizeof(*jobs->ready_slots));
    result = maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_SCHEDULER, MAAGE_OPERATION_INITIALIZE, 0);
    if (!jobs->threads || !jobs->records || !jobs->heap || !jobs->free_slots || !jobs->ready_slots) goto failed;
    result = maage_mutex_create(&jobs->mutex);
    if (maage_status_failed(result)) goto failed;
    result = maage_condition_create(&jobs->work);
    if (maage_status_failed(result)) goto failed;
    result = maage_condition_create(&jobs->completion);
    if (maage_status_failed(result)) goto failed;
    for (index = 0; index < capacity; ++index)
        jobs->free_slots[index] = index;
    jobs->free_count = capacity;
    for (index = 0; index < workers; ++index) {
        result = maage_thread_start(maage_jobs_worker, jobs, &jobs->threads[index]);
        if (maage_status_failed(result)) goto failed;
        ++jobs->workers;
    }
    *output = jobs;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_INITIALIZE, 0);
failed:
    MAAGE_STATUS_TRACE(trace, result, MAAGE_CONTEXT_THREAD, MAAGE_OPERATION_CREATE);
    maage_jobs_destroy(jobs, NULL);
    return MAAGE_STATUS_TRACE(trace, result, MAAGE_CONTEXT_SCHEDULER, MAAGE_OPERATION_INITIALIZE);
}

maage_status maage_jobs_submit(maage_jobs *jobs, unsigned priority, maage_job_run run, void *context, maage_job_id *id, const maage_status_trace *parent_trace) {
    maage_job_record *record;
    unsigned slot, index;
    int duplicate;
    if (id) *id = 0;
    if (!jobs || !run || !id) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT,
                                                       MAAGE_CONTEXT_SCHEDULER, MAAGE_OPERATION_SUBMIT, !jobs ? 0 : (!run ? 2 : 4));
    maage_mutex_lock(jobs->mutex);
    if (jobs->stopped || !jobs->free_count) {
        maage_mutex_unlock(jobs->mutex);
        return maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_SCHEDULER, MAAGE_OPERATION_SUBMIT, 0);
    }
    do {
        ++jobs->next_id;
        if (!jobs->next_id) {
            ++jobs->next_id;
            jobs->ids_wrapped = 1;
        }
        duplicate = 0;
        if (jobs->ids_wrapped)
            for (index = 0; index < jobs->capacity; ++index)
                if (jobs->records[index].state && jobs->records[index].id == jobs->next_id) {
                    duplicate = 1;
                    break;
                }
    } while (duplicate);
    slot = jobs->free_slots[--jobs->free_count];
    record = &jobs->records[slot];
    record->id = jobs->next_id;
    record->priority = priority;
    ++jobs->order_low;
    if (!jobs->order_low) ++jobs->order_high;
    record->order_high = jobs->order_high;
    record->order_low = jobs->order_low;
    record->state = MAAGE_JOB_QUEUED;
    record->cancelled = 0;
    record->stage = 0;
    record->result = maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_JOB, 0);
    if (parent_trace) record->trace = *parent_trace;
    else
        maage_status_trace_init(&record->trace);
    MAAGE_STATUS_TRACE(&record->trace, maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_SUBMIT, 0), MAAGE_CONTEXT_SCHEDULER, MAAGE_OPERATION_SUBMIT);
    record->run = run;
    record->context = context;
    record->submitted = maage_time_monotonic_seconds();
    maage_jobs_heap_up(jobs, jobs->queued++, slot);
    *id = record->id;
    maage_condition_signal(jobs->work);
    maage_mutex_unlock(jobs->mutex);
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_SUBMIT, 0);
}

maage_status maage_jobs_cancel(maage_jobs *jobs, maage_job_id id) {
    maage_job_record *record;
    unsigned index;
    if (!jobs || !id) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_SCHEDULER, MAAGE_OPERATION_CANCEL, !jobs ? 0 : 1);
    maage_mutex_lock(jobs->mutex);
    for (index = 0; index < jobs->capacity; ++index) {
        record = &jobs->records[index];
        if (record->state == MAAGE_JOB_FREE || record->id != id) continue;
        if (!record->cancelled) {
            record->cancelled = 1;
            if (record->state == MAAGE_JOB_QUEUED) {
                maage_jobs_heap_remove(jobs, record->heap_index);
                maage_jobs_complete(jobs, index);
            } else if (record->state == MAAGE_JOB_READY) ++jobs->cancelled;
        }
        maage_mutex_unlock(jobs->mutex);
        return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_CANCEL, 0);
    }
    maage_mutex_unlock(jobs->mutex);
    return maage_status_make(MAAGE_STATUS_NOT_FOUND, MAAGE_CONTEXT_SCHEDULER, MAAGE_OPERATION_CANCEL, 0);
}

int maage_job_cancelled(const maage_job_token *token) {
    int cancelled;
    maage_mutex_lock(token->jobs->mutex);
    cancelled = token->record->cancelled;
    maage_mutex_unlock(token->jobs->mutex);
    return cancelled;
}

maage_status maage_job_set_stage(const maage_job_token *token, unsigned stage) {
    if (!token) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_SCHEDULER, MAAGE_OPERATION_SET, 0);
    maage_mutex_lock(token->jobs->mutex);
    token->record->stage = stage;
    maage_mutex_unlock(token->jobs->mutex);
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_SET, 0);
}

maage_status maage_job_set_result(const maage_job_token *token, maage_status result) {
    /* Only the callback writes this field; poll cannot read it before the
     * worker's existing locked READY publication. No extra lock is needed. */
    if (!token) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_SCHEDULER, MAAGE_OPERATION_SET, 0);
    token->record->result = result;
    if (!maage_status_is_success(result)) MAAGE_STATUS_TRACE(&token->record->trace, result, MAAGE_CONTEXT_SCHEDULER, MAAGE_OPERATION_JOB);
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_SET, 0);
}

maage_status_trace *maage_job_trace(const maage_job_token *token) {
    return token ? &token->record->trace : NULL;
}

maage_status maage_jobs_poll(maage_jobs *jobs, maage_job_completion *completion) {
    maage_job_record *record;
    unsigned slot;
    if (completion) memset(completion, 0, sizeof(*completion));
    if (!jobs || !completion) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_SCHEDULER, MAAGE_OPERATION_POLL, !jobs ? 0 : 1);
    maage_mutex_lock(jobs->mutex);
    if (!jobs->ready) {
        maage_mutex_unlock(jobs->mutex);
        return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_POLL, 0);
    }
    slot = jobs->ready_slots[jobs->ready_head];
    jobs->ready_head = (jobs->ready_head + 1) % jobs->capacity;
    --jobs->ready;
    record = &jobs->records[slot];
    completion->id = record->id;
    completion->context = record->context;
    completion->cancelled = record->cancelled;
    completion->result = record->cancelled ? maage_status_make(MAAGE_STATUS_INTERRUPTED, MAAGE_CONTEXT_SCHEDULER, MAAGE_OPERATION_JOB, MAAGE_DETAIL_CANCELLED) : record->result;
    completion->operation_result = record->result;
    completion->trace = record->trace;
    if (record->cancelled) MAAGE_STATUS_TRACE(&completion->trace, completion->result, MAAGE_CONTEXT_SCHEDULER, MAAGE_OPERATION_CANCEL);
    record->state = MAAGE_JOB_FREE;
    record->context = NULL;
    jobs->free_slots[jobs->free_count++] = slot;
    maage_mutex_unlock(jobs->mutex);
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_POLL, 0);
}

maage_status maage_jobs_wait(maage_jobs *jobs) {
    int ready;
    if (!jobs) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_SCHEDULER, MAAGE_OPERATION_WAIT, 0);
    maage_mutex_lock(jobs->mutex);
    while (!jobs->ready && !(jobs->stopped && !jobs->running))
        maage_condition_wait(jobs->completion, jobs->mutex);
    ready = jobs->ready != 0;
    maage_mutex_unlock(jobs->mutex);
    return maage_status_make(ready ? MAAGE_STATUS_SUCCESS : MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_WAIT, 0);
}

maage_status maage_jobs_snapshot(maage_jobs *jobs, maage_jobs_stats *stats) {
    unsigned copied;
    if (!stats) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_SCHEDULER, MAAGE_OPERATION_SNAPSHOT, 1);
    return maage_jobs_observe(jobs, stats, NULL, 0, &copied);
}

maage_status maage_jobs_observe(maage_jobs *jobs, maage_jobs_stats *stats, maage_job_observation *observations, unsigned capacity, unsigned *copied_output) {
    maage_job_record *record;
    unsigned index, copied = 0;
    if (stats) memset(stats, 0, sizeof(*stats));
    if (copied_output) *copied_output = 0;
    if (!jobs || !copied_output) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_SCHEDULER, MAAGE_OPERATION_SNAPSHOT, !jobs ? 0 : 4);
    maage_mutex_lock(jobs->mutex);
    if (stats) {
        stats->logical_processors = jobs->logical_processors;
        stats->workers = jobs->workers;
        stats->capacity = jobs->capacity;
        stats->queued = jobs->queued;
        stats->running = jobs->running;
        stats->ready = jobs->ready;
        stats->completed = jobs->completed;
        stats->cancelled = jobs->cancelled;
        stats->mean_queue_seconds = jobs->mean_latency;
        stats->maximum_queue_seconds = jobs->maximum_latency;
    }
    if (observations)
        for (index = 0; index < jobs->capacity && copied < capacity; ++index) {
            record = &jobs->records[index];
            if (record->state == MAAGE_JOB_FREE) continue;
            observations[copied].id = record->id;
            observations[copied].context = record->context;
            observations[copied].state = (maage_job_state) record->state;
            observations[copied].stage = record->stage;
            ++copied;
        }
    maage_mutex_unlock(jobs->mutex);
    *copied_output = copied;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_SNAPSHOT, 0);
}

maage_status maage_jobs_stop(maage_jobs *jobs) {
    maage_job_record *record;
    unsigned index;
    maage_status result, failure = maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_STOP, 0);
    if (!jobs || !jobs->mutex) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_STOP, 0);
    maage_mutex_lock(jobs->mutex);
    jobs->stopped = 1;
    while (jobs->queued) {
        index = jobs->heap[0];
        record = &jobs->records[index];
        maage_jobs_heap_remove(jobs, 0);
        record->cancelled = 1;
        maage_jobs_complete(jobs, index);
    }
    if (jobs->records)
        for (index = 0; index < jobs->capacity; ++index)
            if (jobs->records[index].state == MAAGE_JOB_RUNNING) jobs->records[index].cancelled = 1;
    if (jobs->work) maage_condition_broadcast(jobs->work);
    if (jobs->completion) maage_condition_broadcast(jobs->completion);
    maage_mutex_unlock(jobs->mutex);
    for (index = 0; index < jobs->workers; ++index) {
        result = maage_thread_join(jobs->threads[index]);
        if (maage_status_failed(result) && !maage_status_failed(failure)) failure = result;
        else
            jobs->threads[index] = NULL;
    }
    return failure;
}

maage_status maage_jobs_destroy(maage_jobs *jobs, void (*dispose)(void *context)) {
    maage_job_completion completion;
    maage_status result;
    if (!jobs) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_DESTROY, 0);
    result = maage_jobs_stop(jobs);
    if (maage_status_failed(result)) return result;
    if (jobs->mutex && jobs->ready_slots)
        while (maage_status_code(maage_jobs_poll(jobs, &completion)) == MAAGE_STATUS_SUCCESS)
            if (dispose) dispose(completion.context);
    result = maage_condition_destroy(jobs->completion);
    if (maage_status_failed(result)) return result;
    jobs->completion = NULL;
    result = maage_condition_destroy(jobs->work);
    if (maage_status_failed(result)) return result;
    jobs->work = NULL;
    result = maage_mutex_destroy(jobs->mutex);
    if (maage_status_failed(result)) return result;
    jobs->mutex = NULL;
    free(jobs->threads);
    free(jobs->ready_slots);
    free(jobs->free_slots);
    free(jobs->heap);
    free(jobs->records);
    free(jobs);
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_DESTROY, 0);
}
