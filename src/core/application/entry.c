#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include "maage/application.h"
#include "maage/log.h"
#include "cmakecfg.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#if MAAGE_PLATFORM_WINDOWS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <direct.h>
#include <wchar.h>
#include <shellapi.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#include <limits.h>
#if MAAGE_PLATFORM_MACOS
#include <mach-o/dyld.h>
#endif
#endif

#ifdef MAAGE_APPLICATION_USER_DATA
static char *maage_application_log_path(void)
{
    char *path;
#if MAAGE_PLATFORM_WINDOWS
    const wchar_t *base = _wgetenv(L"APPDATA");
    wchar_t *directory;
    int bytes;
    if (!base || !*base) return NULL;
    directory = (wchar_t *)malloc((wcslen(base) + sizeof("\\.maage\\logs\\" MAAGE_APPLICATION_KEY ".log")) * sizeof(*directory));
    if (!directory) return NULL;
    wcscpy(directory, base);
    wcscat(directory, L"\\.maage");
    if (_wmkdir(directory) != 0 && errno != EEXIST) { free(directory); return NULL; }
    wcscat(directory, L"\\logs");
    if (_wmkdir(directory) != 0 && errno != EEXIST) { free(directory); return NULL; }
    wcscat(directory, L"\\");
    {
        const char *key = MAAGE_APPLICATION_KEY;
        size_t index = wcslen(directory);
        while (*key) directory[index++] = (wchar_t)(unsigned char)*key++;
        directory[index] = L'\0';
    }
    wcscat(directory, L".log");
    bytes = WideCharToMultiByte(CP_UTF8, 0, directory, -1, NULL, 0, NULL, NULL);
    path = bytes ? (char *)malloc((size_t)bytes) : NULL;
    if (path && !WideCharToMultiByte(CP_UTF8, 0, directory, -1, path, bytes, NULL, NULL)) {
        free(path); path = NULL;
    }
    free(directory);
#else
    const char *base = getenv("HOME");
    char *cursor;
#if MAAGE_PLATFORM_MACOS
    const char *suffix = "/Library/Application Support/.maage/logs";
#else
    const char *suffix = "/.maage/logs";
#endif
    if (!base || base[0] != '/') return NULL;
    path = (char *)malloc(strlen(base) + strlen(suffix) + sizeof("/" MAAGE_APPLICATION_KEY ".log"));
    if (!path) return NULL;
    strcpy(path, base); strcat(path, suffix);
    for (cursor = path + 1; ; ++cursor) {
        if (*cursor == '/' || !*cursor) {
            char saved = *cursor;
            *cursor = '\0';
            if (mkdir(path, 0700) != 0 && errno != EEXIST) { free(path); return NULL; }
            *cursor = saved;
            if (!saved) break;
        }
    }
    strcat(path, "/" MAAGE_APPLICATION_KEY ".log");
#endif
    return path;
}
#else
/* Resolve the application directory without changing the process working directory. */
static char *maage_application_log_path(void)
{
    char *path;
#if MAAGE_PLATFORM_WINDOWS
    wchar_t *native, *end;
    DWORD count, capacity = 32768;
    int bytes;
    native = (wchar_t *)malloc((size_t)capacity * sizeof(*native));
    if (!native) return NULL;
    count = GetModuleFileNameW(NULL, native, capacity);
    if (!count || count >= capacity) { free(native); return NULL; }
    end = wcsrchr(native, L'\\');
    if (!end) { free(native); return NULL; }
    end[1] = L'\0';
    bytes = WideCharToMultiByte(CP_UTF8, 0, native, -1, NULL, 0, NULL, NULL);
    path = bytes ? (char *)malloc((size_t)bytes + sizeof("logs/") + sizeof(MAAGE_APPLICATION_KEY) + sizeof(".log")) : NULL;
    if (!path || !WideCharToMultiByte(CP_UTF8, 0, native, -1, path, bytes, NULL, NULL)) {
        free(native); free(path); return NULL;
    }
    free(native);
#else
    char executable[PATH_MAX], *separator;
#if MAAGE_PLATFORM_MACOS
    char unresolved[PATH_MAX];
    size_t length;
    uint32_t capacity = sizeof(unresolved);
    if (_NSGetExecutablePath(unresolved, &capacity) != 0 || !realpath(unresolved, executable)) return NULL;
#else
    ssize_t count = readlink("/proc/self/exe", executable, sizeof(executable) - 1);
    if (count <= 0 || (size_t)count >= sizeof(executable) - 1) return NULL;
    executable[count] = '\0';
#endif
    separator = strrchr(executable, '/');
    if (!separator) return NULL;
    separator[1] = '\0';
#if MAAGE_PLATFORM_MACOS
    /* Keep writable data outside the signed application bundle. */
    length = strlen(executable);
    if (length >= sizeof(".app/Contents/MacOS/") - 1 &&
        !strcmp(executable + length - (sizeof(".app/Contents/MacOS/") - 1), ".app/Contents/MacOS/")) {
        executable[length - (sizeof(".app/Contents/MacOS/") - 1)] = '\0';
        separator = strrchr(executable, '/');
        if (!separator) return NULL;
        separator[1] = '\0';
    }
#endif
    path = (char *)malloc(strlen(executable) + sizeof("logs/") + sizeof(MAAGE_APPLICATION_KEY) + sizeof(".log"));
    if (!path) return NULL;
    strcpy(path, executable);
#endif
    strcat(path, "logs");
#if MAAGE_PLATFORM_WINDOWS
    /* Convert the directory back to UTF-16 for non-ASCII installation paths. */
    {
        wchar_t *directory;
        int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, NULL, 0);
        directory = count ? (wchar_t *)malloc((size_t)count * sizeof(*directory)) : NULL;
        if (!directory) { free(path); return NULL; }
        if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, directory, count)) {
            free(directory); free(path); return NULL;
        }
        _wmkdir(directory);
        free(directory);
    }
#else
    mkdir(path, 0700);
#endif
    strcat(path, "/" MAAGE_APPLICATION_KEY ".log");
    return path;
}

#endif

static int maage_application_run(int argc, char *const argv[])
{
    maage_log_config config;
    int result;
    char *path = maage_application_log_path();
    maage_log_config_init(&config);
#ifdef MAAGE_APPLICATION_USER_DATA
    config.file = path != NULL;
    config.file_path = path;
#else
    config.file = 1;
    config.file_path = path ? path : "logs/" MAAGE_APPLICATION_KEY ".log";
#endif
    maage_log_init(&config, argc, argv);
#ifdef MAAGE_APPLICATION_USER_DATA
    if (!path) maage_log_error("maage.core.application", "Cannot resolve or create the per-user .maage/logs directory.");
#endif
    free(path);
    result = maage_application_main(argc, argv);
    maage_log_shutdown();
    return result;
}

#if MAAGE_PLATFORM_WINDOWS
int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous, LPSTR command, int show)
{
    wchar_t **wide;
    char **args = NULL;
    int count = 0, index, bytes, result;
    (void)instance; (void)previous; (void)command; (void)show;
    wide = CommandLineToArgvW(GetCommandLineW(), &count);
    if (wide) args = (char **)calloc((size_t)count + 1, sizeof(*args));
    if (args) {
        for (index = 0; index < count; ++index) {
            bytes = WideCharToMultiByte(CP_UTF8, 0, wide[index], -1, NULL, 0, NULL, NULL);
            if (!bytes) break;
            args[index] = (char *)malloc((size_t)bytes);
            if (!args[index]) break;
            if (!WideCharToMultiByte(CP_UTF8, 0, wide[index], -1, args[index], bytes, NULL, NULL)) break;
        }
        result = maage_application_run(index == count ? count : 0, index == count ? args : NULL);
        for (index = 0; index < count; ++index) free(args[index]);
        free(args);
    } else result = maage_application_run(0, NULL);
    if (wide) LocalFree(wide);
    return result;
}
#else
int main(int argc, char **argv)
{
    return maage_application_run(argc, argv);
}
#endif
