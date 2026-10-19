#include "job.h"
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <pthread.h>
#endif
struct maage_client_job {
    maage_client_registry registry;
    maage_client_candidate candidate;
    char source[MAAGE_PATH_CAPACITY], error[256];
    int mode, done, ok;
    maage_access *access;
    maage_process *process;
#ifdef _WIN32
    CRITICAL_SECTION mutex;
    HANDLE thread;
#else
    pthread_mutex_t mutex;
    pthread_t thread;
#endif
};
#ifdef _WIN32
static DWORD WINAPI maage_client_worker(LPVOID context)
#else
static void *maage_client_worker(void *context)
#endif
{
    maage_client_job *job=(maage_client_job *)context;
    int ok=0;
#if MAAGE_LAUNCHER_DEVELOPMENT
    if(job->mode==1) ok=maage_clients_probe(job->source,&job->candidate,job->error);
    else if(job->mode==2) ok=maage_clients_import(&job->registry,&job->candidate,job->error);
    else
#endif
    if(job->mode==3) ok=maage_clients_remove(&job->registry,job->source,job->error);
    else if(job->mode==4) ok=maage_client_launch(job->registry.root,job->source,job->access,&job->process,job->error);
#ifdef _WIN32
    EnterCriticalSection(&job->mutex); job->ok=ok; job->done=1; LeaveCriticalSection(&job->mutex);
    return 0;
#else
    pthread_mutex_lock(&job->mutex); job->ok=ok; job->done=1; pthread_mutex_unlock(&job->mutex);
    return NULL;
#endif
}
maage_client_job *maage_client_job_start(const char *root, int mode, const char *source, const maage_client_candidate *candidate, maage_access *access)
{
    maage_client_job *job;
#if !MAAGE_LAUNCHER_DEVELOPMENT
    if(mode!=3 && mode!=4) return NULL;
#endif
    if(strlen(root)>=MAAGE_PATH_CAPACITY || (source && strlen(source)>=MAAGE_PATH_CAPACITY)) return NULL;
    job=(maage_client_job *)calloc(1,sizeof(*job)); if(!job) return NULL;
    strcpy(job->registry.root,root); job->mode=mode; job->access=access;
    if(source) strcpy(job->source,source);
    if(candidate) job->candidate=*candidate;
#ifdef _WIN32
    InitializeCriticalSection(&job->mutex); job->thread=CreateThread(NULL,0,maage_client_worker,job,0,NULL);
    if(!job->thread) { DeleteCriticalSection(&job->mutex); free(job); return NULL; }
#else
    if(pthread_mutex_init(&job->mutex,NULL)) { free(job); return NULL; }
    if(pthread_create(&job->thread,NULL,maage_client_worker,job)) { pthread_mutex_destroy(&job->mutex); free(job); return NULL; }
#endif
    return job;
}
void maage_client_job_finish(maage_client_job **handle)
{
    maage_client_job *job=*handle;
    if(!job) return;
#ifdef _WIN32
    WaitForSingleObject(job->thread,INFINITE); CloseHandle(job->thread); DeleteCriticalSection(&job->mutex);
#else
    pthread_join(job->thread,NULL); pthread_mutex_destroy(&job->mutex);
#endif
    maage_process_close(job->process); free(job); *handle=NULL;
}
int maage_client_job_take(maage_client_job **handle, maage_client_candidate *candidate, maage_client_registry *registry, maage_process **process, char error[256])
{
    maage_client_job *job=*handle;
    int done, ok;
    if(!job) return 0;
#ifdef _WIN32
    EnterCriticalSection(&job->mutex); done=job->done; LeaveCriticalSection(&job->mutex);
#else
    pthread_mutex_lock(&job->mutex); done=job->done; pthread_mutex_unlock(&job->mutex);
#endif
    if(!done) return 0;
    ok=job->ok; if(registry && (job->mode==2 || job->mode==3) && ok) *registry=job->registry; if(candidate) *candidate=job->candidate; strcpy(error,job->error);
    if(process && job->mode==4) { *process=job->process; job->process=NULL; }
    maage_client_job_finish(handle); return ok ? 1 : -1;
}
