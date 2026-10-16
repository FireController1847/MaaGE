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
    return handle && handle != INVALID_HANDLE_VALUE && GetFileType(handle) != FILE_TYPE_UNKNOWN;
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

/* MoveFileW never replaces an existing destination. */
static int maage_log_archive(const char *path, const char *archive)
{
    wchar_t *source = maage_log_wide_path(path), *destination = maage_log_wide_path(archive);
    DWORD error;
    int result;
    if (!source || !destination) { free(source); free(destination); return -1; }
    result = MoveFileW(source, destination);
    error = result ? ERROR_SUCCESS : GetLastError();
    free(source); free(destination);
    if (result || error == ERROR_FILE_NOT_FOUND) return 1;
    return error == ERROR_ALREADY_EXISTS || error == ERROR_FILE_EXISTS ? 0 : -1;
}

FILE *maage_log_open(const char *path, const char *mode)
{
    wchar_t *wide = maage_log_wide_path(path);
    FILE *file;
    if (!wide) return NULL;
    file = _wfopen(wide, mode[0] == 'a' ? L"a" : L"r");
    free(wide);
    return file;
}
#else
#include <pthread.h>
#include <unistd.h>

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
static int maage_log_archive(const char *path, const char *archive)
{
    /* link() reserves the archive name atomically without overwriting a file. */
    if (link(path, archive) == 0) return unlink(path) == 0 ? 1 : -1;
    if (errno == ENOENT) return 1;
    return errno == EEXIST ? 0 : -1;
}
FILE *maage_log_open(const char *path, const char *mode) { return fopen(path, mode); }
#endif


int maage_log_rotate(const char *path)
{
    char archive[MAAGE_LOG_PATH_SIZE + 64], timestamp[32];
    const char *extension, *separator;
    size_t stem, index;
    unsigned int sequence;
    int result;
    maage_log_timestamp(timestamp, sizeof(timestamp));
    if (!timestamp[0]) return 0;
    for (index = 0; timestamp[index]; ++index) {
        if (timestamp[index] == ' ') timestamp[index] = '_';
        else if (timestamp[index] == ':') timestamp[index] = '-';
    }
    extension = strrchr(path, '.');
    separator = strrchr(path, '/');
#ifdef _WIN32
    {
        const char *backslash = strrchr(path, '\\');
        if (backslash && (!separator || backslash > separator)) separator = backslash;
    }
#endif
    if (!extension || (separator && extension < separator)) extension = path + strlen(path);
    stem = (size_t)(extension - path);
    for (sequence = 0; sequence <= 99999; ++sequence) {
        if (sequence == 0)
            sprintf(archive, "%.*s.%s%s", (int)stem, path, timestamp, extension);
        else
            sprintf(archive, "%.*s.%s.%u%s", (int)stem, path, timestamp, sequence, extension);
        result = maage_log_archive(path, archive);
        if (result != 0) return result > 0;
    }
    return 0;
}
