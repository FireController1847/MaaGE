#ifndef MAAGE_PROCESS_H
#define MAAGE_PROCESS_H
/* Direct executable invocation. argv is a NULL-terminated UTF-8 array whose
 * first element names executable. No shell; inherits the caller's environment.
 * stdout/stderr append to caller-selected files; the caller owns rotation.
 * Identical paths share one file description/handle for both streams. Capture
 * permits live readers and file replacement; stdin is disconnected.
 * start may perform OS I/O: call on a worker. poll never waits for process exit.
 * close releases monitoring without terminating the independent child. */
typedef struct maage_process maage_process;
typedef enum maage_process_state {
    MAAGE_PROCESS_STARTING, MAAGE_PROCESS_RUNNING, MAAGE_PROCESS_EXITED, MAAGE_PROCESS_FAILED
} maage_process_state;
int maage_process_start(const char *executable, const char *const argv[], const char *directory,
    const char *stdout_path, const char *stderr_path, maage_process **process, char error[256]);
maage_process_state maage_process_poll(maage_process *process, int *exit_code, char error[256]);
void maage_process_close(maage_process *process);
#endif
