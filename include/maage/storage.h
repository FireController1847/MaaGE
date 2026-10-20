#ifndef MAAGE_STORAGE_H
#define MAAGE_STORAGE_H
#include <stddef.h>
#include <stdio.h>
#define MAAGE_PATH_CAPACITY 4096
/* UTF-8 paths. Returned application-data path is absolute; no directories created. */
int maage_storage_data_path(char *path, size_t capacity);
int maage_storage_join(char *path, size_t capacity, const char *base, const char *relative);
/* Creates owner-private directories; refuses symlinks/reparse points. */
int maage_storage_directories(const char *path);
int maage_storage_directory_exists(const char *path);
/* Binary read ("rb") or binary replacement ("wb"). */
FILE *maage_storage_open(const char *path, const char *mode);
/* No-follow status: 0 missing, 1 regular file, 2 directory, -1 unsafe/error. */
int maage_storage_status(const char *path);
/* Cryptographic OS-backed randomness; no application/credential dependency. */
int maage_storage_random(unsigned char *bytes, size_t count);
/* Canonical lowercase, hyphenated, nonzero UUIDs. Generator uses OS randomness
 * and emits RFC UUID version 4 with the standard variant bits. */
int maage_storage_uuid_valid(const char *uuid);
int maage_storage_uuid_generate(char uuid[37]);
/* Flush stdio and the underlying regular file before atomic promotion. */
int maage_storage_sync(FILE *file);
/* Restricts an existing regular file to the current user (0600 / protected DACL). */
int maage_storage_private_file(const char *path);
int maage_storage_remove(const char *path);
int maage_storage_replace(const char *temporary, const char *destination);
/* Link a verified source to a new staging path, or copy if links are unavailable.
 * Returns 1 for a hard link, 2 for a copy, zero on failure/cancellation. Caller
 * verifies staged content and atomically promotes it with storage_replace.
 * Staging path must be private to the caller; any abandoned file is removed. */
int maage_storage_clone(const char *source, const char *staging,
    int (*cancel)(void *), void *context);
int maage_storage_same_file(const char *left, const char *right);
/* Removes only empty directories; never removes their contents. */
void maage_storage_remove_directory(const char *path);
/* Regular files only. Fingerprints detect size/time changes, not malicious tampering. */
int maage_storage_stat(const char *path, double *size, char fingerprint[128]);
/* SHA-1 file hashing requires the optional MaaGE::StorageHash target, enabled
 * for Launcher acquisition. Generic Storage has no OpenSSL dependency. */
int maage_storage_sha1(const char *path, char hash[41], int (*cancel)(void *), void *context);
typedef struct maage_storage_lock maage_storage_lock;
/* Exclusive writer ownership of the lock object. Reads and directory moves may
 * proceed while held; callers serialize changes to its containing namespace. */
maage_storage_lock *maage_storage_acquire(const char *path);
void maage_storage_release(maage_storage_lock *lock);
#endif
