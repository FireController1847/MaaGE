#ifdef __APPLE__
#define _DARWIN_C_SOURCE 1
#endif
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include "log_internal.h"
#include <time.h>
#include <errno.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdlib.h>
#include <io.h>
#include <fcntl.h>

static CRITICAL_SECTION maage_log_mutex;
static volatile LONG maage_log_mutex_ready;

void maage_log_lock(void)
{
    if (InterlockedCompareExchange(&maage_log_mutex_ready, 1, 0) == 0) {
        InitializeCriticalSection(&maage_log_mutex);
        InterlockedExchange(&maage_log_mutex_ready, 2);
    } else {
        while (InterlockedCompareExchange(&maage_log_mutex_ready, 2, 2) != 2) Sleep(0);
    }
    EnterCriticalSection(&maage_log_mutex);
}

void maage_log_unlock(void) { LeaveCriticalSection(&maage_log_mutex); }

int maage_log_format(char *buffer, size_t capacity, const char *format, va_list args)
{
#ifdef _MSC_VER
    return _vsnprintf_s(buffer, capacity, _TRUNCATE, format, args);
#else
    return vsnprintf(buffer, capacity, format, args);
#endif
}

void maage_log_timestamp(char *buffer, size_t capacity)
{
    time_t now = time(NULL);
    struct tm local;
    buffer[0] = '\0';
    if (localtime_s(&local, &now) == 0)
        strftime(buffer, capacity, "%Y-%m-%d %H:%M:%S", &local);
}

int maage_log_console_available(int error)
{
    HANDLE handle = GetStdHandle(error ? STD_ERROR_HANDLE : STD_OUTPUT_HANDLE);
    return _fileno(error ? stderr : stdout) >= 0 &&
        handle && handle != INVALID_HANDLE_VALUE && GetFileType(handle) != FILE_TYPE_UNKNOWN;
}

void maage_log_platform_diagnostic(const char *message)
{
    OutputDebugStringA("MaaGE logging: ");
    OutputDebugStringA(message);
    OutputDebugStringA("\n");
}

static wchar_t *maage_log_wide_path(const char *path)
{
    int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, NULL, 0);
    wchar_t *wide;
    if (!count) return NULL;
    wide = (wchar_t *)malloc((size_t)count * sizeof(*wide));
    if (!wide) return NULL;
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, wide, count)) {
        free(wide);
        return NULL;
    }
    return wide;
}

int maage_log_archive_time(const char *path, int closing, char timestamp[32])
{
    wchar_t *wide = maage_log_wide_path(path);
    WIN32_FILE_ATTRIBUTE_DATA info;
    FILETIME local;
    SYSTEMTIME parts;
    HANDLE handle;
    DWORD error;
    if (!wide) return -1;
    if (!GetFileAttributesExW(wide, GetFileExInfoStandard, &info)) {
        error = GetLastError(); free(wide);
        return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND ? 0 : -1;
    }
    if (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) { free(wide); return -1; }
    if (!info.nFileSizeHigh && !info.nFileSizeLow) { free(wide); return 0; }
    if (closing) {
        GetSystemTimeAsFileTime(&info.ftLastWriteTime);
        handle = CreateFileW(wide, FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (handle == INVALID_HANDLE_VALUE) { free(wide); return -1; }
        if (!SetFileTime(handle, NULL, NULL, &info.ftLastWriteTime)) { CloseHandle(handle); free(wide); return -1; }
        CloseHandle(handle);
    }
    free(wide);
    if (!FileTimeToLocalFileTime(&info.ftLastWriteTime, &local) || !FileTimeToSystemTime(&local, &parts)) return -1;
    sprintf(timestamp, "%04u-%02u-%02u_%02u-%02u-%02u", (unsigned)parts.wYear, (unsigned)parts.wMonth,
        (unsigned)parts.wDay, (unsigned)parts.wHour, (unsigned)parts.wMinute, (unsigned)parts.wSecond);
    return 1;
}

/* MoveFileW never replaces an existing destination. */
int maage_log_archive_publish(const char *path, const char *archive)
{
    wchar_t *source = maage_log_wide_path(path), *destination = maage_log_wide_path(archive);
    DWORD error;
    int result;
    if (!source || !destination) { free(source); free(destination); return -1; }
    result = MoveFileW(source, destination);
    error = result ? ERROR_SUCCESS : GetLastError();
    free(source); free(destination);
    if (result) return 1;
    return error == ERROR_ALREADY_EXISTS || error == ERROR_FILE_EXISTS ? 0 : -1;
}

FILE *maage_log_open(const char *path, const char *mode)
{
    wchar_t *wide = maage_log_wide_path(path);
    FILE *file = NULL;
    HANDLE handle;
    int descriptor, writing = mode[0] == 'a' || mode[0] == 'w';
    if (!wide) return NULL;
    handle = CreateFileW(wide, writing ? (mode[0] == 'w' ? GENERIC_WRITE : FILE_APPEND_DATA) : GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
        mode[0] == 'w' ? CREATE_ALWAYS : writing ? OPEN_ALWAYS : OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (handle != INVALID_HANDLE_VALUE) {
        descriptor = _open_osfhandle((INT_PTR)handle, _O_BINARY | (writing ? _O_APPEND : _O_RDONLY));
        if (descriptor < 0) CloseHandle(handle);
        else {
            file = _fdopen(descriptor, writing ? "ab" : "rb");
            if (!file) _close(descriptor);
        }
    } else {
        DWORD error = GetLastError();
        errno = error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND ? ENOENT : EIO;
    }
    free(wide);
    return file;
}

FILE *maage_log_archive_create(const char *path)
{
    wchar_t *wide = maage_log_wide_path(path);
    HANDLE handle;
    FILE *file = NULL;
    DWORD error;
    int descriptor;
    if (!wide) { errno = EINVAL; return NULL; }
    handle = CreateFileW(wide, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (handle == INVALID_HANDLE_VALUE) {
        error = GetLastError(); free(wide);
        errno = error == ERROR_FILE_EXISTS || error == ERROR_ALREADY_EXISTS ? EEXIST : EIO;
        return NULL;
    }
    descriptor = _open_osfhandle((INT_PTR)handle, _O_BINARY);
    if (descriptor < 0) CloseHandle(handle);
    else { file = _fdopen(descriptor, "wb"); if (!file) _close(descriptor); }
    if (!file) DeleteFileW(wide);
    free(wide);
    return file;
}
int maage_log_archive_remove(const char *path)
{
    wchar_t *wide = maage_log_wide_path(path);
    DWORD error;
    int ok;
    if (!wide) return 0;
    ok = DeleteFileW(wide); error = GetLastError(); free(wide);
    return ok || error == ERROR_FILE_NOT_FOUND;
}
int maage_log_archive_sync(FILE *file)
{ return fflush(file) == 0 && _commit(_fileno(file)) == 0; }

int maage_log_capture_stdio(FILE *output)
{
    FILE *streams[2];
    HANDLE original[2], crt_handle;
    DWORD kinds[2];
    int descriptors[2], saved[2], native_matches[2];
    int i, rebound = 0, ok = 0, failure;
    streams[0] = stdout; streams[1] = stderr;
    kinds[0] = STD_OUTPUT_HANDLE; kinds[1] = STD_ERROR_HANDLE;
    for (i = 0; i < 2; ++i) {
        original[i] = GetStdHandle(kinds[i]);
        descriptors[i] = _fileno(streams[i]); saved[i] = -1;
        crt_handle = descriptors[i] < 0 ? INVALID_HANDLE_VALUE : (HANDLE)_get_osfhandle(descriptors[i]);
        native_matches[i] = original[i] != INVALID_HANDLE_VALUE && original[i] == crt_handle;
    }
    fflush(stdout); fflush(stderr); fflush(output);
    for (i = 0; i < 2; ++i) {
        /* GUI-subsystem processes may have a FILE with no CRT descriptor. */
        if (descriptors[i] < 0) {
            if (!_wfreopen(L"NUL", L"w", streams[i])) goto done;
            setvbuf(streams[i], NULL, _IONBF, 0);
            descriptors[i] = _fileno(streams[i]);
        }
        saved[i] = _dup(descriptors[i]);
        if (saved[i] < 0) goto done;
    }
    for (i = 0; i < 2; ++i) {
        if (_dup2(_fileno(output), descriptors[i])) goto done;
        ++rebound;
    }
    for (i = 0; i < 2; ++i)
        if (!SetStdHandle(kinds[i], (HANDLE)_get_osfhandle(descriptors[i]))) goto done;
    clearerr(stdout); clearerr(stderr);
    ok = 1;
done:
    failure = errno;
    if (!ok) {
        for (i = 0; i < rebound; ++i)
            if (_dup2(saved[i], descriptors[i]))
                maage_log_platform_diagnostic("Cannot restore a standard stream after capture failure.");
        for (i = 0; i < 2; ++i)
            if (!SetStdHandle(kinds[i], native_matches[i] ? (HANDLE)_get_osfhandle(descriptors[i]) : original[i]))
                maage_log_platform_diagnostic("Cannot restore a native standard handle after capture failure.");
    }
    for (i = 0; i < 2; ++i) if (saved[i] >= 0) _close(saved[i]);
    errno = failure;
    return ok;
}
#else
#include <pthread.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <utime.h>

static pthread_mutex_t maage_log_mutex = PTHREAD_MUTEX_INITIALIZER;
void maage_log_lock(void) { pthread_mutex_lock(&maage_log_mutex); }
void maage_log_unlock(void) { pthread_mutex_unlock(&maage_log_mutex); }

int maage_log_format(char *buffer, size_t capacity, const char *format, va_list args)
{
    return vsnprintf(buffer, capacity, format, args);
}

void maage_log_timestamp(char *buffer, size_t capacity)
{
    time_t now = time(NULL);
    struct tm local;
    buffer[0] = '\0';
    if (localtime_r(&now, &local)) strftime(buffer, capacity, "%Y-%m-%d %H:%M:%S", &local);
}

int maage_log_console_available(int error) { (void)error; return 1; }
void maage_log_platform_diagnostic(const char *message) { (void)message; }
int maage_log_archive_time(const char *path, int closing, char timestamp[32])
{
    struct stat info;
    struct utimbuf times;
    struct tm local;
    if (lstat(path, &info)) return errno == ENOENT ? 0 : -1;
    if (!S_ISREG(info.st_mode)) return -1;
    if (!info.st_size) return 0;
    if (closing) {
        times.actime = info.st_atime; times.modtime = time(NULL);
        if (times.modtime == (time_t)-1 || utime(path, &times)) return -1;
        info.st_mtime = times.modtime;
    }
    if (!localtime_r(&info.st_mtime, &local) ||
        !strftime(timestamp, 32, "%Y-%m-%d_%H-%M-%S", &local)) return -1;
    return 1;
}
int maage_log_archive_publish(const char *path, const char *archive)
{
    /* link() reserves the archive name atomically without overwriting a file. */
    if (link(path, archive) == 0) return unlink(path) == 0 ? 1 : -1;
    return errno == EEXIST ? 0 : -1;
}
FILE *maage_log_open(const char *path, const char *mode)
{
    FILE *file;
    int descriptor;
    if (mode[0] != 'w') return fopen(path, mode);
    descriptor = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_APPEND | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (descriptor < 0) return NULL;
    file = fdopen(descriptor, "ab");
    if (!file) close(descriptor);
    return file;
}
FILE *maage_log_archive_create(const char *path)
{
    int descriptor = open(path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    FILE *file;
    if (descriptor < 0) return NULL;
    file = fdopen(descriptor, "wb");
    if (!file) { close(descriptor); unlink(path); }
    return file;
}
int maage_log_archive_remove(const char *path)
{ return !unlink(path) || errno == ENOENT; }
int maage_log_archive_sync(FILE *file)
{ return fflush(file) == 0 && fsync(fileno(file)) == 0; }

int maage_log_capture_stdio(FILE *output)
{
    int saved_out = -1, saved_error = -1, rebound = 0, ok = 0, failure;
    fflush(stdout); fflush(stderr); fflush(output);
    saved_out = dup(STDOUT_FILENO);
    if (saved_out < 0) goto done;
    saved_error = dup(STDERR_FILENO);
    if (saved_error < 0) goto done;
    if (dup2(fileno(output), STDOUT_FILENO) < 0) goto done;
    rebound = 1;
    if (dup2(fileno(output), STDERR_FILENO) < 0) goto done;
    clearerr(stdout); clearerr(stderr);
    ok = 1;
done:
    failure = errno;
    if (!ok && rebound && dup2(saved_out, STDOUT_FILENO) < 0)
        maage_log_platform_diagnostic("Cannot restore stdout after capture failure.");
    if (saved_out >= 0) close(saved_out);
    if (saved_error >= 0) close(saved_error);
    errno = failure;
    return ok;
}
#endif
