#ifdef __APPLE__
#define _DARWIN_C_SOURCE 1
#endif
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include "maage/storage.h"
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#include <shlobj.h>
#include <aclapi.h>
#include <wchar.h>
#include <io.h>
struct maage_storage_lock { HANDLE handle; };
static wchar_t *maage_storage_wide(const char *path)
{
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, NULL, 0);
    wchar_t *wide = n ? (wchar_t *)malloc((size_t)n * sizeof(*wide)) : NULL;
    if (wide && !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, wide, n)) { free(wide); return NULL; }
    return wide;
}
#else
#include <sys/stat.h>
#include <sys/file.h>
#include <fcntl.h>
#include <unistd.h>
struct maage_storage_lock { int descriptor; };
#endif

int maage_storage_join(char *path, size_t capacity, const char *base, const char *relative)
{
    const char *p;
    size_t n = strlen(base), r = strlen(relative);
    if (!n || !r || relative[0] == '/' || relative[0] == '\\' || strchr(relative, ':') || strchr(relative, '\\')) return 0;
    for (p = relative; *p; ++p)
        if ((p == relative || p[-1] == '/') && (p[0] == '.' && (!p[1] || p[1] == '/' || (p[1] == '.' && (!p[2] || p[2] == '/'))))) return 0;
    if (n + r + 2 > capacity) return 0;
    memmove(path, base, n); path[n] = '/'; memcpy(path + n + 1, relative, r + 1);
    return 1;
}
int maage_storage_data_path(char *path, size_t capacity)
{
#ifdef _WIN32
    wchar_t native[MAX_PATH];
    int n;
    if (SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, SHGFP_TYPE_CURRENT, native) != S_OK) return 0;
    n = WideCharToMultiByte(CP_UTF8, 0, native, -1, path, (int)capacity, NULL, NULL);
    return n && maage_storage_join(path, capacity, path, "MaaGE");
#else
    const char *home = getenv("HOME");
#ifdef __APPLE__
    if (!home || home[0] != '/') return 0;
    return maage_storage_join(path, capacity, home, "Library/Application Support/MaaGE");
#else
    const char *xdg = getenv("XDG_DATA_HOME");
    if (xdg && xdg[0] == '/') return maage_storage_join(path, capacity, xdg, "maage");
    if (!home || home[0] != '/') return 0;
    return maage_storage_join(path, capacity, home, ".local/share/maage");
#endif
#endif
}
static int maage_storage_check_directories(const char *path, int create)
{
    char copy[MAAGE_PATH_CAPACITY];
    size_t i, start;
    if (strlen(path) >= sizeof(copy)) return 0;
    strcpy(copy, path);
#ifdef _WIN32
    if (strlen(copy) < 3 || copy[1] != ':') return 0;
    start = 3;
#else
    if (copy[0] != '/') return 0;
    start = 1;
#endif
    for (i = start; ; ++i) {
        char saved = copy[i];
        if (saved == '/' || saved == '\\' || !saved) {
            int ok;
            copy[i] = 0;
#ifdef _WIN32
            {
                wchar_t *wide = maage_storage_wide(copy);
                DWORD attributes;
                if (!wide) return 0;
                if (create) CreateDirectoryW(wide, NULL);
                attributes = GetFileAttributesW(wide);
                ok = attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) && !(attributes & FILE_ATTRIBUTE_REPARSE_POINT);
                free(wide);
            }
#else
            {
                struct stat st;
                if (create) mkdir(copy, 0700);
                ok = lstat(copy, &st) == 0 && S_ISDIR(st.st_mode);
            }
#endif
            copy[i] = saved;
            if (!ok) return 0;
            if (!saved) break;
        }
    }
    return 1;
}
int maage_storage_directories(const char *path) { return maage_storage_check_directories(path, 1); }
int maage_storage_directory_exists(const char *path) { return maage_storage_check_directories(path, 0); }
FILE *maage_storage_open(const char *path, const char *mode)
{
#ifdef _WIN32
    wchar_t *wide = maage_storage_wide(path);
    FILE *file;
    DWORD attributes;
    if (!wide) return NULL;
    attributes = GetFileAttributesW(wide);
    if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY))) { free(wide); return NULL; }
    file = _wfopen(wide, !strcmp(mode, "rb") ? L"rb" : L"wb");
    free(wide); return file;
#else
    int flags = !strcmp(mode, "rb") ? O_RDONLY : O_WRONLY | O_CREAT | O_TRUNC;
    int fd = open(path, flags | O_NOFOLLOW, 0600);
    FILE *file;
    struct stat st;
    if (fd < 0) return NULL;
    if (fstat(fd, &st) || !S_ISREG(st.st_mode)) { close(fd); return NULL; }
    file = fdopen(fd, mode);
    if (!file) close(fd);
    return file;
#endif
}
int maage_storage_status(const char *path)
{
#ifdef _WIN32
    wchar_t *wide = maage_storage_wide(path);
    DWORD attributes, error;
    if (!wide) return -1;
    attributes = GetFileAttributesW(wide); error = GetLastError(); free(wide);
    if (attributes == INVALID_FILE_ATTRIBUTES)
        return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND ? 0 : -1;
    if (attributes & FILE_ATTRIBUTE_REPARSE_POINT) return -1;
    return attributes & FILE_ATTRIBUTE_DIRECTORY ? 2 : 1;
#else
    struct stat st;
    if (lstat(path, &st)) return errno == ENOENT ? 0 : -1;
    if (S_ISREG(st.st_mode)) return 1;
    return S_ISDIR(st.st_mode) ? 2 : -1;
#endif
}
int maage_storage_random(unsigned char *bytes, size_t count)
{
    size_t step;
#ifdef _WIN32
    while (count) {
        step = count > 65536 ? 65536 : count;
        if (BCryptGenRandom(NULL, bytes, (ULONG)step, BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) return 0;
        bytes += step; count -= step;
    }
    return 1;
#else
    int descriptor;
    ssize_t received;
    do { descriptor = open("/dev/urandom", O_RDONLY); } while (descriptor < 0 && errno == EINTR);
    if (descriptor < 0) return 0;
    while (count) {
        step = count > 65536 ? 65536 : count;
        received = read(descriptor, bytes, step);
        if (received < 0 && errno == EINTR) continue;
        if (received <= 0) { close(descriptor); return 0; }
        bytes += received; count -= (size_t)received;
    }
    close(descriptor); return 1;
#endif
}
int maage_storage_uuid_valid(const char *uuid)
{
    size_t index;
    int nonzero = 0;
    if (!uuid || strlen(uuid) != 36) return 0;
    for (index = 0; index < 36; ++index) {
        if (index == 8 || index == 13 || index == 18 || index == 23) {
            if (uuid[index] != '-') return 0;
        } else {
            if (!((uuid[index] >= '0' && uuid[index] <= '9') ||
                (uuid[index] >= 'a' && uuid[index] <= 'f'))) return 0;
            if (uuid[index] != '0') nonzero = 1;
        }
    }
    return nonzero;
}
int maage_storage_uuid_generate(char uuid[37])
{
    static const char hex[] = "0123456789abcdef";
    unsigned char bytes[16];
    size_t index, output = 0;
    if (!uuid || !maage_storage_random(bytes, sizeof(bytes))) return 0;
    bytes[6] = (unsigned char)((bytes[6] & 15) | 64);
    bytes[8] = (unsigned char)((bytes[8] & 63) | 128);
    for (index = 0; index < sizeof(bytes); ++index) {
        if (index == 4 || index == 6 || index == 8 || index == 10) uuid[output++] = '-';
        uuid[output++] = hex[bytes[index] >> 4];
        uuid[output++] = hex[bytes[index] & 15];
    }
    uuid[output] = '\0';
    return 1;
}
int maage_storage_sync(FILE *file)
{
    if (!file || fflush(file)) return 0;
#ifdef _WIN32
    return FlushFileBuffers((HANDLE)_get_osfhandle(_fileno(file))) != 0;
#else
    return fsync(fileno(file)) == 0;
#endif
}
int maage_storage_private_file(const char *path)
{
#ifdef _WIN32
    wchar_t *wide = maage_storage_wide(path);
    HANDLE process_token = NULL, file = INVALID_HANDLE_VALUE;
    TOKEN_USER *user = NULL;
    DWORD bytes = 0, attributes;
    EXPLICIT_ACCESSW access;
    PACL acl = NULL;
    int ok = 0;
    if (!wide) return 0;
    attributes = GetFileAttributesW(wide);
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))) goto done;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &process_token)) goto done;
    GetTokenInformation(process_token, TokenUser, NULL, 0, &bytes);
    if (!bytes || !(user = (TOKEN_USER *)malloc(bytes)) || !GetTokenInformation(process_token, TokenUser, user, bytes, &bytes)) goto done;
    memset(&access, 0, sizeof(access)); access.grfAccessPermissions = FILE_ALL_ACCESS;
    access.grfAccessMode = SET_ACCESS; access.grfInheritance = NO_INHERITANCE;
    access.Trustee.TrusteeForm = TRUSTEE_IS_SID; access.Trustee.TrusteeType = TRUSTEE_IS_USER;
    access.Trustee.ptstrName = (LPWSTR)user->User.Sid;
    if (SetEntriesInAclW(1, &access, NULL, &acl) != ERROR_SUCCESS) goto done;
    file = CreateFileW(wide, READ_CONTROL | WRITE_DAC, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, NULL);
    if (file == INVALID_HANDLE_VALUE) goto done;
    ok = SetSecurityInfo(file, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, NULL, NULL, acl, NULL) == ERROR_SUCCESS;
done:
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    if (process_token) CloseHandle(process_token);
    if (acl) LocalFree(acl);
    free(user); free(wide); return ok;
#else
    struct stat st;
    int descriptor = open(path, O_RDONLY | O_NOFOLLOW);
    int ok;
    if (descriptor < 0) return 0;
    ok = fstat(descriptor, &st) == 0 && S_ISREG(st.st_mode) && st.st_uid == geteuid() && fchmod(descriptor, 0600) == 0;
    close(descriptor); return ok;
#endif
}
int maage_storage_remove(const char *path)
{
#ifdef _WIN32
    wchar_t *wide = maage_storage_wide(path);
    int ok = wide && (DeleteFileW(wide) || GetLastError() == ERROR_FILE_NOT_FOUND);
    free(wide); return ok;
#else
    return unlink(path) == 0 || errno == ENOENT;
#endif
}
int maage_storage_replace(const char *temporary, const char *destination)
{
#ifdef _WIN32
    wchar_t *a = maage_storage_wide(temporary), *b = maage_storage_wide(destination);
    int ok = a && b && MoveFileExW(a, b, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    free(a); free(b); return ok;
#else
    return rename(temporary, destination) == 0;
#endif
}
int maage_storage_stat(const char *path, double *size, char fingerprint[128])
{
#ifdef _WIN32
    WIN32_FILE_ATTRIBUTE_DATA st;
    wchar_t *wide = maage_storage_wide(path);
    int ok = wide && GetFileAttributesExW(wide, GetFileExInfoStandard, &st);
    free(wide);
    if (!ok || (st.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))) return 0;
    *size = (double)st.nFileSizeHigh * 4294967296.0 + st.nFileSizeLow;
    sprintf(fingerprint, "%.0f-%lu-%lu-%lu-%lu", *size, (unsigned long)st.ftLastWriteTime.dwHighDateTime, (unsigned long)st.ftLastWriteTime.dwLowDateTime, (unsigned long)st.ftCreationTime.dwHighDateTime, (unsigned long)st.ftCreationTime.dwLowDateTime);
#else
    struct stat st;
    if (lstat(path, &st) || !S_ISREG(st.st_mode)) return 0;
    *size = (double)st.st_size;
#ifdef __APPLE__
    sprintf(fingerprint, "%.0f-%.0f-%ld-%.0f-%ld", *size, (double)st.st_mtimespec.tv_sec, st.st_mtimespec.tv_nsec, (double)st.st_ctimespec.tv_sec, st.st_ctimespec.tv_nsec);
#else
    sprintf(fingerprint, "%.0f-%.0f-%ld-%.0f-%ld", *size, (double)st.st_mtim.tv_sec, st.st_mtim.tv_nsec, (double)st.st_ctim.tv_sec, st.st_ctim.tv_nsec);
#endif
#endif
    return 1;
}
maage_storage_lock *maage_storage_acquire(const char *path)
{
    maage_storage_lock *lock = (maage_storage_lock *)malloc(sizeof(*lock));
    if (!lock) return NULL;
#ifdef _WIN32
    {
        wchar_t *wide = maage_storage_wide(path);
        /* Read/copy and rename preserve the held object's ownership, like a
         * POSIX flock on an inode. Never share writes: another lock requests
         * GENERIC_WRITE and cannot acquire this object until release. */
        lock->handle = wide ? CreateFileW(wide, GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_DELETE, NULL, OPEN_ALWAYS,
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, NULL) : INVALID_HANDLE_VALUE;
        free(wide);
        if (lock->handle == INVALID_HANDLE_VALUE) { free(lock); return NULL; }
    }
#else
    lock->descriptor = open(path, O_CREAT | O_RDWR | O_NOFOLLOW, 0600);
    if (lock->descriptor < 0 || flock(lock->descriptor, LOCK_EX | LOCK_NB)) {
        if (lock->descriptor >= 0) close(lock->descriptor);
        free(lock); return NULL;
    }
#endif
    return lock;
}
void maage_storage_release(maage_storage_lock *lock)
{
    if (!lock) return;
#ifdef _WIN32
    CloseHandle(lock->handle);
#else
    close(lock->descriptor);
#endif
    free(lock);
}

void maage_storage_remove_directory(const char *path)
{
#ifdef _WIN32
    wchar_t *wide = maage_storage_wide(path);
    if (wide) RemoveDirectoryW(wide);
    free(wide);
#else
    rmdir(path);
#endif
}
int maage_storage_same_file(const char *left, const char *right)
{
#ifdef _WIN32
    wchar_t *a = maage_storage_wide(left), *b = maage_storage_wide(right);
    HANDLE ah = INVALID_HANDLE_VALUE, bh = INVALID_HANDLE_VALUE;
    BY_HANDLE_FILE_INFORMATION ai, bi;
    int same = 0;
    if (a) ah = CreateFileW(a, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, NULL);
    if (b) bh = CreateFileW(b, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, NULL);
    if (ah != INVALID_HANDLE_VALUE && bh != INVALID_HANDLE_VALUE && GetFileInformationByHandle(ah, &ai) && GetFileInformationByHandle(bh, &bi))
        same = !(ai.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) &&
            !(bi.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) && ai.dwVolumeSerialNumber == bi.dwVolumeSerialNumber &&
            ai.nFileIndexHigh == bi.nFileIndexHigh && ai.nFileIndexLow == bi.nFileIndexLow;
    if (ah != INVALID_HANDLE_VALUE) CloseHandle(ah);
    if (bh != INVALID_HANDLE_VALUE) CloseHandle(bh);
    free(a); free(b); return same;
#else
    struct stat a, b;
    return !lstat(left, &a) && !lstat(right, &b) && S_ISREG(a.st_mode) && S_ISREG(b.st_mode) && a.st_dev == b.st_dev && a.st_ino == b.st_ino;
#endif
}
int maage_storage_clone(const char *source, const char *staging, int (*cancel)(void *), void *context)
{
    FILE *input, *output;
    unsigned char buffer[32768];
    char fingerprint[128];
    double size;
    size_t count;
    int ok = 1, linked;
    if ((cancel && cancel(context)) || !maage_storage_stat(source, &size, fingerprint) || !maage_storage_remove(staging)) return 0;
#ifdef _WIN32
    {
        wchar_t *a = maage_storage_wide(source), *b = maage_storage_wide(staging);
        linked = a && b && CreateHardLinkW(b, a, NULL);
        free(a); free(b);
    }
#else
    linked = link(source, staging) == 0;
#endif
    if (linked) return 1;
    input = maage_storage_open(source, "rb");
    if (!input) return 0;
    output = maage_storage_open(staging, "wb");
    if (!output) { fclose(input); return 0; }
    while ((count = fread(buffer, 1, sizeof(buffer), input)) != 0) {
        if ((cancel && cancel(context)) || fwrite(buffer, 1, count, output) != count) { ok = 0; break; }
    }
    if (ferror(input)) ok = 0;
    fclose(input);
    if (fclose(output)) ok = 0;
    if (!ok) maage_storage_remove(staging);
    return ok ? 2 : 0;
}
