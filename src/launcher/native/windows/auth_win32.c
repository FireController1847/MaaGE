#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#include <shellapi.h>
#include "../../access/oauth.h"
#include <limits.h>
#include <stdlib.h>
maage_status maage_auth_random(unsigned char *bytes, size_t count) {
    NTSTATUS native;
    if ((!bytes && count) || count > ULONG_MAX) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_AUTH, MAAGE_OPERATION_CREATE, 0);
    if (!count) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_CREATE, 0);
    native = BCryptGenRandom(NULL, bytes, (ULONG) count, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    return maage_status_make(native ? MAAGE_STATUS_ERROR : MAAGE_STATUS_SUCCESS, MAAGE_CONTEXT_AUTH, MAAGE_OPERATION_CREATE, 0);
}
maage_status maage_auth_browser(const char *url) {
    wchar_t *wide;
    INT_PTR native = 0;
    int count, ok;
    if (!url) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_AUTH, MAAGE_OPERATION_OPEN, 0);
    count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, url, -1, NULL, 0);
    if (!count) return maage_status_from_win32(GetLastError(), MAAGE_CONTEXT_AUTH, MAAGE_OPERATION_OPEN);
    wide = (wchar_t *) malloc((size_t) count * sizeof(*wide));
    if (!wide) return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_AUTH, MAAGE_OPERATION_OPEN, 0);
    ok = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, url, -1, wide, count) != 0;
    if (ok) {
        native = (INT_PTR) ShellExecuteW(NULL, L"open", wide, NULL, NULL, SW_SHOWNORMAL);
        ok = native > 32;
    }
    free(wide);
    return maage_status_make(ok ? MAAGE_STATUS_SUCCESS : MAAGE_STATUS_SYSTEM_NOT_SUPPORTED, MAAGE_CONTEXT_AUTH, MAAGE_OPERATION_OPEN, 0);
}
maage_status maage_auth_sleep(unsigned milliseconds) {
    Sleep(milliseconds);
    return maage_status_make(milliseconds ? MAAGE_STATUS_SUCCESS : MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_WAIT, 0);
}
