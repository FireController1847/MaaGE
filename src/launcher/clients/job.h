#ifndef MAAGE_CLIENT_JOB_H
#define MAAGE_CLIENT_JOB_H
#include "registry.h"
/* Worker owns all package I/O and registry writes. UI consumes only after join.
 * mode: 1 probe, 2 import, 3 remove. Probe/import are absent in Release. */
typedef struct maage_client_job maage_client_job;
maage_client_job *maage_client_job_start(const char *root, int mode, const char *source, const maage_client_candidate *candidate);
/* 0 pending; 1 success; -1 failure. Consumes completed job; clears *job. */
int maage_client_job_take(maage_client_job **job, maage_client_candidate *candidate, maage_client_registry *registry, char error[256]);
/* Joins and releases pending work before owner destruction; never runs a Client. */
void maage_client_job_finish(maage_client_job **job);
#endif
