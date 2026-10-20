#ifdef __APPLE__
#define _DARWIN_C_SOURCE 1
#endif
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#ifdef __linux__
#define _GNU_SOURCE 1
#endif
#endif
#include "maage/folder_link.h"
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winioctl.h>
#include <wchar.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>
#ifdef __linux__
#include <sys/syscall.h>
#ifndef RENAME_NOREPLACE
#define RENAME_NOREPLACE 1
#endif
#endif
#endif

#ifdef _WIN32
static wchar_t *maage_link_wide(const char *path)
{
    int count;
    wchar_t *wide;
    if (!path) return NULL;
    count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, NULL, 0);
    wide = count ? (wchar_t *)malloc((size_t)count * sizeof(*wide)) : NULL;
    if (wide && !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, wide, count)) {
        free(wide); return NULL;
    }
    return wide;
}
static int maage_link_missing_error(DWORD error)
{
    return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND ||
        error == ERROR_CANT_RESOLVE_FILENAME;
}
#endif

maage_folder_link_kind maage_folder_link_status(const char *path)
{
    if (!path || !*path) return MAAGE_FOLDER_LINK_ERROR;
#ifdef _WIN32
    {
        wchar_t *wide = maage_link_wide(path);
        DWORD attributes, error, received, tag;
        unsigned char reparse[MAXIMUM_REPARSE_DATA_BUFFER_SIZE];
        BY_HANDLE_FILE_INFORMATION information;
        HANDLE handle;
        int valid;
        if (!wide) return MAAGE_FOLDER_LINK_ERROR;
        handle = CreateFileW(wide, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            NULL, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, NULL);
        error = GetLastError();
        if (handle == INVALID_HANDLE_VALUE) {
            free(wide);
            return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND ?
                MAAGE_FOLDER_LINK_MISSING : MAAGE_FOLDER_LINK_ERROR;
        }
        if (!GetFileInformationByHandle(handle, &information)) {
            CloseHandle(handle); free(wide); return MAAGE_FOLDER_LINK_ERROR;
        }
        attributes = information.dwFileAttributes;
        if (!(attributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
            CloseHandle(handle); free(wide);
            return attributes & FILE_ATTRIBUTE_DIRECTORY ? MAAGE_FOLDER_LINK_DIRECTORY : MAAGE_FOLDER_LINK_FILE;
        }
        valid = DeviceIoControl(handle, FSCTL_GET_REPARSE_POINT,
            NULL, 0, reparse, sizeof(reparse), &received, NULL) && received >= sizeof(tag);
        CloseHandle(handle);
        if (!valid) { free(wide); return MAAGE_FOLDER_LINK_ERROR; }
        memcpy(&tag, reparse, sizeof(tag));
        if (tag != IO_REPARSE_TAG_MOUNT_POINT && tag != IO_REPARSE_TAG_SYMLINK) {
            free(wide); return MAAGE_FOLDER_LINK_ERROR;
        }
        handle = CreateFileW(wide, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
        error = GetLastError(); free(wide);
        if (handle == INVALID_HANDLE_VALUE)
            return maage_link_missing_error(error) ? MAAGE_FOLDER_LINK_BROKEN : MAAGE_FOLDER_LINK_ERROR;
        CloseHandle(handle); return MAAGE_FOLDER_LINK_LINK;
    }
#else
    {
        struct stat information;
        if (lstat(path, &information))
            return errno == ENOENT || errno == ENOTDIR ? MAAGE_FOLDER_LINK_MISSING : MAAGE_FOLDER_LINK_ERROR;
        if (S_ISLNK(information.st_mode)) {
            if (!stat(path, &information)) return MAAGE_FOLDER_LINK_LINK;
            return errno == ENOENT || errno == ENOTDIR || errno == ELOOP ?
                MAAGE_FOLDER_LINK_BROKEN : MAAGE_FOLDER_LINK_ERROR;
        }
        if (S_ISDIR(information.st_mode)) return MAAGE_FOLDER_LINK_DIRECTORY;
        return S_ISREG(information.st_mode) ? MAAGE_FOLDER_LINK_FILE : MAAGE_FOLDER_LINK_ERROR;
    }
#endif
}

/* Normalize separators, establish the absolute root, and bound all path work. */
static size_t maage_link_absolute(const char *path, char normalized[MAAGE_PATH_CAPACITY])
{
    size_t length, index, root;
    if (!path || !(length = strlen(path)) || length >= MAAGE_PATH_CAPACITY) return 0;
    memcpy(normalized, path, length + 1);
#ifdef _WIN32
    for (index = 0; index < length; ++index)
        if (normalized[index] == '\\') normalized[index] = '/';
    if (((normalized[0] >= 'A' && normalized[0] <= 'Z') ||
        (normalized[0] >= 'a' && normalized[0] <= 'z')) &&
        normalized[1] == ':' && normalized[2] == '/') root = 3;
    else if (normalized[0] == '/' && normalized[1] == '/') {
        index = 2;
        while (normalized[index] && normalized[index] != '/') ++index;
        if (index == 2 || !normalized[index] ||
            (index == 3 && (normalized[2] == '.' || normalized[2] == '?')) ||
            (index == 4 && normalized[2] == '.' && normalized[3] == '.')) return 0;
        root = ++index;
        while (normalized[index] && normalized[index] != '/') ++index;
        if (index == root) return 0;
        if ((index - root == 1 && normalized[root] == '.') ||
            (index - root == 2 && normalized[root] == '.' && normalized[root + 1] == '.')) return 0;
        root = index;
    } else return 0;
    for (index = root; index < length; ++index)
        if (normalized[index] == ':') return 0;
    if (root != 3) for (index = 2; index < root; ++index)
        if (normalized[index] == ':') return 0;
    /* Win32 strips trailing dots/spaces and recognizes device syntax. Reject
     * aliases before any lexical ancestry decision or filesystem operation. */
    for (index = 0; index < length; ++index) {
        if ((unsigned char)normalized[index] < 32 || strchr("<>\"|?*", normalized[index])) return 0;
        if ((normalized[index] == '.' || normalized[index] == ' ') &&
            (!normalized[index + 1] || normalized[index + 1] == '/')) return 0;
    }
#else
    (void)index;
    if (normalized[0] != '/') return 0;
    root = 1;
#endif
    while (length > root && normalized[length - 1] == '/') normalized[--length] = '\0';
    return root;
}

int maage_folder_link_canonical(const char *path, char *resolved, size_t capacity)
{
    char normalized[MAAGE_PATH_CAPACITY];
    size_t length;
    if (!resolved || !capacity || !maage_link_absolute(path, normalized)) return 0;
#ifdef _WIN32
    {
        wchar_t *wide = maage_link_wide(normalized);
        wchar_t canonical[MAAGE_PATH_CAPACITY], *start;
        BY_HANDLE_FILE_INFORMATION information;
        HANDLE handle;
        DWORD count;
        int converted;
        handle = wide ? CreateFileW(wide, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL) : INVALID_HANDLE_VALUE;
        free(wide);
        if (handle == INVALID_HANDLE_VALUE) return 0;
        count = GetFinalPathNameByHandleW(handle, canonical, MAAGE_PATH_CAPACITY, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
        if (!GetFileInformationByHandle(handle, &information)) count = 0;
        CloseHandle(handle);
        if (!count || count >= MAAGE_PATH_CAPACITY || !(information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) return 0;
        start = canonical;
        if (!wcsncmp(start, L"\\\\?\\UNC\\", 8)) { start += 6; start[0] = L'\\'; }
        else if (!wcsncmp(start, L"\\\\?\\", 4)) start += 4;
        if (capacity > 2147483647UL) return 0;
        converted = WideCharToMultiByte(CP_UTF8, 0, start, -1, resolved, (int)capacity, NULL, NULL);
        if (!converted || !maage_link_absolute(resolved, normalized)) return 0;
    }
#else
    {
        char *canonical;
        struct stat information;
        canonical = realpath(normalized, NULL);
        if (!canonical) return 0;
        length = strlen(canonical);
        if (length >= capacity || length >= sizeof(normalized) ||
            stat(canonical, &information) || !S_ISDIR(information.st_mode)) {
            free(canonical); return 0;
        }
        memcpy(normalized, canonical, length + 1); free(canonical);
    }
#endif
    length = strlen(normalized);
    if (length >= capacity) return 0;
    memcpy(resolved, normalized, length + 1); return 1;
}

int maage_folder_link_managed_path(const char *path, int allow_leaf_link)
{
    char normalized[MAAGE_PATH_CAPACITY], prefix[MAAGE_PATH_CAPACITY];
    size_t root, start, end, length;
    int missing = 0;
    maage_folder_link_kind kind;
    root = maage_link_absolute(path, normalized);
    if (!root) return 0;
    length = strlen(normalized);
    memcpy(prefix, normalized, root); prefix[root] = '\0';
    if (maage_folder_link_status(prefix) != MAAGE_FOLDER_LINK_DIRECTORY) return 0;
    start = root;
    if (normalized[start] == '/') ++start;
    while (start < length) {
        end = start;
        while (normalized[end] && normalized[end] != '/') ++end;
        if (end == start || (end - start == 1 && normalized[start] == '.') ||
            (end - start == 2 && normalized[start] == '.' && normalized[start + 1] == '.')) return 0;
        memcpy(prefix, normalized, end); prefix[end] = '\0';
        kind = missing ? MAAGE_FOLDER_LINK_MISSING : maage_folder_link_status(prefix);
        if (kind == MAAGE_FOLDER_LINK_MISSING) missing = 1;
        else if (kind != MAAGE_FOLDER_LINK_DIRECTORY &&
            !(allow_leaf_link && end == length &&
            (kind == MAAGE_FOLDER_LINK_LINK || kind == MAAGE_FOLDER_LINK_BROKEN))) return 0;
        start = end + 1;
    }
    return 1;
}

static int maage_link_contains(const char *root, const char *path)
{
    size_t length = strlen(root), path_length = strlen(path);
    if (path_length < length) return 0;
#ifdef _WIN32
    {
        wchar_t *left = maage_link_wide(root), *right = maage_link_wide(path);
        size_t count = left ? wcslen(left) : 0;
        int equal = left && right && wcslen(right) >= count && count <= 2147483647UL &&
            CompareStringOrdinal(left, (int)count, right, (int)count, TRUE) == CSTR_EQUAL &&
            (left[count - 1] == L'/' || !right[count] || right[count] == L'/');
        free(left); free(right);
        return equal;
    }
#else
    if (strncmp(root, path, length)) return 0;
    return root[length - 1] == '/' || !path[length] || path[length] == '/';
#endif
}
int maage_folder_link_within(const char *root, const char *path)
{
    char canonical_root[MAAGE_PATH_CAPACITY], canonical_path[MAAGE_PATH_CAPACITY];
    return maage_folder_link_canonical(root, canonical_root, sizeof(canonical_root)) &&
        maage_folder_link_canonical(path, canonical_path, sizeof(canonical_path)) &&
        maage_link_contains(canonical_root, canonical_path);
}
static int maage_link_lexical(const char *path, char normalized[MAAGE_PATH_CAPACITY])
{
    size_t start, end, length;
    start = maage_link_absolute(path, normalized);
    if (!start) return 0;
    length = strlen(normalized);
    if (normalized[start] == '/') ++start;
    while (start < length) {
        end = start;
        while (normalized[end] && normalized[end] != '/') ++end;
        if (end == start || (end - start == 1 && normalized[start] == '.') ||
            (end - start == 2 && normalized[start] == '.' && normalized[start + 1] == '.')) return 0;
        start = end + 1;
    }
    return 1;
}
int maage_folder_link_path_within(const char *root, const char *path)
{
    char normalized_root[MAAGE_PATH_CAPACITY], normalized_path[MAAGE_PATH_CAPACITY];
    return maage_link_lexical(root, normalized_root) && maage_link_lexical(path, normalized_path) &&
        maage_link_contains(normalized_root, normalized_path);
}
static int maage_link_raw_target(const char *path, char target[MAAGE_PATH_CAPACITY])
{
#ifdef _WIN32
    wchar_t *wide = maage_link_wide(path), native[MAAGE_PATH_CAPACITY], *start;
    unsigned char reparse[MAXIMUM_REPARSE_DATA_BUFFER_SIZE];
    DWORD received, tag, flags;
    WORD offset, length;
    size_t base;
    HANDLE handle;
    int ok;
    handle = wide ? CreateFileW(wide, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, NULL) : INVALID_HANDLE_VALUE;
    free(wide);
    if (handle == INVALID_HANDLE_VALUE) return 0;
    ok = DeviceIoControl(handle, FSCTL_GET_REPARSE_POINT, NULL, 0,
        reparse, sizeof(reparse), &received, NULL) != 0;
    CloseHandle(handle);
    if (!ok || received < 16) return 0;
    memcpy(&tag, reparse, sizeof(tag));
    if (tag == IO_REPARSE_TAG_MOUNT_POINT) base = 16;
    else if (tag == IO_REPARSE_TAG_SYMLINK && received >= 20) {
        memcpy(&flags, reparse + 16, sizeof(flags));
        if (flags & 1) return 0;
        base = 20;
    } else return 0;
    memcpy(&offset, reparse + 8, sizeof(offset));
    memcpy(&length, reparse + 10, sizeof(length));
    if ((offset & 1) || (length & 1) || (size_t)length / sizeof(wchar_t) >= MAAGE_PATH_CAPACITY ||
        base + offset + length > received) return 0;
    memcpy(native, reparse + base + offset, length);
    native[length / sizeof(wchar_t)] = L'\0'; start = native;
    if (!wcsncmp(start, L"\\??\\UNC\\", 8)) { start += 6; start[0] = L'\\'; }
    else if (!wcsncmp(start, L"\\??\\", 4)) start += 4;
    else if (!wcsncmp(start, L"\\\\?\\UNC\\", 8)) { start += 6; start[0] = L'\\'; }
    else if (!wcsncmp(start, L"\\\\?\\", 4)) start += 4;
    return WideCharToMultiByte(CP_UTF8, 0, start, -1, target, MAAGE_PATH_CAPACITY, NULL, NULL) != 0;
#else
    ssize_t length = readlink(path, target, MAAGE_PATH_CAPACITY - 1);
    if (length < 0 || length >= MAAGE_PATH_CAPACITY - 1) return 0;
    target[length] = '\0'; return 1;
#endif
}
int maage_folder_link_matches(const char *path, const char *expected_target)
{
    char actual[MAAGE_PATH_CAPACITY], expected[MAAGE_PATH_CAPACITY], raw[MAAGE_PATH_CAPACITY];
    maage_folder_link_kind kind = maage_folder_link_status(path);
    if (kind == MAAGE_FOLDER_LINK_LINK) {
        if (!maage_folder_link_canonical(path, actual, sizeof(actual)) ||
            !maage_folder_link_canonical(expected_target, expected, sizeof(expected))) return 0;
    } else if (kind == MAAGE_FOLDER_LINK_BROKEN) {
        if (!maage_link_raw_target(path, raw) || !maage_link_lexical(raw, actual) ||
            !maage_link_lexical(expected_target, expected)) return 0;
    } else return 0;
    return maage_link_contains(actual, expected) && maage_link_contains(expected, actual);
}
static int maage_link_parent(const char *path, char parent[MAAGE_PATH_CAPACITY])
{
    char normalized[MAAGE_PATH_CAPACITY], *separator;
    size_t root = maage_link_absolute(path, normalized);
    if (!root || strlen(normalized) <= root) return 0;
    separator = strrchr(normalized, '/');
    if (!separator) return 0;
    if ((size_t)(separator - normalized) < root) normalized[root] = '\0';
    else *separator = '\0';
    return maage_folder_link_canonical(normalized, parent, MAAGE_PATH_CAPACITY);
}

int maage_folder_link_create(const char *path, const char *target)
{
    char canonical[MAAGE_PATH_CAPACITY], parent[MAAGE_PATH_CAPACITY];
    if (!maage_folder_link_managed_path(path, 0) ||
        maage_folder_link_status(path) != MAAGE_FOLDER_LINK_MISSING ||
        !maage_folder_link_canonical(target, canonical, sizeof(canonical)) ||
        !maage_link_parent(path, parent) || maage_link_contains(canonical, parent)) return 0;
#ifdef _WIN32
    {
        typedef struct maage_mount_point {
            DWORD tag;
            WORD length, reserved, substitute_offset, substitute_length, print_offset, print_length;
            wchar_t paths[1];
        } maage_mount_point;
        wchar_t *destination = maage_link_wide(path), *wide = maage_link_wide(canonical);
        maage_mount_point *point;
        size_t characters, substitute, bytes, index;
        DWORD received;
        HANDLE handle;
        int ok = 0;
        if (!destination || !wide || canonical[1] != ':') { free(destination); free(wide); return 0; }
        {
            wchar_t drive[4];
            drive[0] = wide[0]; drive[1] = L':'; drive[2] = L'\\'; drive[3] = L'\0';
            if (GetDriveTypeW(drive) == DRIVE_REMOTE) { free(destination); free(wide); return 0; }
        }
        characters = wcslen(wide); substitute = characters + 4;
        bytes = 16 + (substitute + 1 + characters + 1) * sizeof(wchar_t);
        if (bytes > MAXIMUM_REPARSE_DATA_BUFFER_SIZE) { free(destination); free(wide); return 0; }
        point = (maage_mount_point *)calloc(1, bytes);
        if (!point) { free(destination); free(wide); return 0; }
        for (index = 0; index < characters; ++index) if (wide[index] == L'/') wide[index] = L'\\';
        point->tag = IO_REPARSE_TAG_MOUNT_POINT; point->length = (WORD)(bytes - 8);
        point->substitute_length = (WORD)(substitute * sizeof(wchar_t));
        point->print_offset = (WORD)((substitute + 1) * sizeof(wchar_t));
        point->print_length = (WORD)(characters * sizeof(wchar_t));
        memcpy(point->paths, L"\\??\\", 4 * sizeof(wchar_t));
        memcpy(point->paths + 4, wide, (characters + 1) * sizeof(wchar_t));
        memcpy(point->paths + substitute + 1, wide, (characters + 1) * sizeof(wchar_t));
        if (CreateDirectoryW(destination, NULL)) {
            handle = CreateFileW(destination, GENERIC_WRITE, 0, NULL, OPEN_EXISTING,
                FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, NULL);
            if (handle != INVALID_HANDLE_VALUE) {
                ok = DeviceIoControl(handle, FSCTL_SET_REPARSE_POINT, point, (DWORD)bytes, NULL, 0, &received, NULL) != 0;
                CloseHandle(handle);
            }
            if (!ok) RemoveDirectoryW(destination);
        }
        free(point); free(destination); free(wide); return ok;
    }
#else
    return symlink(canonical, path) == 0;
#endif
}
int maage_folder_link_remove(const char *path)
{
    maage_folder_link_kind kind;
    if (!maage_folder_link_managed_path(path, 1)) return 0;
    kind = maage_folder_link_status(path);
    if (kind != MAAGE_FOLDER_LINK_LINK && kind != MAAGE_FOLDER_LINK_BROKEN) return 0;
#ifdef _WIN32
    {
        wchar_t *wide = maage_link_wide(path);
        BY_HANDLE_FILE_INFORMATION information;
        HANDLE handle;
        int ok;
        if (!wide) return 0;
        handle = CreateFileW(wide, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            NULL, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, NULL);
        if (handle == INVALID_HANDLE_VALUE) { free(wide); return 0; }
        ok = GetFileInformationByHandle(handle, &information) &&
            (information.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT);
        CloseHandle(handle);
        if (ok) ok = (information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ?
            RemoveDirectoryW(wide) : DeleteFileW(wide);
        free(wide); return ok;
    }
#else
    return unlink(path) == 0;
#endif
}
int maage_folder_link_move(const char *source, const char *destination)
{
    char canonical[MAAGE_PATH_CAPACITY], parent[MAAGE_PATH_CAPACITY];
    if (!maage_folder_link_managed_path(source, 0) || !maage_folder_link_managed_path(destination, 0) ||
        maage_folder_link_status(source) != MAAGE_FOLDER_LINK_DIRECTORY ||
        maage_folder_link_status(destination) != MAAGE_FOLDER_LINK_MISSING ||
        !maage_folder_link_canonical(source, canonical, sizeof(canonical)) ||
        !maage_link_parent(destination, parent) || maage_link_contains(canonical, parent)) return 0;
#ifdef _WIN32
    {
        wchar_t *left = maage_link_wide(source), *right = maage_link_wide(destination);
        DWORD error;
        int ok = left && right && MoveFileExW(left, right, MOVEFILE_WRITE_THROUGH);
        error = GetLastError(); free(left); free(right);
        if (ok) return 1;
        return error == ERROR_NOT_SAME_DEVICE || error == ERROR_NOT_SUPPORTED ||
            error == ERROR_INVALID_FUNCTION ? -1 : 0;
    }
#elif defined(__APPLE__)
    if (!renamex_np(source, destination, RENAME_EXCL)) return 1;
    return errno == EXDEV || errno == ENOTSUP ? -1 : 0;
#elif defined(__linux__) && defined(SYS_renameat2)
    if (!syscall(SYS_renameat2, AT_FDCWD, source, AT_FDCWD, destination, RENAME_NOREPLACE)) return 1;
    return errno == EXDEV || errno == ENOSYS || errno == EINVAL || errno == EOPNOTSUPP ? -1 : 0;
#else
    return -1;
#endif
}

int maage_folder_link_list(const char *path, maage_folder_link_entry entry, void *context)
{
    char *child;
    int ok = 1;
    maage_folder_link_kind kind;
    if (!entry || !maage_folder_link_managed_path(path, 0) ||
        maage_folder_link_status(path) != MAAGE_FOLDER_LINK_DIRECTORY) return 0;
#ifdef _WIN32
    {
        char *pattern, *name;
        wchar_t *wide;
        WIN32_FIND_DATAW information;
        HANDLE search;
        DWORD error;
        child = (char *)malloc(MAAGE_PATH_CAPACITY * 3U);
        if (!child) return 0;
        pattern = child + MAAGE_PATH_CAPACITY; name = pattern + MAAGE_PATH_CAPACITY;
        if (!maage_storage_join(pattern, MAAGE_PATH_CAPACITY, path, "*")) { free(child); return 0; }
        wide = maage_link_wide(pattern);
        search = wide ? FindFirstFileW(wide, &information) : INVALID_HANDLE_VALUE;
        error = GetLastError(); free(wide);
        if (search == INVALID_HANDLE_VALUE) { free(child); return error == ERROR_FILE_NOT_FOUND; }
        do {
            if (!wcscmp(information.cFileName, L".") || !wcscmp(information.cFileName, L"..")) continue;
            if (!WideCharToMultiByte(CP_UTF8, 0, information.cFileName, -1, name, MAAGE_PATH_CAPACITY, NULL, NULL) ||
                !maage_storage_join(child, MAAGE_PATH_CAPACITY, path, name)) { ok = 0; break; }
            kind = maage_folder_link_status(child);
            if (kind == MAAGE_FOLDER_LINK_ERROR || !entry(name, kind, context)) { ok = 0; break; }
        } while (FindNextFileW(search, &information));
        if (ok && GetLastError() != ERROR_NO_MORE_FILES) ok = 0;
        FindClose(search);
    }
#else
    {
        DIR *directory = opendir(path);
        struct dirent *information;
        if (!directory) return 0;
        child = (char *)malloc(MAAGE_PATH_CAPACITY);
        if (!child) { closedir(directory); return 0; }
        for (;;) {
            errno = 0; information = readdir(directory);
            if (!information) { if (errno) ok = 0; break; }
            if (!strcmp(information->d_name, ".") || !strcmp(information->d_name, "..")) continue;
            if (!maage_storage_join(child, MAAGE_PATH_CAPACITY, path, information->d_name)) { ok = 0; break; }
            kind = maage_folder_link_status(child);
            if (kind == MAAGE_FOLDER_LINK_ERROR || !entry(information->d_name, kind, context)) { ok = 0; break; }
        }
        if (closedir(directory)) ok = 0;
    }
#endif
    free(child); return ok;
}

#define MAAGE_LINK_TREE_DEPTH 64U
#define MAAGE_LINK_TREE_ENTRIES 1000000UL
#define MAAGE_LINK_COPY_BUFFER 16384U
typedef struct maage_link_tree_work {
    unsigned long entries;
    unsigned char *buffers;
    int copying;
} maage_link_tree_work;
typedef struct maage_link_tree_level {
    maage_link_tree_work *work;
    const char *left, *right;
    unsigned depth;
    unsigned long count;
} maage_link_tree_level;

static int maage_link_file_equal(const char *left, const char *right,
    maage_link_tree_work *work)
{
    FILE *a, *b;
    char before_a[128], before_b[128], after_a[128], after_b[128];
    double size_a, size_b;
    size_t count_a, count_b;
    int ok = 1;
    if (!maage_storage_stat(left, &size_a, before_a) ||
        !maage_storage_stat(right, &size_b, before_b) || size_a != size_b) return 0;
    a = maage_storage_open(left, "rb"); b = maage_storage_open(right, "rb");
    if (!a || !b) { if (a) fclose(a); if (b) fclose(b); return 0; }
    do {
        count_a = fread(work->buffers, 1, MAAGE_LINK_COPY_BUFFER, a);
        count_b = fread(work->buffers + MAAGE_LINK_COPY_BUFFER, 1, MAAGE_LINK_COPY_BUFFER, b);
        if (count_a != count_b || memcmp(work->buffers, work->buffers + MAAGE_LINK_COPY_BUFFER, count_a)) {
            ok = 0; break;
        }
    } while (count_a);
    if (ferror(a) || ferror(b)) ok = 0;
    if (fclose(a)) ok = 0;
    if (fclose(b)) ok = 0;
    if (!maage_storage_stat(left, &size_a, after_a) ||
        !maage_storage_stat(right, &size_b, after_b) ||
        strcmp(before_a, after_a) || strcmp(before_b, after_b)) ok = 0;
    return ok;
}
static int maage_link_copy_file(const char *source, const char *destination,
    maage_link_tree_work *work)
{
    FILE *input;
    size_t count;
    int ok = 1;
    char before[128], after[128];
    double size;
#ifdef _WIN32
    wchar_t *wide;
    HANDLE output;
    DWORD written;
    size_t offset;
#else
    int descriptor;
    FILE *output;
#endif
    if (!maage_storage_stat(source, &size, before)) return 0;
    input = maage_storage_open(source, "rb");
    if (!input) return 0;
#ifdef _WIN32
    wide = maage_link_wide(destination);
    output = wide ? CreateFileW(wide, GENERIC_WRITE, 0, NULL, CREATE_NEW,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, NULL) : INVALID_HANDLE_VALUE;
    free(wide);
    if (output == INVALID_HANDLE_VALUE) { fclose(input); return 0; }
#else
    descriptor = open(destination, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
    output = descriptor >= 0 ? fdopen(descriptor, "wb") : NULL;
    if (!output) { if (descriptor >= 0) close(descriptor); fclose(input); return 0; }
#endif
    while ((count = fread(work->buffers, 1, MAAGE_LINK_COPY_BUFFER, input)) != 0) {
#ifdef _WIN32
        offset = 0;
        while (offset < count) {
            if (!WriteFile(output, work->buffers + offset, (DWORD)(count - offset), &written, NULL) || !written) {
                ok = 0; break;
            }
            offset += written;
        }
        if (!ok) break;
#else
        if (fwrite(work->buffers, 1, count, output) != count) { ok = 0; break; }
#endif
    }
    if (ferror(input)) ok = 0;
    if (fclose(input)) ok = 0;
#ifdef _WIN32
    if (!FlushFileBuffers(output)) ok = 0;
    if (!CloseHandle(output)) ok = 0;
    if (!maage_storage_private_file(destination)) ok = 0;
#else
    if (!maage_storage_sync(output)) ok = 0;
    if (fclose(output)) ok = 0;
#endif
    return ok && maage_storage_stat(source, &size, after) && !strcmp(before, after) &&
        maage_link_file_equal(source, destination, work);
}
static int maage_link_new_directory(const char *path)
{
#ifdef _WIN32
    wchar_t *wide = maage_link_wide(path);
    int ok = wide && CreateDirectoryW(wide, NULL);
    free(wide); return ok;
#else
    return mkdir(path, 0700) == 0;
#endif
}
static int maage_link_tree_directory(const char *left, const char *right,
    unsigned depth, maage_link_tree_work *work);
static int maage_link_tree_entry(const char *name, maage_folder_link_kind kind,
    void *context)
{
    maage_link_tree_level *level = (maage_link_tree_level *)context;
    char *left, *right;
    int ok;
    if (++level->work->entries > MAAGE_LINK_TREE_ENTRIES ||
        (kind != MAAGE_FOLDER_LINK_FILE && kind != MAAGE_FOLDER_LINK_DIRECTORY)) return 0;
    left = (char *)malloc(MAAGE_PATH_CAPACITY * 2U);
    if (!left) return 0;
    right = left + MAAGE_PATH_CAPACITY;
    if (!maage_storage_join(left, MAAGE_PATH_CAPACITY, level->left, name) ||
        !maage_storage_join(right, MAAGE_PATH_CAPACITY, level->right, name)) { free(left); return 0; }
    ++level->count;
    if (!level->work->copying && maage_folder_link_status(right) != kind) { free(left); return 0; }
    if (kind == MAAGE_FOLDER_LINK_DIRECTORY)
        ok = maage_link_tree_directory(left, right, level->depth + 1, level->work);
    else ok = level->work->copying ? maage_link_copy_file(left, right, level->work) :
        maage_link_file_equal(left, right, level->work);
    free(left); return ok;
}
static int maage_link_count_entry(const char *name, maage_folder_link_kind kind,
    void *context)
{
    unsigned long *count = (unsigned long *)context;
    (void)name;
    if (kind != MAAGE_FOLDER_LINK_FILE && kind != MAAGE_FOLDER_LINK_DIRECTORY) return 0;
    return ++*count <= MAAGE_LINK_TREE_ENTRIES;
}
static int maage_link_tree_directory(const char *left, const char *right,
    unsigned depth, maage_link_tree_work *work)
{
    maage_link_tree_level level;
    unsigned long right_count = 0;
    if (depth > MAAGE_LINK_TREE_DEPTH || maage_folder_link_status(left) != MAAGE_FOLDER_LINK_DIRECTORY) return 0;
    if (work->copying) {
        if (!maage_link_new_directory(right)) return 0;
    } else if (maage_folder_link_status(right) != MAAGE_FOLDER_LINK_DIRECTORY) return 0;
    level.work = work; level.left = left; level.right = right;
    level.depth = depth; level.count = 0;
    if (!maage_folder_link_list(left, maage_link_tree_entry, &level)) return 0;
    if (!work->copying && (!maage_folder_link_list(right, maage_link_count_entry, &right_count) ||
        level.count != right_count)) return 0;
    return 1;
}
int maage_folder_link_equal_tree(const char *left, const char *right)
{
    maage_link_tree_work work;
    int ok;
    if (!maage_folder_link_managed_path(left, 0) || !maage_folder_link_managed_path(right, 0)) return 0;
    memset(&work, 0, sizeof(work));
    work.buffers = (unsigned char *)malloc(MAAGE_LINK_COPY_BUFFER * 2U);
    if (!work.buffers) return 0;
    ok = maage_link_tree_directory(left, right, 0, &work);
    free(work.buffers); return ok;
}
int maage_folder_link_copy_verified(const char *source, const char *destination)
{
    maage_link_tree_work work;
    char canonical[MAAGE_PATH_CAPACITY], parent[MAAGE_PATH_CAPACITY];
    int ok;
    if (!maage_folder_link_managed_path(source, 0) || !maage_folder_link_managed_path(destination, 0) ||
        maage_folder_link_status(destination) != MAAGE_FOLDER_LINK_MISSING ||
        !maage_folder_link_canonical(source, canonical, sizeof(canonical)) ||
        !maage_link_parent(destination, parent) || maage_link_contains(canonical, parent)) return 0;
    memset(&work, 0, sizeof(work)); work.copying = 1;
    work.buffers = (unsigned char *)malloc(MAAGE_LINK_COPY_BUFFER * 2U);
    if (!work.buffers) return 0;
    ok = maage_link_tree_directory(source, destination, 0, &work);
    if (ok) {
        work.copying = 0; work.entries = 0;
        ok = maage_link_tree_directory(source, destination, 0, &work);
    }
    free(work.buffers); return ok;
}
