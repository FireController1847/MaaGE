#ifndef MAAGE_JOBS_H
#define MAAGE_JOBS_H
#include "maage/status.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Bounded CPU worker pool. No graphics, application, or native OS dependencies. */
#define MAAGE_JOBS_FALLBACK_WORKERS 8U
typedef struct maage_jobs maage_jobs;
typedef struct maage_job_token maage_job_token;
typedef unsigned long maage_job_id;
typedef void (*maage_job_run)(void *context, const maage_job_token *token);

typedef struct maage_job_completion {
    maage_job_id id;
    void *context;
    int cancelled;
    maage_status result;                             /* Published callback outcome, or scheduler interruption. */
    maage_status operation_result;                   /* Original callback outcome; NOOP if never invoked. */
    maage_status_trace trace;                        /* Owned value; no references to callback stack data. */
    double queue_seconds, run_seconds, completed_at; /* Local monotonic durations; -1 if unavailable. */
} maage_job_completion;

typedef struct maage_jobs_stats {
    unsigned logical_processors, workers, capacity, queued, running, ready; /* CPUs: 0 unknown */
    unsigned long completed, cancelled;
    double mean_queue_seconds, maximum_queue_seconds;
} maage_jobs_stats;

typedef enum maage_job_state {
    MAAGE_JOB_STATE_QUEUED = 1,
    MAAGE_JOB_STATE_RUNNING,
    MAAGE_JOB_STATE_READY
} maage_job_state;
typedef struct maage_job_observation {
    maage_job_id id;
    void *context; /* identity only; snapshot does not transfer ownership */
    maage_job_state state;
    unsigned stage; /* callback-defined numeric progress, zero until reported */
} maage_job_observation;

/* Nonzero requests pass through unchanged. Zero chooses detected logical
 * processors minus one (minimum one), or eight workers if detection fails.
 * There is no fixed worker limit. */
unsigned maage_jobs_resolve_workers(unsigned requested);

/* Capacity must be nonzero and bounds ALL accepted contexts, including
 * completed jobs not yet polled. Counts are limited only by representable
 * allocation sizes and available OS/memory resources; creation fails cleanly
 * if any worker cannot start. Arrays and threads are allocated only here. */
maage_status maage_jobs_create(unsigned workers, unsigned capacity, maage_jobs **output, maage_status_trace *trace);

/* Thread-safe and nonblocking apart from brief queue synchronization. Lower
 * numeric priority runs first, FIFO within a priority. Returns INVALID_STATE on saturation
 * or shutdown; the caller retains context on failure. On success the worker
 * exclusively owns context until poll transfers it back. No automatic freeing.
 * Inputs borrowed by context must remain immutable/alive through completion.
 * IDs are nonzero and valid until that completion is consumed. */
maage_status maage_jobs_submit(maage_jobs *jobs, unsigned priority, maage_job_run run, void *context, maage_job_id *id, const maage_status_trace *parent_trace);

/* Queued cancellation produces a completion without invoking run. Running
 * callbacks cooperate by querying their temporary token at useful boundaries.
 * Cancellation may win after run returns, so discard results marked cancelled. */
maage_status maage_jobs_cancel(maage_jobs *jobs, maage_job_id id);
int maage_job_cancelled(const maage_job_token *token);
/* Optional coarse stage transition. Uses the existing brief scheduler lock;
 * publish only at stage boundaries, never from an inner workload loop. */
maage_status maage_job_set_stage(const maage_job_token *token, unsigned stage);
/* Callback-owned result, handed off by existing completion synchronization.
 * Invoked callbacks default to SUCCESS; uninvoked callbacks retain NOOP.
 * Cancellation wins at poll without erasing completion.operation_result. */
maage_status maage_job_set_result(const maage_job_token *token, maage_status result);

/* Callback-exclusive diagnostic storage; NULL for a NULL token. It remains
 * valid only during the callback and is copied into the completion on handoff. */
maage_status_trace *maage_job_trace(const maage_job_token *token);

/* Poll returns SUCCESS for one transferred completion, NOOP when empty.
 * Submit saturation is INVALID_STATE, cancellation of unknown IDs NOT_FOUND.
 * All failed output-producing operations initialize their output to empty.
 * Poll transfers one context to the caller; callbacks never run here. Wait is
 * optional owner-thread notification, returning 0 once stopped and drained.
 * Never wait/stop/destroy from a callback or a rendering loop. */
maage_status maage_jobs_poll(maage_jobs *jobs, maage_job_completion *completion);
maage_status maage_jobs_wait(maage_jobs *jobs);
maage_status maage_jobs_snapshot(maage_jobs *jobs, maage_jobs_stats *stats);
/* One coherent scheduler snapshot, with at most capacity observations copied
 * into caller-owned storage; copied reports the count. Aggregate stats include
 * all accepted work even if observations are truncated. No callbacks or heap
 * allocations; only a brief queue lock. Stats may be NULL. Observed contexts
 * remain worker-owned until poll; never inspect fields workers may change. */
maage_status maage_jobs_observe(maage_jobs *jobs, maage_jobs_stats *stats, maage_job_observation *observations, unsigned capacity, unsigned *copied);

/* Owner lifecycle: stop rejects submissions, cancels accepted work, and joins
 * every worker. Completions remain pollable. Destroy also calls dispose for
 * each unconsumed context, on the calling thread and outside scheduler locks.
 * Both require exclusive lifecycle ownership; callers must end other pool API
 * calls before destroy. Running callbacks must eventually return. A failed
 * stop/destroy retains the pool and its borrowed inputs; callers must not free
 * those inputs until a subsequent successful drain/join. */
maage_status maage_jobs_stop(maage_jobs *jobs);
maage_status maage_jobs_destroy(maage_jobs *jobs, void (*dispose)(void *context));

#ifdef __cplusplus
}
#endif
#endif
