#include "maage/application.h"
#include "cmakecfg.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if MAAGE_PLATFORM_WINDOWS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <direct.h>
#include <wchar.h>
#else
#include <sys/stat.h>
#endif

/* Preserve existing stdio diagnostics without a console or working-directory
 * assumption. GUI applications append to separate per-user log files. */
static int maage_application_log_init(void)
{
#if MAAGE_PLATFORM_WINDOWS
    const wchar_t *base = _wgetenv(L"LOCALAPPDATA");
    const char *key = MAAGE_APPLICATION_KEY;
    wchar_t *path;
    size_t length, index;
    if (!base) base = _wgetenv(L"TEMP");
    if (!base) return 0;
    length = wcslen(base) + strlen(key) + 32;
    path = (wchar_t *)malloc(length * sizeof(*path));
    if (!path) return 0;
    wcscpy(path, base);
    wcscat(path, L"\\MaaGE");
    _wmkdir(path);
    wcscat(path, L"\\");
    index = wcslen(path);
    while (*key) path[index++] = (wchar_t)(unsigned char)*key++;
    path[index] = L'\0';
    wcscat(path, L".log");
    if (!_wfreopen(path, L"a", stdout) || !_wfreopen(path, L"a", stderr)) {
        OutputDebugStringA("MaaGE: unable to open the GUI diagnostic log.\n");
        free(path);
        return 0;
    }
    free(path);
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
    if (!base) return 0;
    length = strlen(base) + strlen(suffix) + strlen(MAAGE_APPLICATION_KEY) + 5;
    path = (char *)malloc(length);
    if (!path) return 0;
    strcpy(path, base);
    strcat(path, suffix);
    strcat(path, MAAGE_APPLICATION_KEY);
    strcat(path, ".log");
    for (cursor = path + 1; *cursor; ++cursor) {
        if (*cursor == '/') { *cursor = '\0'; mkdir(path, 0700); *cursor = '/'; }
    }
    if (!freopen(path, "a", stdout) || !freopen(path, "a", stderr)) {
        free(path);
        return 0;
    }
    free(path);
#endif
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
    return 1;
}

#if MAAGE_PLATFORM_WINDOWS
int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous, LPSTR command, int show)
{
    (void)instance; (void)previous; (void)command; (void)show;
    if (!maage_application_log_init()) return 1;
    return maage_application_main();
}
#else
int main(void)
{
    if (!maage_application_log_init()) return 1;
    return maage_application_main();
}
#endif
