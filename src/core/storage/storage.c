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
#include <openssl/evp.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include <wchar.h>
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
int maage_storage_directories(const char *path)
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
                CreateDirectoryW(wide, NULL);
                attributes = GetFileAttributesW(wide);
                ok = attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) && !(attributes & FILE_ATTRIBUTE_REPARSE_POINT);
                free(wide);
            }
#else
            {
                struct stat st;
                mkdir(copy, 0700);
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
int maage_storage_sha1(const char *path, char hash[41], int (*cancel)(void *), void *context)
{
    FILE *file = maage_storage_open(path, "rb");
    EVP_MD_CTX *digest;
    unsigned char buffer[32768], bytes[EVP_MAX_MD_SIZE];
    unsigned int length, i;
    size_t n;
    int ok = 1;
    if (!file) return 0;
    digest = EVP_MD_CTX_new();
    if (!digest || EVP_DigestInit_ex(digest, EVP_sha1(), NULL) != 1) ok = 0;
    while (ok && (n = fread(buffer, 1, sizeof(buffer), file)) != 0) {
        if ((cancel && cancel(context)) || EVP_DigestUpdate(digest, buffer, n) != 1) ok = 0;
    }
    if (ferror(file)) ok = 0;
    if (ok && (EVP_DigestFinal_ex(digest, bytes, &length) != 1 || length != 20)) ok = 0;
    fclose(file); EVP_MD_CTX_free(digest);
    if (!ok) return 0;
    for (i = 0; i < 20; ++i) sprintf(hash + i * 2, "%02x", bytes[i]);
    hash[40] = 0; return 1;
}
maage_storage_lock *maage_storage_acquire(const char *path)
{
    maage_storage_lock *lock = (maage_storage_lock *)malloc(sizeof(*lock));
    if (!lock) return NULL;
#ifdef _WIN32
    {
        wchar_t *wide = maage_storage_wide(path);
        lock->handle = wide ? CreateFileW(wide, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, NULL) : INVALID_HANDLE_VALUE;
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
