#ifdef __APPLE__
#define _DARWIN_C_SOURCE 1
#endif
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include "maage/process.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#ifdef _WIN32
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
struct maage_process {
    HANDLE handle;
    maage_process_state state;
    int code;
};
static wchar_t *maage_process_wide(const char *text) {
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, NULL, 0);
    wchar_t *w = n ? (wchar_t *) malloc((size_t) n * sizeof(*w)) : NULL;
    if (w && !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, w, n)) {
        free(w);
        return NULL;
    }
    return w;
}
static HANDLE maage_process_capture(const char *path, SECURITY_ATTRIBUTES *security) {
    wchar_t *wide = maage_process_wide(path);
    HANDLE handle;
    BY_HANDLE_FILE_INFORMATION info;
    DWORD failure;
    if (!wide) return INVALID_HANDLE_VALUE;
    handle = CreateFileW(wide, FILE_APPEND_DATA | FILE_READ_ATTRIBUTES,
                         FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, security, OPEN_ALWAYS,
                         FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
    failure = GetLastError();
    free(wide);
    if (handle == INVALID_HANDLE_VALUE) {
        SetLastError(failure);
        return handle;
    }
    if (GetFileType(handle) != FILE_TYPE_DISK) {
        CloseHandle(handle);
        SetLastError(ERROR_INVALID_DATA);
        return INVALID_HANDLE_VALUE;
    }
    if (!GetFileInformationByHandle(handle, &info)) {
        failure = GetLastError();
        CloseHandle(handle);
        SetLastError(failure);
        return INVALID_HANDLE_VALUE;
    }
    if (info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) {
        CloseHandle(handle);
        SetLastError(ERROR_INVALID_DATA);
        return INVALID_HANDLE_VALUE;
    }
    return handle;
}
/* Microsoft CRT command-line quoting, including trailing backslashes. */
static wchar_t *maage_process_arguments(const char *const argv[]) {
    size_t total = 1, i, n, slash, j;
    wchar_t *line, *out, *arg, *p;
    for (i = 0; argv[i]; ++i) {
        total += strlen(argv[i]) * 2 + 4;
        if (total > 32767) return NULL;
    }
    line = (wchar_t *) malloc(total * sizeof(*line));
    if (!line) return NULL;
    out = line;
    for (i = 0; argv[i]; ++i) {
        arg = maage_process_wide(argv[i]);
        if (!arg) {
            free(line);
            return NULL;
        }
        if (i) *out++ = L' ';
        *out++ = L'"';
        p = arg;
        while (*p) {
            slash = 0;
            while (*p == L'\\') {
                ++slash;
                ++p;
            }
            n = (*p == L'"' || !*p) ? slash * 2 : slash;
            for (j = 0; j < n; ++j)
                *out++ = L'\\';
            if (*p == L'"') *out++ = L'\\';
            if (*p) *out++ = *p++;
        }
        *out++ = L'"';
        free(arg);
    }
    *out = 0;
    return line;
}
maage_status maage_process_start(const char *exe, const char *const argv[], const char *directory,
                                 const char *stdout_path, const char *stderr_path, maage_process **process, char error[256], maage_status_trace *trace) {
    STARTUPINFOEXW startup;
    PROCESS_INFORMATION info;
    HANDLE handles[3];
    SECURITY_ATTRIBUTES security;
    wchar_t *native = NULL, *cwd = NULL, *command = NULL;
    SIZE_T size = 0, handle_bytes = sizeof(handles);
    int initialized = 0, ok = 0;
    DWORD failure = 0;
    maage_process *p;
    maage_status result;
    if (process) *process = NULL;
    if (!exe || !argv || !argv[0] || !directory || !stdout_path || !stderr_path || !process || !error)
        return MAAGE_STATUS_TRACE(trace, maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_PROCESS, MAAGE_OPERATION_START, MAAGE_DETAIL_ARGUMENT_UNKNOWN), MAAGE_CONTEXT_PROCESS, MAAGE_OPERATION_START);
    error[0] = 0;
    p = (maage_process *) calloc(1, sizeof(*p));
    *process = NULL;
    memset(&startup, 0, sizeof(startup));
    memset(&info, 0, sizeof(info));
    memset(handles, 0, sizeof(handles));
    memset(&security, 0, sizeof(security));
    security.nLength = sizeof(security);
    security.bInheritHandle = TRUE;
    if (!p || !argv || !argv[0]) goto done;
    native = maage_process_wide(exe);
    cwd = maage_process_wide(directory);
    command = maage_process_arguments(argv);
    if (!native || !cwd || !command) goto done;
    handles[1] = maage_process_capture(stdout_path, &security);
    if (handles[1] == INVALID_HANDLE_VALUE) goto done;
    handles[2] = !strcmp(stdout_path, stderr_path) ? handles[1] : maage_process_capture(stderr_path, &security);
    if (handles[2] == INVALID_HANDLE_VALUE) goto done;
    if (handles[1] == handles[2]) handle_bytes = 2 * sizeof(handles[0]);
    handles[0] = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &security, OPEN_EXISTING, 0, NULL);
    if (handles[0] == INVALID_HANDLE_VALUE) goto done;
    InitializeProcThreadAttributeList(NULL, 1, 0, &size);
    startup.lpAttributeList = (LPPROC_THREAD_ATTRIBUTE_LIST) malloc(size);
    if (!startup.lpAttributeList) goto done;
    if (!InitializeProcThreadAttributeList(startup.lpAttributeList, 1, 0, &size)) goto done;
    initialized = 1;
    if (!UpdateProcThreadAttribute(startup.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, handles, handle_bytes, NULL, NULL)) goto done;
    startup.StartupInfo.cb = sizeof(startup);
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput = handles[0];
    startup.StartupInfo.hStdOutput = handles[1];
    startup.StartupInfo.hStdError = handles[2];
    if (!CreateProcessW(native, command, NULL, NULL, TRUE, EXTENDED_STARTUPINFO_PRESENT | CREATE_NEW_PROCESS_GROUP | DETACHED_PROCESS,
                        NULL, cwd, &startup.StartupInfo, &info)) goto done;
    CloseHandle(info.hThread);
    p->handle = info.hProcess;
    p->state = MAAGE_PROCESS_RUNNING;
    *process = p;
    ok = 1;
done:
    failure = GetLastError();
    if (initialized) DeleteProcThreadAttributeList(startup.lpAttributeList);
    free(startup.lpAttributeList);
    free(native);
    free(cwd);
    free(command);
    if (handles[0] && handles[0] != INVALID_HANDLE_VALUE) CloseHandle(handles[0]);
    if (handles[1] && handles[1] != INVALID_HANDLE_VALUE) CloseHandle(handles[1]);
    if (handles[2] && handles[2] != INVALID_HANDLE_VALUE && handles[2] != handles[1]) CloseHandle(handles[2]);
    if (!ok) {
        free(p);
        sprintf(error, "Cannot create Client process (Windows error %lu).", (unsigned long) failure);
    }
    result = ok ? maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_START, 0) : maage_status_from_win32(failure, MAAGE_CONTEXT_PROCESS, MAAGE_OPERATION_START);
    if (!ok && !maage_status_failed(result)) result = maage_status_make(MAAGE_STATUS_ERROR, MAAGE_CONTEXT_PROCESS, MAAGE_OPERATION_START, 0);
    return maage_status_is_success(result) ? result : maage_status_trace_record(trace, result, MAAGE_CONTEXT_PROCESS, MAAGE_OPERATION_START, __FILE__, (unsigned long) __LINE__, (long) failure);
}
maage_status maage_process_poll(maage_process *p, int *code, maage_process_state *state,
                                char error[256], maage_status_trace *trace) {
    DWORD status, exit, failure = 0;
    maage_status result;
    if (state) *state = MAAGE_PROCESS_FAILED;
    if (code) *code = 0;
    if (!p || !code || !state || !error) return MAAGE_STATUS_TRACE(trace,
                                                                   maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_PROCESS, MAAGE_OPERATION_POLL,
                                                                                     MAAGE_DETAIL_ARGUMENT_UNKNOWN),
                                                                   MAAGE_CONTEXT_PROCESS, MAAGE_OPERATION_POLL);
    error[0] = 0;
    status = WaitForSingleObject(p->handle, 0);
    if (status == WAIT_OBJECT_0 && p->state == MAAGE_PROCESS_RUNNING) {
        if (GetExitCodeProcess(p->handle, &exit)) {
            p->state = MAAGE_PROCESS_EXITED;
            p->code = (int) exit;
        } else {
            p->state = MAAGE_PROCESS_FAILED;
            sprintf(error, "Cannot read process exit status (Windows error %lu).", (unsigned long) GetLastError());
        }
    } else if (status == WAIT_FAILED) {
        p->state = MAAGE_PROCESS_FAILED;
        sprintf(error, "Cannot monitor process (Windows error %lu).", (unsigned long) GetLastError());
    }
    if (p->state == MAAGE_PROCESS_FAILED) failure = GetLastError();
    *code = p->code;
    *state = p->state;
    result = p->state == MAAGE_PROCESS_FAILED ? maage_status_from_win32(failure ? failure : ERROR_GEN_FAILURE,
                                                                        MAAGE_CONTEXT_PROCESS, MAAGE_OPERATION_POLL)
                                              : maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_POLL, 0);
    return maage_status_is_success(result) ? result : maage_status_trace_record(trace, result, MAAGE_CONTEXT_PROCESS, MAAGE_OPERATION_POLL, __FILE__, (unsigned long) __LINE__, (long) failure);
}
maage_status maage_process_close(maage_process *p) {
    maage_status result;
    if (!p) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_CLOSE, 0);
    result = maage_status_from_win32(CloseHandle(p->handle) ? 0 : GetLastError(), MAAGE_CONTEXT_PROCESS, MAAGE_OPERATION_CLOSE);
    free(p);
    return result;
}
#else
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <errno.h>
#include <pthread.h>
#include <limits.h>
extern char **environ;
struct maage_process {
    pid_t pid;
    int pipe, failure, code;
    maage_process_state state;
};
static int maage_process_cloexec(int fd) {
    int flags = fcntl(fd, F_GETFD, 0);
    return flags >= 0 && fcntl(fd, F_SETFD, flags | FD_CLOEXEC) == 0;
}
static int maage_process_capture(const char *path) {
    struct stat st;
    int fd = open(path, O_WRONLY | O_CREAT | O_APPEND | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK, 0600);
    int failure, moved;
    if (fd < 0) return -1;
    if (fstat(fd, &st)) {
        failure = errno;
        close(fd);
        errno = failure;
        return -1;
    }
    if (!S_ISREG(st.st_mode)) {
        close(fd);
        errno = EINVAL;
        return -1;
    }
    if (fd < 3) {
        moved = fcntl(fd, F_DUPFD_CLOEXEC, 3);
        failure = errno;
        close(fd);
        errno = failure;
        fd = moved;
    }
    return fd;
}
maage_status maage_process_start(const char *exe, const char *const argv[], const char *directory,
                                 const char *stdout_path, const char *stderr_path, maage_process **process, char error[256], maage_status_trace *trace) {
    int pipefd[2] = {-1, -1}, input = -1, output = -1, errors = -1, flags, failure, ok = 0;
    long fd, maximum = sysconf(_SC_OPEN_MAX);
    maage_process *p;
    maage_status result;
    if (process) *process = NULL;
    if (!exe || !argv || !argv[0] || !directory || !stdout_path || !stderr_path || !process || !error)
        return MAAGE_STATUS_TRACE(trace, maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_PROCESS, MAAGE_OPERATION_START, MAAGE_DETAIL_ARGUMENT_UNKNOWN), MAAGE_CONTEXT_PROCESS, MAAGE_OPERATION_START);
    error[0] = 0;
    p = (maage_process *) calloc(1, sizeof(*p));
    *process = NULL;
    if (!p || !argv || !argv[0]) goto done;
    output = maage_process_capture(stdout_path);
    if (output < 0) goto done;
    errors = !strcmp(stdout_path, stderr_path) ? output : maage_process_capture(stderr_path);
    if (errors < 0) goto done;
    input = open("/dev/null", O_RDONLY);
    if (input < 0 || maximum < 3 || maximum > INT_MAX || pipe(pipefd) || !maage_process_cloexec(pipefd[0]) || !maage_process_cloexec(pipefd[1]) ||
        !maage_process_cloexec(input)) goto done;
    if (input < 3) {
        flags = fcntl(input, F_DUPFD_CLOEXEC, 3);
        close(input);
        input = flags;
        if (input < 0) goto done;
    }
    if (pipefd[0] < 3) {
        flags = fcntl(pipefd[0], F_DUPFD_CLOEXEC, 3);
        close(pipefd[0]);
        pipefd[0] = flags;
        if (flags < 0) goto done;
    }
    if (pipefd[1] < 3) {
        flags = fcntl(pipefd[1], F_DUPFD_CLOEXEC, 3);
        close(pipefd[1]);
        pipefd[1] = flags;
        if (flags < 0) goto done;
    }
    flags = fcntl(pipefd[0], F_GETFL, 0);
    if (flags < 0 || fcntl(pipefd[0], F_SETFL, flags | O_NONBLOCK)) goto done;
    p->pid = fork();
    if (p->pid < 0) goto done;
    if (!p->pid) {
        /* Only async-signal-safe calls between fork and exec in a threaded app. */
        close(pipefd[0]);
        if (setsid() < 0 || chdir(directory) || dup2(input, STDIN_FILENO) < 0 || dup2(output, STDOUT_FILENO) < 0 || dup2(errors, STDERR_FILENO) < 0) goto child_failed;
        for (fd = 3; fd < maximum; ++fd)
            if (fd != pipefd[1]) close((int) fd);
        execve(exe, (char *const *) argv, environ);
    child_failed:
        failure = errno;
        while (write(pipefd[1], &failure, sizeof(failure)) < 0 && errno == EINTR) {
        }
        _exit(127);
    }
    close(pipefd[1]);
    pipefd[1] = -1;
    p->pipe = pipefd[0];
    pipefd[0] = -1;
    p->state = MAAGE_PROCESS_STARTING;
    *process = p;
    ok = 1;
done:
    failure = errno;
    if (pipefd[0] >= 0) close(pipefd[0]);
    if (pipefd[1] >= 0) close(pipefd[1]);
    if (input >= 0) close(input);
    if (output >= 0) close(output);
    if (errors >= 0 && errors != output) close(errors);
    if (!ok) {
        free(p);
        sprintf(error, "Cannot create process (OS error %d).", failure);
    }
    result = ok ? maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_START, 0) : maage_status_from_errno(failure, MAAGE_CONTEXT_PROCESS, MAAGE_OPERATION_START);
    if (!ok && !maage_status_failed(result)) result = maage_status_make(MAAGE_STATUS_ERROR, MAAGE_CONTEXT_PROCESS, MAAGE_OPERATION_START, 0);
    return maage_status_is_success(result) ? result : maage_status_trace_record(trace, result, MAAGE_CONTEXT_PROCESS, MAAGE_OPERATION_START, __FILE__, (unsigned long) __LINE__, failure);
}
static void maage_process_handshake(maage_process *p) {
    ssize_t n;
    if (p->pipe < 0) return;
    n = read(p->pipe, &p->failure, sizeof(p->failure));
    if (n == 0 || n == (ssize_t) sizeof(p->failure)) {
        close(p->pipe);
        p->pipe = -1;
        if (!p->failure) p->state = MAAGE_PROCESS_RUNNING;
    } else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
        p->failure = errno;
        close(p->pipe);
        p->pipe = -1;
    }
}
maage_status maage_process_poll(maage_process *p, int *code, maage_process_state *state,
                                char error[256], maage_status_trace *trace) {
    int status;
    pid_t result;
    maage_status outcome;
    if (code) *code = 0;
    if (state) *state = MAAGE_PROCESS_FAILED;
    if (!p || !code || !state || !error) return MAAGE_STATUS_TRACE(trace,
                                                                   maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_PROCESS, MAAGE_OPERATION_POLL,
                                                                                     MAAGE_DETAIL_ARGUMENT_UNKNOWN),
                                                                   MAAGE_CONTEXT_PROCESS, MAAGE_OPERATION_POLL);
    error[0] = 0;
    maage_process_handshake(p);
    if (p->pid > 0) {
        result = waitpid(p->pid, &status, WNOHANG);
        if (result == p->pid) {
            /* The child may have written its exec error after the first read. */
            do {
                maage_process_handshake(p);
            } while (p->pipe >= 0 && errno == EINTR);
            p->pid = 0;
            p->code = WIFEXITED(status) ? WEXITSTATUS(status) : WIFSIGNALED(status) ? 128 + WTERMSIG(status)
                                                                                    : -1;
            p->state = p->failure ? MAAGE_PROCESS_FAILED : MAAGE_PROCESS_EXITED;
        } else if (result < 0 && errno != EINTR) {
            p->failure = errno;
            p->pid = 0;
            p->state = MAAGE_PROCESS_FAILED;
        }
    }
    if (p->failure) sprintf(error, "Client startup/monitoring failed (OS error %d).", p->failure);
    *code = p->code;
    *state = p->state;
    outcome = p->failure ? maage_status_from_errno(p->failure, MAAGE_CONTEXT_PROCESS, MAAGE_OPERATION_POLL) : maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_POLL, 0);
    return maage_status_is_success(outcome) ? outcome : maage_status_trace_record(trace, outcome, MAAGE_CONTEXT_PROCESS, MAAGE_OPERATION_POLL, __FILE__, (unsigned long) __LINE__, p->failure);
}
static void *maage_process_reap(void *context) {
    maage_process *p = (maage_process *) context;
    while (waitpid(p->pid, NULL, 0) < 0 && errno == EINTR) {
    }
    free(p);
    return NULL;
}
maage_status maage_process_close(maage_process *p) {
    pthread_t thread;
    pid_t result;
    int native;
    if (!p) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_CLOSE, 0);
    if (p->pipe >= 0) close(p->pipe);
    result = 0;
    if (p->pid > 0) do {
            result = waitpid(p->pid, NULL, WNOHANG);
        } while (result < 0 && errno == EINTR);
    if (p->pid > 0 && result == 0) {
        native = pthread_create(&thread, NULL, maage_process_reap, p);
        if (!native) {
            native = pthread_detach(thread);
            return maage_status_make(native ? MAAGE_STATUS_PARTIAL : MAAGE_STATUS_SUCCESS,
                                     MAAGE_CONTEXT_PROCESS, MAAGE_OPERATION_CLOSE, 0);
        }
        free(p);
        return maage_status_make(MAAGE_STATUS_PARTIAL, MAAGE_CONTEXT_PROCESS, MAAGE_OPERATION_CLOSE, 0);
    }
    free(p);
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_CLOSE, 0);
}
#endif
