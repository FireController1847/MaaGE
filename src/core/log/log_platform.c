#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include "log_internal.h"
#include <time.h>

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

FILE *maage_log_open(const char *path, const char *mode)
{
    int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, NULL, 0);
    wchar_t *wide;
    FILE *file;
    if (!count) return NULL;
    wide = (wchar_t *)malloc((size_t)count * sizeof(*wide));
    if (!wide) return NULL;
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, wide, count)) {
        free(wide);
        return NULL;
    }
    file = _wfopen(wide, mode[0] == 'a' ? L"a" : L"r");
    free(wide);
    return file;
}
#else
#include <pthread.h>

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
FILE *maage_log_open(const char *path, const char *mode) { return fopen(path, mode); }
#endif
