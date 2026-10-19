#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include "maage/process.h"
#include "maage/storage.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#ifdef _WIN32
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <io.h>
struct maage_process { HANDLE handle; maage_process_state state; int code; };
static wchar_t *maage_process_wide(const char *text)
{
    int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text,-1,NULL,0);
    wchar_t *w=n ? (wchar_t *)malloc((size_t)n*sizeof(*w)) : NULL;
    if(w && !MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text,-1,w,n)) { free(w); return NULL; }
    return w;
}
/* Microsoft CRT command-line quoting, including trailing backslashes. */
static wchar_t *maage_process_arguments(const char *const argv[])
{
    size_t total=1, i, n, slash, j;
    wchar_t *line, *out, *arg, *p;
    for(i=0;argv[i];++i) { total+=strlen(argv[i])*2+4; if(total>32767) return NULL; }
    line=(wchar_t *)malloc(total*sizeof(*line)); if(!line) return NULL;
    out=line;
    for(i=0;argv[i];++i) {
        arg=maage_process_wide(argv[i]); if(!arg) { free(line); return NULL; }
        if(i) *out++=L' ';
        *out++=L'"'; p=arg;
        while(*p) {
            slash=0; while(*p==L'\\') { ++slash; ++p; }
            n=(*p==L'"' || !*p) ? slash*2 : slash;
            for(j=0;j<n;++j) *out++=L'\\';
            if(*p==L'"') *out++=L'\\';
            if(*p) *out++=*p++;
        }
        *out++=L'"'; free(arg);
    }
    *out=0; return line;
}
int maage_process_start(const char *exe, const char *const argv[], const char *directory,
    const char *stdout_path, const char *stderr_path, maage_process **process, char error[256])
{
    STARTUPINFOEXW startup; PROCESS_INFORMATION info;
    HANDLE handles[3]; FILE *out=NULL, *err=NULL;
    SECURITY_ATTRIBUTES security;
    wchar_t *native=NULL, *cwd=NULL, *command=NULL;
    SIZE_T size=0; int initialized=0, ok=0; DWORD failure=0;
    maage_process *p=(maage_process *)calloc(1,sizeof(*p));
    *process=NULL; memset(&startup,0,sizeof(startup)); memset(&info,0,sizeof(info));
    memset(handles,0,sizeof(handles)); memset(&security,0,sizeof(security));
    security.nLength=sizeof(security); security.bInheritHandle=TRUE;
    if(!p || !argv || !argv[0]) goto done;
    native=maage_process_wide(exe); cwd=maage_process_wide(directory); command=maage_process_arguments(argv);
    out=maage_storage_open(stdout_path,"wb"); err=maage_storage_open(stderr_path,"wb");
    if(!native || !cwd || !command || !out || !err) goto done;
    handles[0]=CreateFileW(L"NUL",GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,&security,OPEN_EXISTING,0,NULL);
    handles[1]=(HANDLE)_get_osfhandle(_fileno(out)); handles[2]=(HANDLE)_get_osfhandle(_fileno(err));
    if(handles[0]==INVALID_HANDLE_VALUE || !SetHandleInformation(handles[1],HANDLE_FLAG_INHERIT,HANDLE_FLAG_INHERIT) ||
        !SetHandleInformation(handles[2],HANDLE_FLAG_INHERIT,HANDLE_FLAG_INHERIT)) goto done;
    InitializeProcThreadAttributeList(NULL,1,0,&size);
    startup.lpAttributeList=(LPPROC_THREAD_ATTRIBUTE_LIST)malloc(size); if(!startup.lpAttributeList) goto done;
    if(!InitializeProcThreadAttributeList(startup.lpAttributeList,1,0,&size)) goto done;
    initialized=1;
    if(!UpdateProcThreadAttribute(startup.lpAttributeList,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,handles,sizeof(handles),NULL,NULL)) goto done;
    startup.StartupInfo.cb=sizeof(startup); startup.StartupInfo.dwFlags=STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput=handles[0]; startup.StartupInfo.hStdOutput=handles[1]; startup.StartupInfo.hStdError=handles[2];
    if(!CreateProcessW(native,command,NULL,NULL,TRUE,EXTENDED_STARTUPINFO_PRESENT|CREATE_NEW_PROCESS_GROUP|DETACHED_PROCESS,
        NULL,cwd,&startup.StartupInfo,&info)) goto done;
    CloseHandle(info.hThread); p->handle=info.hProcess; p->state=MAAGE_PROCESS_RUNNING; *process=p; ok=1;
 done:
    failure=GetLastError();
    if(initialized) DeleteProcThreadAttributeList(startup.lpAttributeList);
    free(startup.lpAttributeList); free(native); free(cwd); free(command);
    if(handles[0] && handles[0]!=INVALID_HANDLE_VALUE) CloseHandle(handles[0]);
    if(out) fclose(out); if(err) fclose(err);
    if(!ok) { free(p); sprintf(error,"Cannot create Client process (Windows error %lu).",(unsigned long)failure); }
    return ok;
}
maage_process_state maage_process_poll(maage_process *p, int *code, char error[256])
{
    DWORD status=WaitForSingleObject(p->handle,0), exit;
    if(status==WAIT_OBJECT_0 && p->state==MAAGE_PROCESS_RUNNING) {
        if(GetExitCodeProcess(p->handle,&exit)) { p->state=MAAGE_PROCESS_EXITED; p->code=(int)exit; }
        else { p->state=MAAGE_PROCESS_FAILED; sprintf(error,"Cannot read process exit status (Windows error %lu).",(unsigned long)GetLastError()); }
    } else if(status==WAIT_FAILED) { p->state=MAAGE_PROCESS_FAILED; sprintf(error,"Cannot monitor process (Windows error %lu).",(unsigned long)GetLastError()); }
    *code=p->code; return p->state;
}
void maage_process_close(maage_process *p) { if(p) { CloseHandle(p->handle); free(p); } }
#else
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <errno.h>
#include <pthread.h>
#include <limits.h>
extern char **environ;
struct maage_process { pid_t pid; int pipe, failure, code; maage_process_state state; };
static int maage_process_cloexec(int fd)
{ int flags=fcntl(fd,F_GETFD,0); return flags>=0 && fcntl(fd,F_SETFD,flags|FD_CLOEXEC)==0; }
int maage_process_start(const char *exe, const char *const argv[], const char *directory,
    const char *stdout_path, const char *stderr_path, maage_process **process, char error[256])
{
    int pipefd[2]={-1,-1}, input=-1, output=-1, errors=-1, flags, failure, ok=0;
    long fd, maximum=sysconf(_SC_OPEN_MAX);
    FILE *out=NULL, *err=NULL;
    maage_process *p=(maage_process *)calloc(1,sizeof(*p));
    *process=NULL;
    if(!p || !argv || !argv[0]) goto done;
    out=maage_storage_open(stdout_path,"wb"); err=maage_storage_open(stderr_path,"wb"); input=open("/dev/null",O_RDONLY);
    if(!out || !err || input<0 || maximum<3 || maximum>INT_MAX || pipe(pipefd) || !maage_process_cloexec(pipefd[0]) || !maage_process_cloexec(pipefd[1]) ||
        !maage_process_cloexec(fileno(out)) || !maage_process_cloexec(fileno(err)) || !maage_process_cloexec(input)) goto done;
    output=fcntl(fileno(out),F_DUPFD_CLOEXEC,3); errors=fcntl(fileno(err),F_DUPFD_CLOEXEC,3);
    if(output<0 || errors<0) goto done;
    if(input<3) { flags=fcntl(input,F_DUPFD_CLOEXEC,3); close(input); input=flags; if(input<0) goto done; }
    if(pipefd[0]<3) { flags=fcntl(pipefd[0],F_DUPFD_CLOEXEC,3); close(pipefd[0]); pipefd[0]=flags; if(flags<0) goto done; }
    if(pipefd[1]<3) { flags=fcntl(pipefd[1],F_DUPFD_CLOEXEC,3); close(pipefd[1]); pipefd[1]=flags; if(flags<0) goto done; }
    flags=fcntl(pipefd[0],F_GETFL,0); if(flags<0 || fcntl(pipefd[0],F_SETFL,flags|O_NONBLOCK)) goto done;
    p->pid=fork();
    if(p->pid<0) goto done;
    if(!p->pid) {
        /* Only async-signal-safe calls between fork and exec in a threaded app. */
        close(pipefd[0]);
        if(setsid()<0 || chdir(directory) || dup2(input,STDIN_FILENO)<0 || dup2(output,STDOUT_FILENO)<0 || dup2(errors,STDERR_FILENO)<0) goto child_failed;
        for(fd=3;fd<maximum;++fd) if(fd!=pipefd[1]) close((int)fd);
        execve(exe,(char *const *)argv,environ);
 child_failed:
        failure=errno; while(write(pipefd[1],&failure,sizeof(failure))<0 && errno==EINTR) { }
        _exit(127);
    }
    close(pipefd[1]); pipefd[1]=-1; p->pipe=pipefd[0]; pipefd[0]=-1;
    p->state=MAAGE_PROCESS_STARTING; *process=p; ok=1;
 done:
    failure=errno;
    if(pipefd[0]>=0) close(pipefd[0]); if(pipefd[1]>=0) close(pipefd[1]); if(input>=0) close(input);
    if(output>=0) close(output); if(errors>=0) close(errors);
    if(out) fclose(out); if(err) fclose(err);
    if(!ok) { free(p); sprintf(error,"Cannot create process (OS error %d).",failure); }
    return ok;
}
static void maage_process_handshake(maage_process *p)
{
    ssize_t n;
    if(p->pipe<0) return;
    n=read(p->pipe,&p->failure,sizeof(p->failure));
    if(n==0 || n==(ssize_t)sizeof(p->failure)) {
        close(p->pipe); p->pipe=-1;
        if(!p->failure) p->state=MAAGE_PROCESS_RUNNING;
    } else if(n<0 && errno!=EAGAIN && errno!=EWOULDBLOCK && errno!=EINTR) {
        p->failure=errno; close(p->pipe); p->pipe=-1;
    }
}
maage_process_state maage_process_poll(maage_process *p, int *code, char error[256])
{
    int status;
    pid_t result;
    maage_process_handshake(p);
    if(p->pid>0) {
        result=waitpid(p->pid,&status,WNOHANG);
        if(result==p->pid) {
            /* The child may have written its exec error after the first read. */
            do { maage_process_handshake(p); } while(p->pipe>=0 && errno==EINTR);
            p->pid=0;
            p->code=WIFEXITED(status) ? WEXITSTATUS(status) : WIFSIGNALED(status) ? 128+WTERMSIG(status) : -1;
            p->state=p->failure ? MAAGE_PROCESS_FAILED : MAAGE_PROCESS_EXITED;
        } else if(result<0 && errno!=EINTR) { p->failure=errno; p->pid=0; p->state=MAAGE_PROCESS_FAILED; }
    }
    if(p->failure) sprintf(error,"Client startup/monitoring failed (OS error %d).",p->failure);
    *code=p->code; return p->state;
}
static void *maage_process_reap(void *context)
{
    maage_process *p=(maage_process *)context;
    while(waitpid(p->pid,NULL,0)<0 && errno==EINTR) { }
    free(p); return NULL;
}
void maage_process_close(maage_process *p)
{
    pthread_t thread;
    pid_t result;
    if(!p) return;
    if(p->pipe>=0) close(p->pipe);
    result=0;
    if(p->pid>0) do { result=waitpid(p->pid,NULL,WNOHANG); } while(result<0 && errno==EINTR);
    if(p->pid>0 && result==0) {
        if(!pthread_create(&thread,NULL,maage_process_reap,p)) { pthread_detach(thread); return; }
    }
    free(p);
}
#endif
