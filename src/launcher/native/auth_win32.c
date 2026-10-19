#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#include <shellapi.h>
#include "../access/oauth.h"
#include "maage/log.h"
#include <stdlib.h>
#include <string.h>
int maage_auth_random(unsigned char *bytes, size_t count)
{
    NTSTATUS status;
    if (count > MAXULONG) { maage_log_error("maage.launcher.access.platform", "Cryptographic random request exceeds platform limit."); return 0; }
    status = BCryptGenRandom(NULL, bytes, (ULONG)count, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    if (status) maage_log_error("maage.launcher.access.platform", "Windows cryptographic random generation failed (NTSTATUS=%ld).", (long)status);
    return status == 0;
}
int maage_auth_browser(const char *url)
{
    wchar_t *wide;
    INT_PTR result;
    int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, url, -1, NULL, 0), ok;
    if (!count) { maage_log_warn("maage.launcher.access.platform", "Cannot convert browser URL (Windows error=%lu).", (unsigned long)GetLastError()); return 0; }
    wide = (wchar_t *)malloc((size_t)count * sizeof(*wide));
    if (!wide) { maage_log_error("maage.launcher.access.platform", "Cannot allocate browser launch request."); return 0; }
    ok = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, url, -1, wide, count) != 0;
    if (!ok) maage_log_warn("maage.launcher.access.platform", "Cannot convert browser URL (Windows error=%lu).", (unsigned long)GetLastError());
    else {
        result = (INT_PTR)ShellExecuteW(NULL, L"open", wide, NULL, NULL, SW_SHOWNORMAL);
        ok = result > 32;
        if (!ok) maage_log_warn("maage.launcher.access.platform", "System browser launch failed (ShellExecute error=%ld).", (long)result);
    }
    free(wide); return ok;
}
void maage_auth_sleep(unsigned milliseconds) { Sleep(milliseconds); }
