#ifdef __APPLE__
#define _DARWIN_C_SOURCE 1
#endif
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include "files.h"
#include "maage/crypto.h"
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wchar.h>
static wchar_t *maage_client_wide(const char *path) {
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, NULL, 0);
    wchar_t *wide;
    if (!n) return NULL;
    wide = (wchar_t *) malloc((size_t) n * sizeof(*wide));
    if (!wide) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return NULL;
    }
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, wide, n)) {
        DWORD native = GetLastError();
        free(wide);
        SetLastError(native);
        return NULL;
    }
    return wide;
}
static maage_status maage_client_windows(maage_status_trace *trace, DWORD native, unsigned char operation) {
    return maage_status_trace_record(trace, maage_status_from_win32(native, MAAGE_CONTEXT_STORAGE, operation),
                                     MAAGE_CONTEXT_STORAGE, operation, __FILE__, (unsigned long) __LINE__, (long) native);
}
#else
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef __APPLE__
#include <copyfile.h>
#endif
#endif
static maage_status maage_client_errno(maage_status_trace *trace, unsigned char operation) {
    int native = errno;
    return maage_status_trace_record(trace, maage_status_from_errno(native ? native : EIO, MAAGE_CONTEXT_STORAGE, operation),
                                     MAAGE_CONTEXT_STORAGE, operation, __FILE__, (unsigned long) __LINE__, (long) native);
}
int maage_client_file_kind(const char *path) {
    if (!path) return 0;
#ifdef _WIN32
    {
        wchar_t *wide = maage_client_wide(path);
        DWORD attributes = wide ? GetFileAttributesW(wide) : INVALID_FILE_ATTRIBUTES;
        free(wide);
        if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) return 0;
        return (attributes & FILE_ATTRIBUTE_DIRECTORY) ? 2 : (attributes & FILE_ATTRIBUTE_DEVICE) ? 0
                                                                                                  : 1;
    }
#else
    {
        struct stat information;
        if (lstat(path, &information)) return 0;
        return S_ISREG(information.st_mode) ? 1 : S_ISDIR(information.st_mode) ? 2
                                              : S_ISLNK(information.st_mode)   ? 3
                                                                               : 0;
    }
#endif
}
int maage_client_executable_file(const char *path) {
    if (maage_client_file_kind(path) != 1) return 0;
#ifdef _WIN32
    return 1;
#else
    return access(path, X_OK) == 0;
#endif
}
maage_status maage_client_absolute(const char *path, char out[MAAGE_PATH_CAPACITY], maage_status_trace *trace) {
    maage_status result;
    if (!path || !out) return MAAGE_STATUS_PROPAGATE(trace, maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_RESOLVE, !path ? 0 : 1), MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_RESOLVE);
#ifdef _WIN32
    {
        wchar_t *wide = maage_client_wide(path), full[MAAGE_PATH_CAPACITY];
        DWORD count, native;
        unsigned i;
        if (!wide) return maage_client_windows(trace, GetLastError(), MAAGE_OPERATION_RESOLVE);
        count = GetFullPathNameW(wide, MAAGE_PATH_CAPACITY, full, NULL);
        native = GetLastError();
        free(wide);
        if (!count) return maage_client_windows(trace, native, MAAGE_OPERATION_RESOLVE);
        if (count >= MAAGE_PATH_CAPACITY) return MAAGE_STATUS_PROPAGATE(trace, maage_status_make(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_CONTEXT_STORAGE, MAAGE_OPERATION_RESOLVE, 0), MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_RESOLVE);
        if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, full, -1, out, MAAGE_PATH_CAPACITY, NULL, NULL))
            return maage_client_windows(trace, GetLastError(), MAAGE_OPERATION_RESOLVE);
        for (i = 0; out[i]; ++i)
            if (out[i] == '\\') out[i] = '/';
        result = maage_status_make(maage_client_file_kind(out) ? MAAGE_STATUS_SUCCESS : MAAGE_STATUS_NOT_FOUND,
                                   MAAGE_CONTEXT_STORAGE, MAAGE_OPERATION_RESOLVE, 0);
    }
#else
    {
        char *resolved = realpath(path, NULL);
        if (!resolved) return maage_client_errno(trace, MAAGE_OPERATION_RESOLVE);
        result = maage_status_make(strlen(resolved) < MAAGE_PATH_CAPACITY ? MAAGE_STATUS_SUCCESS : MAAGE_STATUS_BUFFER_OVERFLOW,
                                   MAAGE_CONTEXT_STORAGE, MAAGE_OPERATION_RESOLVE, 0);
        if (!maage_status_failed(result)) strcpy(out, resolved);
        free(resolved);
    }
#endif
    return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_RESOLVE);
}
maage_status maage_client_sha256(const char *path, char hash[65], maage_status_trace *trace) {
    FILE *file;
    maage_crypto_hash *digest = NULL;
    unsigned char buffer[65536], bytes[32];
    unsigned i;
    size_t received;
    maage_status result;
    if (hash) hash[0] = 0;
    if (!path || !hash) return MAAGE_STATUS_PROPAGATE(trace, maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_HASH, !path ? 0 : 1), MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_HASH);
    file = maage_storage_open(path, "rb");
    if (!file) return maage_client_errno(trace, MAAGE_OPERATION_OPEN);
    result = maage_crypto_hash_open(MAAGE_CRYPTO_SHA256, &digest);
    while (!maage_status_failed(result) && (received = fread(buffer, 1, sizeof(buffer), file)) != 0)
        result = maage_crypto_hash_update(digest, buffer, received);
    if (ferror(file) && !maage_status_failed(result)) result = maage_client_errno(trace, MAAGE_OPERATION_READ);
    if (!maage_status_failed(result)) result = maage_crypto_hash_finish(digest, bytes, sizeof(bytes));
    if (fclose(file) && !maage_status_failed(result)) result = maage_client_errno(trace, MAAGE_OPERATION_CLOSE);
    maage_crypto_hash_destroy(digest);
    if (!maage_status_failed(result)) {
        for (i = 0; i < 32; ++i)
            sprintf(hash + i * 2, "%02x", bytes[i]);
        hash[64] = 0;
    }
    return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_HASH);
}
static maage_status maage_client_copy_file(const char *from, const char *to, maage_status_trace *trace) {
    maage_status result;
#ifdef _WIN32
    wchar_t *source = maage_client_wide(from), *destination;
    DWORD native;
    if (!source) return maage_client_windows(trace, GetLastError(), MAAGE_OPERATION_COPY);
    destination = maage_client_wide(to);
    if (!destination) {
        native = GetLastError();
        free(source);
        return maage_client_windows(trace, native, MAAGE_OPERATION_COPY);
    }
    native = CopyFileW(source, destination, TRUE) ? 0 : GetLastError();
    free(source);
    free(destination);
    result = native ? maage_client_windows(trace, native, MAAGE_OPERATION_COPY) : maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_COPY, 0);
#else
    struct stat information;
#ifndef __APPLE__
    FILE *source, *destination;
    char data[65536];
    size_t count;
#endif
    if (lstat(from, &information)) return maage_client_errno(trace, MAAGE_OPERATION_QUERY);
    if (!S_ISREG(information.st_mode)) return MAAGE_STATUS_PROPAGATE(trace, maage_status_make(MAAGE_STATUS_ACCESS_DENIED, MAAGE_CONTEXT_STORAGE, MAAGE_OPERATION_COPY, 0), MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_COPY);
#ifdef __APPLE__
    /* Preserve resource forks, extended attributes and executable mode. */
    result = copyfile(from, to, NULL, COPYFILE_ALL | COPYFILE_EXCL | COPYFILE_NOFOLLOW_SRC) ? maage_client_errno(trace, MAAGE_OPERATION_COPY) : maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_COPY, 0);
#else
    source = maage_storage_open(from, "rb");
    if (!source) return maage_client_errno(trace, MAAGE_OPERATION_OPEN);
    destination = maage_storage_open(to, "wb");
    if (!destination) {
        result = maage_client_errno(trace, MAAGE_OPERATION_OPEN);
        fclose(source);
        return result;
    }
    result = maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_COPY, 0);
    while ((count = fread(data, 1, sizeof(data), source)) != 0) {
        if (fwrite(data, 1, count, destination) != count) {
            result = maage_client_errno(trace, MAAGE_OPERATION_WRITE);
            break;
        }
    }
    if (ferror(source) && !maage_status_failed(result)) result = maage_client_errno(trace, MAAGE_OPERATION_READ);
    if (fclose(source) && !maage_status_failed(result)) result = maage_client_errno(trace, MAAGE_OPERATION_CLOSE);
    if (!maage_status_failed(result)) result = maage_storage_sync(destination);
    if (fclose(destination) && !maage_status_failed(result)) result = maage_client_errno(trace, MAAGE_OPERATION_CLOSE);
#endif
    if (!maage_status_failed(result) && chmod(to, information.st_mode & 0777)) result = maage_client_errno(trace, MAAGE_OPERATION_SET);
#endif
    return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_COPY);
}
static maage_status maage_client_remove_file(const char *path, maage_status_trace *trace) {
#ifdef _WIN32
    wchar_t *wide = maage_client_wide(path);
    DWORD attributes, native;
    if (!wide) return maage_client_windows(trace, GetLastError(), MAAGE_OPERATION_REMOVE);
    attributes = GetFileAttributesW(wide);
    native = GetLastError();
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        free(wide);
        if (native == ERROR_FILE_NOT_FOUND || native == ERROR_PATH_NOT_FOUND) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_REMOVE, 0);
        return maage_client_windows(trace, native, MAAGE_OPERATION_QUERY);
    }
    if ((attributes & FILE_ATTRIBUTE_READONLY) && !SetFileAttributesW(wide, attributes & ~FILE_ATTRIBUTE_READONLY)) {
        native = GetLastError();
        free(wide);
        return maage_client_windows(trace, native, MAAGE_OPERATION_SET);
    }
    free(wide);
#endif
    return MAAGE_STATUS_PROPAGATE(trace, maage_storage_remove(path), MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_REMOVE);
}
static maage_status maage_client_tree(const char *from, const char *to, const char *root,
                                      unsigned depth, unsigned *entries, double *bytes, maage_status_trace *trace) {
    char source[MAAGE_PATH_CAPACITY], destination[MAAGE_PATH_CAPACITY];
    int kind = maage_client_file_kind(from);
    unsigned char operation = to ? MAAGE_OPERATION_COPY : MAAGE_OPERATION_REMOVE;
    maage_status result = maage_status_make(MAAGE_STATUS_SUCCESS, 0, operation, 0);
    if (depth > 32 || ++*entries > 50000) return MAAGE_STATUS_PROPAGATE(trace,
                                                                        maage_status_make(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_CONTEXT_LAUNCHER, operation, 0), MAAGE_CONTEXT_LAUNCHER, operation);
#ifndef _WIN32
    if (kind == 3) {
        char target[MAAGE_PATH_CAPACITY], resolved[MAAGE_PATH_CAPACITY];
        ssize_t count;
        size_t length = strlen(root);
        if (!to) return MAAGE_STATUS_PROPAGATE(trace, maage_storage_remove(from), MAAGE_CONTEXT_LAUNCHER, operation);
        count = readlink(from, target, sizeof(target) - 1);
        if (count < 0) return maage_client_errno(trace, MAAGE_OPERATION_READ);
        if (!count || (size_t) count >= sizeof(target) - 1) return MAAGE_STATUS_PROPAGATE(trace,
                                                                                          maage_status_make(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_CONTEXT_STORAGE, MAAGE_OPERATION_READ, 0), MAAGE_CONTEXT_LAUNCHER, operation);
        target[count] = 0;
        if (target[0] == '/') return MAAGE_STATUS_PROPAGATE(trace, maage_status_make(MAAGE_STATUS_ACCESS_DENIED, MAAGE_CONTEXT_STORAGE, operation, 0), MAAGE_CONTEXT_LAUNCHER, operation);
        result = maage_client_absolute(from, resolved, trace);
        if (maage_status_failed(result)) return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_LAUNCHER, operation);
        if (strncmp(root, resolved, length) || (resolved[length] && resolved[length] != '/')) return MAAGE_STATUS_PROPAGATE(trace,
                                                                                                                            maage_status_make(MAAGE_STATUS_ACCESS_DENIED, MAAGE_CONTEXT_STORAGE, operation, 0), MAAGE_CONTEXT_LAUNCHER, operation);
        if (symlink(target, to)) return maage_client_errno(trace, operation);
        return maage_status_make(MAAGE_STATUS_SUCCESS, 0, operation, 0);
    }
#endif
    if (kind == 1) {
        double size;
        char fingerprint[128];
        result = maage_storage_stat(from, &size, fingerprint);
        if (maage_status_failed(result)) return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_LAUNCHER, operation);
        *bytes += size;
        if (*bytes > 8589934592.0) return MAAGE_STATUS_PROPAGATE(trace, maage_status_make(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_CONTEXT_LAUNCHER, operation, 0), MAAGE_CONTEXT_LAUNCHER, operation);
        return to ? maage_client_copy_file(from, to, trace) : maage_client_remove_file(from, trace);
    }
    if (kind != 2 || !maage_storage_directory_exists(from)) return MAAGE_STATUS_PROPAGATE(trace,
                                                                                          maage_status_make(MAAGE_STATUS_ACCESS_DENIED, MAAGE_CONTEXT_STORAGE, operation, 0), MAAGE_CONTEXT_LAUNCHER, operation);
    if (to) {
        result = maage_storage_directories(to);
        if (maage_status_failed(result)) return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_LAUNCHER, operation);
    }
#ifdef _WIN32
    {
        WIN32_FIND_DATAW information;
        HANDLE search;
        wchar_t *pattern;
        DWORD native;
        result = maage_storage_join(source, sizeof(source), from, "*");
        if (maage_status_failed(result)) return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_LAUNCHER, operation);
        pattern = maage_client_wide(source);
        if (!pattern) return maage_client_windows(trace, GetLastError(), MAAGE_OPERATION_QUERY);
        search = FindFirstFileW(pattern, &information);
        native = GetLastError();
        free(pattern);
        if (search == INVALID_HANDLE_VALUE) return maage_client_windows(trace, native, MAAGE_OPERATION_QUERY);
        for (;;) {
            char name[MAAGE_PATH_CAPACITY];
            if (wcscmp(information.cFileName, L".") && wcscmp(information.cFileName, L"..")) {
                if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, information.cFileName, -1, name, sizeof(name), NULL, NULL)) {
                    result = maage_client_windows(trace, GetLastError(), MAAGE_OPERATION_RESOLVE);
                    break;
                }
                result = maage_storage_join(source, sizeof(source), from, name);
                if (!maage_status_failed(result) && to) result = maage_storage_join(destination, sizeof(destination), to, name);
                if (!maage_status_failed(result)) result = maage_client_tree(source, to ? destination : NULL, root, depth + 1, entries, bytes, trace);
                if (maage_status_failed(result)) break;
            }
            if (!FindNextFileW(search, &information)) {
                native = GetLastError();
                if (native != ERROR_NO_MORE_FILES) result = maage_client_windows(trace, native, MAAGE_OPERATION_QUERY);
                break;
            }
        }
        if (!FindClose(search) && !maage_status_failed(result)) result = maage_client_windows(trace, GetLastError(), MAAGE_OPERATION_CLOSE);
    }
#else
    {
        DIR *directory = opendir(from);
        struct dirent *entry;
        if (!directory) return maage_client_errno(trace, MAAGE_OPERATION_OPEN);
        for (;;) {
            errno = 0;
            entry = readdir(directory);
            if (!entry) {
                if (errno) result = maage_client_errno(trace, MAAGE_OPERATION_READ);
                break;
            }
            if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
            result = maage_storage_join(source, sizeof(source), from, entry->d_name);
            if (!maage_status_failed(result) && to) result = maage_storage_join(destination, sizeof(destination), to, entry->d_name);
            if (!maage_status_failed(result)) result = maage_client_tree(source, to ? destination : NULL, root, depth + 1, entries, bytes, trace);
            if (maage_status_failed(result)) break;
        }
        if (closedir(directory) && !maage_status_failed(result)) result = maage_client_errno(trace, MAAGE_OPERATION_CLOSE);
    }
#endif
    if (!to && !maage_status_failed(result)) result = maage_storage_remove_directory(from);
    return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_LAUNCHER, operation);
}
maage_status maage_client_copy_tree(const char *source, const char *destination, maage_status_trace *trace) {
    unsigned entries = 0;
    double bytes = 0;
    if (!source || !destination) return MAAGE_STATUS_PROPAGATE(trace, maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_COPY, !source ? 0 : 1), MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_COPY);
    return MAAGE_STATUS_PROPAGATE(trace, maage_client_tree(source, destination, source, 0, &entries, &bytes, trace), MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_COPY);
}
maage_status maage_client_remove_tree(const char *path, maage_status_trace *trace) {
    unsigned entries = 0;
    double bytes = 0;
    if (!path) return MAAGE_STATUS_PROPAGATE(trace, maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_REMOVE, 0), MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_REMOVE);
    return MAAGE_STATUS_PROPAGATE(trace, maage_client_tree(path, NULL, path, 0, &entries, &bytes, trace), MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_REMOVE);
}
