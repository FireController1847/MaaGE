#ifndef MAAGE_CLIENT_JOB_H
#define MAAGE_CLIENT_JOB_H
#include "registry.h"
#include "launch.h"
/* Worker owns all package I/O and registry writes. UI consumes only after join.
 * mode: 1 probe, 2 import, 3 remove, 4 launch. Probe/import are absent in Release. */
typedef struct maage_client_job maage_client_job;
maage_status maage_client_job_start(const char *root, int mode, const char *source,
    const maage_client_candidate *candidate, maage_access *access,
    const char *instance_id, maage_client_job **output, const maage_status_trace *parent);
/* NOOP while pending. completed distinguishes a consumed NOOP operation from
 * pending work. Completed jobs transfer owned outputs and an owned trace copy. */
maage_status maage_client_job_take(maage_client_job **job,
    maage_client_candidate *candidate, maage_client_registry *registry,
    maage_process **process, char error[256], int *completed, maage_status_trace *trace);
/* Joins and releases pending work before owner destruction; leaves any launched Client independent. */
maage_status maage_client_job_finish(maage_client_job **job);
#endif
