#include "maage/application.h"
#include "maage/log.h"
#include "cmakecfg.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if MAAGE_PLATFORM_WINDOWS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <direct.h>
#include <wchar.h>
#include <shellapi.h>
#else
#include <sys/stat.h>
#endif

/* GUI applications append to separate per-user files by default, without a
 * console or working-directory assumption. INI and CLI settings may override. */
static char *maage_application_log_path(void)
{
#if MAAGE_PLATFORM_WINDOWS
    const wchar_t *base = _wgetenv(L"LOCALAPPDATA");
    const char *key = MAAGE_APPLICATION_KEY;
    wchar_t *path;
    char *utf8;
    int bytes;
    size_t length, index;
    if (!base) base = _wgetenv(L"TEMP");
    if (!base) return NULL;
    length = wcslen(base) + strlen(key) + 32;
    path = (wchar_t *)malloc(length * sizeof(*path));
    if (!path) return NULL;
    wcscpy(path, base);
    wcscat(path, L"\\MaaGE");
    _wmkdir(path);
    wcscat(path, L"\\");
    index = wcslen(path);
    while (*key) path[index++] = (wchar_t)(unsigned char)*key++;
    path[index] = L'\0';
    wcscat(path, L".log");
    bytes = WideCharToMultiByte(CP_UTF8, 0, path, -1, NULL, 0, NULL, NULL);
    utf8 = bytes ? (char *)malloc((size_t)bytes) : NULL;
    if (utf8 && !WideCharToMultiByte(CP_UTF8, 0, path, -1, utf8, bytes, NULL, NULL)) {
        free(utf8);
        utf8 = NULL;
    }
    free(path);
    return utf8;
#else
    const char *base;
    const char *suffix;
    char *path, *cursor;
    size_t length;
#if MAAGE_PLATFORM_MACOS
    base = getenv("HOME");
    suffix = "/Library/Logs/MaaGE/";
#else
    base = getenv("XDG_STATE_HOME");
    suffix = "/maage/";
    if (!base || base[0] != '/') {
        base = getenv("HOME");
        suffix = "/.local/state/maage/";
    }
#endif
    if (!base) return NULL;
    length = strlen(base) + strlen(suffix) + strlen(MAAGE_APPLICATION_KEY) + 5;
    path = (char *)malloc(length);
    if (!path) return NULL;
    strcpy(path, base);
    strcat(path, suffix);
    strcat(path, MAAGE_APPLICATION_KEY);
    strcat(path, ".log");
    for (cursor = path + 1; *cursor; ++cursor) {
        if (*cursor == '/') { *cursor = '\0'; mkdir(path, 0700); *cursor = '/'; }
    }
    return path;
#endif
}

static int maage_application_run(int argc, char *const argv[])
{
    maage_log_config config;
    char *path = maage_application_log_path();
    int result;
    maage_log_config_init(&config);
    if (path) { config.file = 1; config.file_path = path; }
    maage_log_init(&config, argc, argv);
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
