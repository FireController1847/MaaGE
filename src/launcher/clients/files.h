#ifndef MAAGE_CLIENT_FILES_H
#define MAAGE_CLIENT_FILES_H
#include "maage/storage.h"
/* Launcher-private package I/O. Only relative links resolving within the package are copied on POSIX;
 * reject Windows reparse points and special files.
 * Trees are bounded in depth, entry count and total bytes. Never executes code. */
#ifdef __APPLE__
int maage_client_bundle_identity(const char *path, const char *executable, const char *version, const char *build);
#endif
int maage_client_file_kind(const char *path); /* 1 regular, 2 directory, 3 POSIX link, 0 absent/unsafe */
int maage_client_executable_file(const char *path);
int maage_client_absolute(const char *path, char out[MAAGE_PATH_CAPACITY]);
int maage_client_copy_tree(const char *source, const char *destination);
int maage_client_remove_tree(const char *path);
int maage_client_sha256(const char *path, char hash[65]);
#endif
