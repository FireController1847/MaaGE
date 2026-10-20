#ifndef MAAGE_JOBS_H
#define MAAGE_JOBS_H

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
} maage_job_completion;

typedef struct maage_jobs_stats {
    unsigned logical_processors, workers, capacity, queued, running, ready; /* CPUs: 0 unknown */
    unsigned long completed, cancelled;
    double mean_queue_seconds, maximum_queue_seconds;
} maage_jobs_stats;

/* Nonzero requests pass through unchanged. Zero chooses detected logical
 * processors minus one (minimum one), or eight workers if detection fails.
 * There is no fixed worker limit. */
unsigned maage_jobs_resolve_workers(unsigned requested);

/* Capacity must be nonzero and bounds ALL accepted contexts, including
 * completed jobs not yet polled. Counts are limited only by representable
 * allocation sizes and available OS/memory resources; creation fails cleanly
 * if any worker cannot start. Arrays and threads are allocated only here. */
maage_jobs *maage_jobs_create(unsigned workers, unsigned capacity);

/* Thread-safe and nonblocking apart from brief queue synchronization. Lower
 * numeric priority runs first, FIFO within a priority. Returns 0 on saturation
 * or shutdown; the caller retains context on failure. On success the worker
 * exclusively owns context until poll transfers it back. No automatic freeing.
 * Inputs borrowed by context must remain immutable/alive through completion.
 * IDs are nonzero and valid until that completion is consumed. */
int maage_jobs_submit(maage_jobs *jobs, unsigned priority, maage_job_run run,
    void *context, maage_job_id *id);

/* Queued cancellation produces a completion without invoking run. Running
 * callbacks cooperate by querying their temporary token at useful boundaries.
 * Cancellation may win after run returns, so discard results marked cancelled. */
int maage_jobs_cancel(maage_jobs *jobs, maage_job_id id);
int maage_job_cancelled(const maage_job_token *token);

/* Poll transfers one context to the caller; callbacks never run here. Wait is
 * optional owner-thread notification, returning 0 once stopped and drained.
 * Never wait/stop/destroy from a callback or a rendering loop. */
int maage_jobs_poll(maage_jobs *jobs, maage_job_completion *completion);
int maage_jobs_wait(maage_jobs *jobs);
void maage_jobs_snapshot(maage_jobs *jobs, maage_jobs_stats *stats);

/* Owner lifecycle: stop rejects submissions, cancels accepted work, and joins
 * every worker. Completions remain pollable. Destroy also calls dispose for
 * each unconsumed context, on the calling thread and outside scheduler locks.
 * Both require exclusive lifecycle ownership; callers must end other pool API
 * calls before destroy. Running callbacks must eventually return. */
void maage_jobs_stop(maage_jobs *jobs);
void maage_jobs_destroy(maage_jobs *jobs, void (*dispose)(void *context));

#ifdef __cplusplus
}
#endif
#endif
