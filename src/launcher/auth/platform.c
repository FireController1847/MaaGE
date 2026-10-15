#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include "internal.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wincred.h>
#include <bcrypt.h>
#include <shellapi.h>
#include <shlobj.h>
#include <io.h>
struct maage_auth_runtime { CRITICAL_SECTION mutex; CONDITION_VARIABLE changed; HANDLE thread; void (*work)(void *); void *context; };
static DWORD WINAPI maage_auth_entry(void *context)
{
    maage_auth_runtime *runtime = context;
    runtime->work(runtime->context);
    return 0;
}
#else
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include <time.h>
#include <spawn.h>
#include <sys/wait.h>
#ifdef __APPLE__
#include <Security/Security.h>
#include <CoreServices/CoreServices.h>
#include <CommonCrypto/CommonDigest.h>
#else
#include <libsecret/secret.h>
#endif
struct maage_auth_runtime { pthread_mutex_t mutex; pthread_cond_t changed; pthread_t thread; int started; pid_t browsers[MAAGE_AUTH_MAX_ACCOUNTS]; void (*work)(void *); void *context; };
static void *maage_auth_entry(void *context)
{
    maage_auth_runtime *runtime = context;
    runtime->work(runtime->context);
    return NULL;
}
#endif
maage_auth_runtime *maage_auth_runtime_create(void)
{
    maage_auth_runtime *runtime = calloc(1, sizeof(*runtime));
    if (!runtime) return NULL;
#ifdef _WIN32
    InitializeCriticalSection(&runtime->mutex); InitializeConditionVariable(&runtime->changed);
#else
    if (pthread_mutex_init(&runtime->mutex, NULL)) { free(runtime); return NULL; }
    if (pthread_cond_init(&runtime->changed, NULL)) { pthread_mutex_destroy(&runtime->mutex); free(runtime); return NULL; }
#endif
    return runtime;
}
void maage_auth_lock(maage_auth_runtime *runtime)
{
#ifdef _WIN32
    EnterCriticalSection(&runtime->mutex);
#else
    pthread_mutex_lock(&runtime->mutex);
#endif
}
void maage_auth_unlock(maage_auth_runtime *runtime)
{
#ifdef _WIN32
    LeaveCriticalSection(&runtime->mutex);
#else
    pthread_mutex_unlock(&runtime->mutex);
#endif
}
void maage_auth_wait(maage_auth_runtime *runtime)
{
#ifdef _WIN32
    SleepConditionVariableCS(&runtime->changed, &runtime->mutex, INFINITE);
#else
    pthread_cond_wait(&runtime->changed, &runtime->mutex);
#endif
}
void maage_auth_wake(maage_auth_runtime *runtime)
{
#ifdef _WIN32
    WakeConditionVariable(&runtime->changed);
#else
    pthread_cond_signal(&runtime->changed);
#endif
}
int maage_auth_thread_start(maage_auth_runtime *runtime, void (*work)(void *), void *context)
{
    runtime->work = work; runtime->context = context;
#ifdef _WIN32
    runtime->thread = CreateThread(NULL, 0, maage_auth_entry, runtime, 0, NULL);
    return runtime->thread != NULL;
#else
    runtime->started = pthread_create(&runtime->thread, NULL, maage_auth_entry, runtime) == 0;
    return runtime->started;
#endif
}
void maage_auth_runtime_destroy(maage_auth_runtime *runtime)
{
    if (!runtime) return;
#ifdef _WIN32
    if (runtime->thread) { WaitForSingleObject(runtime->thread, INFINITE); CloseHandle(runtime->thread); }
    DeleteCriticalSection(&runtime->mutex);
#else
    if (runtime->started) pthread_join(runtime->thread, NULL);
    maage_auth_browser_poll(runtime);
    pthread_cond_destroy(&runtime->changed); pthread_mutex_destroy(&runtime->mutex);
#endif
    free(runtime);
}
int maage_auth_random(unsigned char *bytes, size_t size)
{
    if (size > INT_MAX) return 0;
#ifdef _WIN32
    return BCryptGenRandom(NULL, bytes, (ULONG)size, BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0;
#elif defined(__APPLE__)
    return SecRandomCopyBytes(kSecRandomDefault, size, bytes) == errSecSuccess;
#else
    int descriptor = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    size_t offset = 0;
    ssize_t read_count;
    if (descriptor < 0) return 0;
    while (offset < size) {
        read_count = read(descriptor, bytes + offset, size - offset);
        if (read_count < 0 && errno == EINTR) continue;
        if (read_count <= 0) break;
        offset += (size_t)read_count;
    }
    close(descriptor);
    return offset == size;
#endif
}
int maage_auth_sha256(const unsigned char *bytes, size_t size, unsigned char digest[32])
{
    if (size > INT_MAX) return 0;
#ifdef _WIN32
    BCRYPT_ALG_HANDLE algorithm = NULL;
    BCRYPT_HASH_HANDLE hash = NULL;
    PUCHAR object = NULL;
    DWORD length = 0, received;
    int result = 0;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, NULL, 0) != 0) return 0;
    if (BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, (PUCHAR)&length, sizeof(length), &received, 0) != 0 || !length || length > 1048576) goto cleanup;
    object = malloc(length);
    if (!object || BCryptCreateHash(algorithm, &hash, object, length, NULL, 0, 0) != 0) goto cleanup;
    result = BCryptHashData(hash, (PUCHAR)bytes, (ULONG)size, 0) == 0 && BCryptFinishHash(hash, digest, 32, 0) == 0;
cleanup:
    if (hash) BCryptDestroyHash(hash);
    if (object) { maage_auth_wipe(object, length); free(object); }
    BCryptCloseAlgorithmProvider(algorithm, 0);
    return result;
#elif defined(__APPLE__)
    return CC_SHA256(bytes, (CC_LONG)size, digest) != NULL;
#else
    GChecksum *checksum = g_checksum_new(G_CHECKSUM_SHA256);
    gsize length = 32;
    if (!checksum) return 0;
    g_checksum_update(checksum, bytes, (gssize)size); g_checksum_get_digest(checksum, digest, &length);
    g_checksum_free(checksum);
    return length == 32;
#endif
}
int maage_auth_open_browser(maage_auth_runtime *runtime, const char *url)
{
#ifdef _WIN32
    (void)runtime;
    wchar_t wide[4096];
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, url, -1, wide, 4096)) return 0;
    return (INT_PTR)ShellExecuteW(NULL, L"open", wide, NULL, NULL, SW_SHOWNORMAL) > 32;
#elif defined(__APPLE__)
    (void)runtime;
    CFURLRef native = CFURLCreateWithBytes(NULL, (const UInt8 *)url, strlen(url), kCFStringEncodingUTF8, NULL);
    OSStatus result;
    if (!native) return 0;
    result = LSOpenCFURLRef(native, NULL); CFRelease(native);
    return result == noErr;
#else
    pid_t child;
    size_t slot;
    char *const args[] = { "xdg-open", (char *)url, NULL };
    extern char **environ;
    maage_auth_browser_poll(runtime);
    for (slot = 0; slot < MAAGE_AUTH_MAX_ACCOUNTS; ++slot) if (!runtime->browsers[slot]) break;
    if (slot == MAAGE_AUTH_MAX_ACCOUNTS) return 0;
    if (posix_spawnp(&child, "xdg-open", NULL, NULL, args, environ)) return 0;
    runtime->browsers[slot] = child;
    return 1;
#endif
}

void maage_auth_browser_poll(maage_auth_runtime *runtime)
{
#if !defined(_WIN32) && !defined(__APPLE__)
    int status;
    size_t slot;
    for (slot = 0; slot < MAAGE_AUTH_MAX_ACCOUNTS; ++slot)
        if (runtime->browsers[slot] && waitpid(runtime->browsers[slot], &status, WNOHANG) == runtime->browsers[slot]) runtime->browsers[slot] = 0;
#else
    (void)runtime;
#endif
}

#ifdef _WIN32
static int maage_auth_wide_key(const char *key, wchar_t wide[128])
{
    return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, key, -1, wide, 128) != 0;
}
int maage_auth_secret_get(const char *key, char *output, size_t capacity)
{
    wchar_t wide[128]; PCREDENTIALW credential;
    int result;
    if (!maage_auth_wide_key(key, wide)) return -1;
    if (!CredReadW(wide, CRED_TYPE_GENERIC, 0, &credential)) return GetLastError() == ERROR_NOT_FOUND ? 0 : -1;
    result = credential->CredentialBlobSize < capacity && !memchr(credential->CredentialBlob, 0, credential->CredentialBlobSize);
    if (result) { memcpy(output, credential->CredentialBlob, credential->CredentialBlobSize); output[credential->CredentialBlobSize] = '\0'; }
    maage_auth_wipe(credential->CredentialBlob, credential->CredentialBlobSize); CredFree(credential);
    return result ? 1 : -1;
}
int maage_auth_secret_set(const char *key, const char *value)
{
    wchar_t wide[128]; CREDENTIALW credential;
    size_t size = strlen(value);
    if (!maage_auth_wide_key(key, wide) || size > CRED_MAX_CREDENTIAL_BLOB_SIZE) return -1;
    memset(&credential, 0, sizeof(credential));
    credential.Type = CRED_TYPE_GENERIC; credential.TargetName = wide;
    credential.CredentialBlob = (LPBYTE)value; credential.CredentialBlobSize = (DWORD)size;
    credential.Persist = CRED_PERSIST_LOCAL_MACHINE; credential.UserName = L"MaaGE";
    return CredWriteW(&credential, 0) ? 1 : -1;
}
int maage_auth_secret_delete(const char *key)
{
    wchar_t wide[128];
    if (!maage_auth_wide_key(key, wide)) return -1;
    return CredDeleteW(wide, CRED_TYPE_GENERIC, 0) || GetLastError() == ERROR_NOT_FOUND ? 1 : -1;
}
#elif defined(__APPLE__)
static CFMutableDictionaryRef maage_auth_keychain_query(const char *key)
{
    CFMutableDictionaryRef query = CFDictionaryCreateMutable(NULL, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFStringRef account = CFStringCreateWithCString(NULL, key, kCFStringEncodingUTF8);
    if (!query || !account) { if (query) CFRelease(query); if (account) CFRelease(account); return NULL; }
    CFDictionarySetValue(query, kSecClass, kSecClassGenericPassword);
    CFDictionarySetValue(query, kSecAttrService, CFSTR("MaaGE Microsoft Authentication"));
    CFDictionarySetValue(query, kSecAttrAccount, account); CFRelease(account);
    return query;
}
int maage_auth_secret_get(const char *key, char *output, size_t capacity)
{
    CFMutableDictionaryRef query = maage_auth_keychain_query(key);
    CFDataRef data = NULL;
    OSStatus status;
    CFIndex size;
    int result = -1;
    if (!query) return -1;
    CFDictionarySetValue(query, kSecReturnData, kCFBooleanTrue);
    CFDictionarySetValue(query, kSecMatchLimit, kSecMatchLimitOne);
    status = SecItemCopyMatching(query, (CFTypeRef *)&data); CFRelease(query);
    if (status == errSecItemNotFound) return 0;
    if (status != errSecSuccess || !data) return -1;
    size = CFDataGetLength(data);
    if (size > 0 && (size_t)size < capacity && !memchr(CFDataGetBytePtr(data), 0, (size_t)size)) {
        memcpy(output, CFDataGetBytePtr(data), (size_t)size); output[size] = '\0'; result = 1;
    }
    CFRelease(data);
    return result;
}
int maage_auth_secret_set(const char *key, const char *value)
{
    CFMutableDictionaryRef query = maage_auth_keychain_query(key), update;
    CFDataRef data;
    OSStatus status;
    if (!query) return -1;
    data = CFDataCreate(NULL, (const UInt8 *)value, strlen(value));
    update = CFDictionaryCreateMutable(NULL, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    if (!data || !update) { if (data) CFRelease(data); if (update) CFRelease(update); CFRelease(query); return -1; }
    CFDictionarySetValue(update, kSecValueData, data);
    status = SecItemUpdate(query, update);
    if (status == errSecItemNotFound) {
        CFDictionarySetValue(query, kSecValueData, data);
        CFDictionarySetValue(query, kSecAttrAccessible, kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly);
        status = SecItemAdd(query, NULL);
    }
    CFRelease(update); CFRelease(data); CFRelease(query);
    return status == errSecSuccess ? 1 : -1;
}
int maage_auth_secret_delete(const char *key)
{
    CFMutableDictionaryRef query = maage_auth_keychain_query(key);
    OSStatus status;
    if (!query) return -1;
    status = SecItemDelete(query); CFRelease(query);
    return status == errSecSuccess || status == errSecItemNotFound ? 1 : -1;
}
#else
static const SecretSchema maage_auth_schema = {
    "org.maage.Microsoft", SECRET_SCHEMA_NONE, { { "account", SECRET_SCHEMA_ATTRIBUTE_STRING }, { NULL, 0 } },
    0, NULL, NULL, NULL, NULL, NULL, NULL, NULL
};
int maage_auth_secret_get(const char *key, char *output, size_t capacity)
{
    GError *error = NULL;
    gchar *value = secret_password_lookup_sync(&maage_auth_schema, NULL, &error, "account", key, NULL);
    int result = error ? -1 : value ? (maage_auth_copy(output, capacity, value) ? 1 : -1) : 0;
    if (error) g_error_free(error);
    secret_password_free(value);
    return result;
}
int maage_auth_secret_set(const char *key, const char *value)
{
    GError *error = NULL;
    int result = secret_password_store_sync(&maage_auth_schema, SECRET_COLLECTION_DEFAULT, "MaaGE Microsoft account", value, NULL, &error, "account", key, NULL);
    if (error) g_error_free(error);
    return result ? 1 : -1;
}
int maage_auth_secret_delete(const char *key)
{
    GError *error = NULL;
    secret_password_clear_sync(&maage_auth_schema, NULL, &error, "account", key, NULL);
    if (error) { g_error_free(error); return -1; }
    return 1;
}
#endif
