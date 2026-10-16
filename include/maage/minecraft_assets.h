#ifndef MAAGE_MINECRAFT_ASSETS_H
#define MAAGE_MINECRAFT_ASSETS_H
#include <stddef.h>
/* Resolve an indexed or client-JAR Minecraft 1.7.10 logical resource name beneath
 * reconstructed assets. JAR names omit only assets/, preserving minecraft/ and other namespaces.
 * application_data is an absolute MaaGE data directory, or NULL for the platform
 * default. Writes a UTF-8 path into caller-owned storage. Returns zero for unsafe
 * names, invalid roots, or insufficient capacity. Does not create files, check
 * installation integrity, or require Launcher/UI/networking initialization. */
int maage_minecraft_asset_path(const char *application_data, const char *name,
    char *path, size_t capacity);
#endif
