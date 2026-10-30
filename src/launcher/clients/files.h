#ifndef MAAGE_CLIENT_FILES_H
#define MAAGE_CLIENT_FILES_H
#include "maage/storage.h"
/* Import only the manifest and verified executable from the selected directory.
 * Never traverse a developer source directory. Managed-tree removal remains bounded. */
int maage_client_file_kind(const char *path); /* 1 regular, 2 directory, 3 POSIX link, 0 absent/unsafe */
int maage_client_executable_file(const char *path);
maage_status maage_client_absolute(const char *path, char out[MAAGE_PATH_CAPACITY], maage_status_trace *trace);
maage_status maage_client_copy_package(const char *source, const char *destination, const char *executable, maage_status_trace *trace);
maage_status maage_client_remove_tree(const char *path, maage_status_trace *trace);
maage_status maage_client_sha256(const char *path, char hash[65], maage_status_trace *trace);
#endif
