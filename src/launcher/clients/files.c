#ifdef __APPLE__
#define _DARWIN_C_SOURCE 1
#endif
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include "files.h"
#include <openssl/evp.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wchar.h>
static wchar_t *maage_client_wide(const char *path)
{
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, NULL, 0);
    wchar_t *w = n ? (wchar_t *)malloc((size_t)n * sizeof(*w)) : NULL;
    if (w && !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, w, n)) { free(w); return NULL; }
    return w;
}
#else
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef __APPLE__
#include <copyfile.h>
#endif
#endif
int maage_client_file_kind(const char *path)
{
#ifdef _WIN32
    wchar_t *w = maage_client_wide(path);
    DWORD a = w ? GetFileAttributesW(w) : INVALID_FILE_ATTRIBUTES;
    free(w);
    if (a == INVALID_FILE_ATTRIBUTES || (a & FILE_ATTRIBUTE_REPARSE_POINT)) return 0;
    return (a & FILE_ATTRIBUTE_DIRECTORY) ? 2 : (a & FILE_ATTRIBUTE_DEVICE) ? 0 : 1;
#else
    struct stat st;
    if (lstat(path, &st)) return 0;
    return S_ISREG(st.st_mode) ? 1 : S_ISDIR(st.st_mode) ? 2 : S_ISLNK(st.st_mode) ? 3 : 0;
#endif
}
int maage_client_executable_file(const char *path)
{
    if (maage_client_file_kind(path) != 1) return 0;
#ifdef _WIN32
    return 1;
#else
    return access(path, X_OK) == 0;
#endif
}
int maage_client_absolute(const char *path, char out[MAAGE_PATH_CAPACITY])
{
#ifdef _WIN32
    wchar_t *w = maage_client_wide(path), full[MAAGE_PATH_CAPACITY];
    DWORD n = w ? GetFullPathNameW(w, MAAGE_PATH_CAPACITY, full, NULL) : 0;
    unsigned i;
    free(w);
    if (!n || n >= MAAGE_PATH_CAPACITY || !WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, full, -1, out, MAAGE_PATH_CAPACITY, NULL, NULL)) return 0;
    for (i = 0; out[i]; ++i) if (out[i] == '\\') out[i] = '/';
    return maage_client_file_kind(out) != 0;
#else
    char *resolved = realpath(path, NULL);
    int ok = resolved && strlen(resolved) < MAAGE_PATH_CAPACITY;
    if (ok) strcpy(out, resolved);
    free(resolved); return ok;
#endif
}
int maage_client_sha256(const char *path, char hash[65])
{
    FILE *f = maage_storage_open(path, "rb");
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    unsigned char buffer[65536], digest[EVP_MAX_MD_SIZE];
    unsigned n, i;
    size_t got;
    int ok = f && ctx && EVP_DigestInit_ex(ctx, EVP_sha256(), NULL) == 1;
    while (ok && (got = fread(buffer, 1, sizeof(buffer), f)) != 0) ok = EVP_DigestUpdate(ctx, buffer, got) == 1;
    if (f) { if (ferror(f)) ok = 0; fclose(f); }
    if (ok) ok = EVP_DigestFinal_ex(ctx, digest, &n) == 1 && n == 32;
    EVP_MD_CTX_free(ctx);
    if (ok) for (i = 0; i < 32; ++i) sprintf(hash + i*2, "%02x", digest[i]);
    return ok;
}
static int maage_client_copy_file(const char *from, const char *to)
{
#ifdef _WIN32
    wchar_t *a = maage_client_wide(from), *b = maage_client_wide(to);
    int ok = a && b && CopyFileW(a, b, TRUE);
    free(a); free(b); return ok;
#else
    struct stat st;
    int ok;
#ifndef __APPLE__
    FILE *a, *b;
    char data[65536]; size_t n;
#endif
    if (lstat(from, &st) || !S_ISREG(st.st_mode)) return 0;
#ifdef __APPLE__
    /* Includes resource forks, extended attributes and executable mode. */
    ok = copyfile(from, to, NULL, COPYFILE_ALL | COPYFILE_EXCL | COPYFILE_NOFOLLOW_SRC) == 0;
#else
    a = maage_storage_open(from, "rb"); b = a ? maage_storage_open(to, "wb") : NULL;
    ok = a && b;
    while (ok && (n = fread(data, 1, sizeof(data), a)) != 0) ok = fwrite(data, 1, n, b) == n;
    if (a) { if (ferror(a)) ok = 0; fclose(a); }
    if (b && fclose(b)) ok = 0;
#endif
    return ok && chmod(to, st.st_mode & 0777) == 0;
#endif
}
static int maage_client_remove_file(const char *path)
{
#ifdef _WIN32
    wchar_t *wide = maage_client_wide(path);
    DWORD attributes = wide ? GetFileAttributesW(wide) : INVALID_FILE_ATTRIBUTES;
    if (!wide || attributes == INVALID_FILE_ATTRIBUTES) { free(wide); return 0; }
    if ((attributes & FILE_ATTRIBUTE_READONLY) && !SetFileAttributesW(wide, attributes & ~FILE_ATTRIBUTE_READONLY)) { free(wide); return 0; }
    free(wide);
#endif
    return maage_storage_remove(path);
}
static int maage_client_tree(const char *from, const char *to, const char *root, unsigned depth, unsigned *entries, double *bytes)
{
    char a[MAAGE_PATH_CAPACITY], b[MAAGE_PATH_CAPACITY];
    int kind = maage_client_file_kind(from), ok = 1;
    if (depth > 32 || ++*entries > 50000) return 0;
#ifndef _WIN32
    if (kind == 3) {
        char target[MAAGE_PATH_CAPACITY], resolved[MAAGE_PATH_CAPACITY];
        ssize_t n;
        size_t length = strlen(root);
        if (!to) return maage_storage_remove(from); /* Unlink, never follow. */
        n = readlink(from, target, sizeof(target) - 1);
        if (n <= 0 || (size_t)n >= sizeof(target) - 1) return 0;
        target[n] = 0;
        if (target[0] == '/' || !maage_client_absolute(from, resolved) || strncmp(root, resolved, length) ||
            (resolved[length] && resolved[length] != '/')) return 0;
        return symlink(target, to) == 0;
    }
#endif
    if (kind == 1) {
        double size; char fingerprint[128];
        if (!maage_storage_stat(from, &size, fingerprint) || (*bytes += size) > 8589934592.0) return 0;
        return to ? maage_client_copy_file(from, to) : maage_client_remove_file(from);
    }
    if (kind != 2 || !maage_storage_directory_exists(from) || (to && !maage_storage_directories(to))) return 0;
#ifdef _WIN32
    {
        WIN32_FIND_DATAW data; HANDLE find;
        wchar_t *pattern;
        if (!maage_storage_join(a, sizeof(a), from, "*")) return 0;
        pattern = maage_client_wide(a); if (!pattern) return 0;
        find = FindFirstFileW(pattern, &data); free(pattern);
        if (find == INVALID_HANDLE_VALUE) return 0;
        do {
            char name[MAAGE_PATH_CAPACITY];
            if (!wcscmp(data.cFileName, L".") || !wcscmp(data.cFileName, L"..")) continue;
            if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, data.cFileName, -1, name, sizeof(name), NULL, NULL) ||
                !maage_storage_join(a, sizeof(a), from, name) || (to && !maage_storage_join(b, sizeof(b), to, name)) ||
                !maage_client_tree(a, to ? b : NULL, root, depth + 1, entries, bytes)) { ok = 0; break; }
        } while (FindNextFileW(find, &data));
        if (ok && GetLastError() != ERROR_NO_MORE_FILES) ok = 0;
        FindClose(find);
    }
#else
    {
        DIR *dir = opendir(from); struct dirent *entry;
        if (!dir) return 0;
        errno = 0;
        while ((entry = readdir(dir)) != NULL) {
            if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) { errno = 0; continue; }
            if (!maage_storage_join(a, sizeof(a), from, entry->d_name) || (to && !maage_storage_join(b, sizeof(b), to, entry->d_name)) ||
                !maage_client_tree(a, to ? b : NULL, root, depth + 1, entries, bytes)) { ok = 0; break; }
            errno = 0;
        }
        if (ok && errno) ok = 0;
        closedir(dir);
    }
#endif
    if (!to && ok) { maage_storage_remove_directory(from); ok = !maage_client_file_kind(from); }
    return ok;
}
int maage_client_copy_tree(const char *source, const char *destination)
{ unsigned entries = 0; double bytes = 0; return maage_client_tree(source, destination, source, 0, &entries, &bytes); }
int maage_client_remove_tree(const char *path)
{ unsigned entries = 0; double bytes = 0; return maage_client_tree(path, NULL, path, 0, &entries, &bytes); }
