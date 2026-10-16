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
/* Binary read ("rb") or binary replacement ("wb"). */
FILE *maage_storage_open(const char *path, const char *mode);
int maage_storage_remove(const char *path);
int maage_storage_replace(const char *temporary, const char *destination);
/* Regular files only. Fingerprints detect size/time changes, not malicious tampering. */
int maage_storage_stat(const char *path, double *size, char fingerprint[128]);
int maage_storage_sha1(const char *path, char hash[41], int (*cancel)(void *), void *context);
typedef struct maage_storage_lock maage_storage_lock;
maage_storage_lock *maage_storage_acquire(const char *path);
void maage_storage_release(maage_storage_lock *lock);
#endif
