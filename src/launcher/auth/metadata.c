#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#elif !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif
#include "internal.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <wchar.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
static int maage_auth_metadata_path(wchar_t path[4096])
{
    if (SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA | CSIDL_FLAG_CREATE, NULL, 0, path) != S_OK) return 0;
    wcscat(path, L"\\MaaGE");
    if (!CreateDirectoryW(path, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) return 0;
    wcscat(path, L"\\accounts.json");
    return 1;
}
int maage_auth_metadata_read(char **output)
{
    wchar_t path[4096]; HANDLE file; LARGE_INTEGER size; DWORD received;
    *output = NULL;
    if (!maage_auth_metadata_path(path)) return -1;
    file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return GetLastError() == ERROR_FILE_NOT_FOUND ? 0 : -1;
    if (!GetFileSizeEx(file, &size) || size.HighPart || size.LowPart > MAAGE_AUTH_RESPONSE_LIMIT) { CloseHandle(file); return -1; }
    *output = malloc((size_t)size.LowPart + 1);
    if (!*output || !ReadFile(file, *output, size.LowPart, &received, NULL) || received != size.LowPart) { free(*output); *output = NULL; CloseHandle(file); return -1; }
    if (memchr(*output, 0, received)) { free(*output); *output = NULL; CloseHandle(file); return -1; }
    (*output)[received] = '\0'; CloseHandle(file);
    return 1;
}
int maage_auth_metadata_write(const char *data)
{
    wchar_t path[4096], temporary[4096], suffix[40]; HANDLE file; DWORD written;
    unsigned char random[8];
    size_t length = strlen(data); int success;
    if (length > MAAGE_AUTH_RESPONSE_LIMIT || !maage_auth_metadata_path(path)) return 0;
    if (!maage_auth_random(random, sizeof(random))) return 0;
    swprintf(suffix, 40, L".%02x%02x%02x%02x%02x%02x%02x%02x.tmp",
        random[0], random[1], random[2], random[3], random[4], random[5], random[6], random[7]);
    wcscpy(temporary, path); wcscat(temporary, suffix);
    file = CreateFileW(temporary, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return 0;
    success = WriteFile(file, data, (DWORD)length, &written, NULL) && written == length && FlushFileBuffers(file);
    CloseHandle(file);
    if (success) success = MoveFileExW(temporary, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
    if (!success) DeleteFileW(temporary);
    return success;
}
#else
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
static int maage_auth_metadata_path(char path[4096])
{
    const char *home = getenv("HOME");
    size_t index;
    int written;
    struct stat info;
#ifdef __APPLE__
    if (!home) return 0;
    written = snprintf(path, 4096, "%s/Library/Application Support/MaaGE", home);
#else
    const char *root = getenv("XDG_DATA_HOME");
    if (root && root[0] == '/') written = snprintf(path, 4096, "%s/maage", root);
    else if (home) written = snprintf(path, 4096, "%s/.local/share/maage", home);
    else return 0;
#endif
    if (written <= 0 || written > 4000 || path[0] != '/') return 0;
    for (index = 1; path[index]; ++index) if (path[index] == '/') {
        path[index] = '\0';
        if (mkdir(path, 0700) && errno != EEXIST) return 0;
        path[index] = '/';
    }
    if (mkdir(path, 0700) && errno != EEXIST) return 0;
    if (lstat(path, &info) || !S_ISDIR(info.st_mode) || info.st_uid != geteuid() || (info.st_mode & 0022)) return 0;
    strcat(path, "/accounts.json");
    return 1;
}
int maage_auth_metadata_read(char **output)
{
    char path[4096]; int file; struct stat info; size_t offset = 0; ssize_t count;
    *output = NULL;
    if (!maage_auth_metadata_path(path)) return -1;
    file = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (file < 0) return errno == ENOENT ? 0 : -1;
    if (fstat(file, &info) || !S_ISREG(info.st_mode) || info.st_uid != geteuid() || info.st_size < 0 || (double)info.st_size > MAAGE_AUTH_RESPONSE_LIMIT) { close(file); return -1; }
    *output = malloc((size_t)info.st_size + 1);
    if (!*output) { close(file); return -1; }
    while (offset < (size_t)info.st_size) {
        count = read(file, *output + offset, (size_t)info.st_size - offset);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) break;
        offset += (size_t)count;
    }
    close(file);
    if (offset != (size_t)info.st_size || memchr(*output, 0, offset)) { free(*output); *output = NULL; return -1; }
    (*output)[offset] = '\0';
    return 1;
}
int maage_auth_metadata_write(const char *data)
{
    char path[4096], temporary[4096]; int file, success;
    size_t offset = 0, size = strlen(data); ssize_t count;
    if (size > MAAGE_AUTH_RESPONSE_LIMIT || !maage_auth_metadata_path(path)) return 0;
    snprintf(temporary, sizeof(temporary), "%s.XXXXXX", path);
    file = mkstemp(temporary);
    if (file >= 0) fcntl(file, F_SETFD, FD_CLOEXEC);
    if (file < 0) return 0;
    while (offset < size) {
        count = write(file, data + offset, size - offset);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) break;
        offset += (size_t)count;
    }
    success = offset == size && fsync(file) == 0;
    if (close(file)) success = 0;
    if (success) success = rename(temporary, path) == 0;
    if (!success) unlink(temporary);
    return success;
}
#endif
