#ifndef MAAGE_FOLDER_LINK_H
#define MAAGE_FOLDER_LINK_H
#include "maage/storage.h"

typedef enum maage_folder_link_kind {
    MAAGE_FOLDER_LINK_ERROR = -1,
    MAAGE_FOLDER_LINK_MISSING = 0,
    MAAGE_FOLDER_LINK_FILE = 1,
    MAAGE_FOLDER_LINK_DIRECTORY = 2,
    MAAGE_FOLDER_LINK_LINK = 3,
    MAAGE_FOLDER_LINK_BROKEN = 4
} maage_folder_link_kind;

/* No-follow leaf status. Unknown reparse objects and special files are errors. */
maage_folder_link_kind maage_folder_link_status(const char *path);
/* Existing directory, canonical absolute UTF-8 path; loops/broken targets fail. */
maage_status maage_folder_link_canonical(const char *path, char *resolved, size_t capacity);
/* Absolute directory path without dot components or linked parents. A missing
 * suffix is permitted for creation. Only allow_leaf_link admits a linked leaf. */
int maage_folder_link_managed_path(const char *path, int allow_leaf_link);
/* Canonical existing-directory ancestry; equal directories also return true. */
int maage_folder_link_within(const char *root, const char *path);
/* Absolute lexical ancestry without filesystem probes; rejects dot components
 * and normalizes native separators. Suitable for cached/missing paths. */
int maage_folder_link_path_within(const char *root, const char *path);
int maage_folder_link_matches(const char *path, const char *expected_target);
/* Caller serializes changes to its managed directory. Never replaces an
 * existing entry. Target must exist; ancestor/self cycles are rejected. */
maage_status maage_folder_link_create(const char *path, const char *target);
/* Removes only a recognized link (including a broken link), never its target. */
maage_status maage_folder_link_remove(const char *path);
/* Atomic directory move without replacement; no cross-filesystem copy fallback.
 * SUCCESS on completion; NOT_IMPLEMENTED for unavailable atomic primitives. */
maage_status maage_folder_link_move(const char *source, const char *destination);
/* Names contain one basename, never a relative path. Returning zero from the
 * callback stops enumeration and makes list report INTERRUPTED; entries are not followed. */
typedef int (*maage_folder_link_entry)(const char *name, maage_folder_link_kind kind, void *context);
maage_status maage_folder_link_list(const char *path, maage_folder_link_entry entry, void *context);
#endif
