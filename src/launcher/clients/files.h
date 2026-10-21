#ifndef MAAGE_CLIENT_FILES_H
#define MAAGE_CLIENT_FILES_H
#include "maage/storage.h"
/* Launcher-private package I/O. Only relative links resolving within the package are copied on POSIX;
 * reject Windows reparse points and special files.
 * Trees are bounded in depth, entry count and total bytes. Never executes code. */
int maage_client_file_kind(const char *path); /* 1 regular, 2 directory, 3 POSIX link, 0 absent/unsafe */
int maage_client_executable_file(const char *path);
maage_status maage_client_absolute(const char *path, char out[MAAGE_PATH_CAPACITY], maage_status_trace *trace);
maage_status maage_client_copy_tree(const char *source, const char *destination, maage_status_trace *trace);
maage_status maage_client_remove_tree(const char *path, maage_status_trace *trace);
maage_status maage_client_sha256(const char *path, char hash[65], maage_status_trace *trace);
#endif
