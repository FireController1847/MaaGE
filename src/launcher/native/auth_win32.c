#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#include <shellapi.h>
#include "../access/oauth.h"
#include "maage/storage.h"
#include <openssl/crypto.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int maage_auth_random(unsigned char *bytes, size_t count)
{ return count <= MAXULONG && BCryptGenRandom(NULL, bytes, (ULONG)count, BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0; }
int maage_auth_browser(const char *url)
{
    wchar_t *wide;
    int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, url, -1, NULL, 0), ok;
    if (!count) return 0;
    wide = (wchar_t *)malloc((size_t)count * sizeof(*wide)); if (!wide) return 0;
    ok = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, url, -1, wide, count) &&
        (INT_PTR)ShellExecuteW(NULL, L"open", wide, NULL, NULL, SW_SHOWNORMAL) > 32;
    free(wide); return ok;
}
void maage_auth_sleep(unsigned milliseconds) { Sleep(milliseconds); }
static void maage_auth_registry(const wchar_t *path, const wchar_t *name, const wchar_t *value)
{
    HKEY key;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, path, 0, NULL, 0, KEY_SET_VALUE, NULL, &key, NULL) != ERROR_SUCCESS) return;
    RegSetValueExW(key, name, 0, REG_SZ, (const BYTE *)value, (DWORD)((wcslen(value) + 1) * sizeof(*value))); RegCloseKey(key);
}
void maage_auth_platform_start(void)
{
    wchar_t executable[32768], command[32800];
    DWORD count = GetModuleFileNameW(NULL, executable, 32768);
    if (!count || count >= 32768) return;
    /* Per-user registration uses this build's absolute executable, including
     * installations outside Program Files. No elevation or third-party edits. */
    wcscpy(command, L"\""); wcscat(command, executable); wcscat(command, L"\" \"%1\"");
    maage_auth_registry(L"Software\\Classes\\maage", NULL, L"URL:MaaGE Authentication");
    maage_auth_registry(L"Software\\Classes\\maage", L"URL Protocol", L"");
    maage_auth_registry(L"Software\\Classes\\maage\\shell\\open\\command", NULL, command);
}
void maage_auth_platform_stop(void) { }
