#include "maage/minecraft_assets.h"
#include "maage/storage.h"
#include <string.h>
#include <ctype.h>
static int maage_minecraft_name_valid(const char *name)
{
    const char *start, *end, *p;
    size_t length, stem;
    char device[8];
    if (!name || !*name || strlen(name) > 1024) return 0;
    start = name;
    while (*start) {
        end = strchr(start, '/');
        if (!end) end = start + strlen(start);
        length = (size_t)(end - start);
        if (!length || length > 255 || start[length - 1] == '.' || start[length - 1] == ' ') return 0;
        for (p = start; p < end; ++p)
            if ((unsigned char)*p < 32 || strchr("\\:<>\"|?*", *p)) return 0;
        /* Windows device aliases and trailing-dot normalization must never turn
         * a logical name into an OS device or a different path component. */
        stem = 0;
        while (stem < length && start[stem] != '.') ++stem;
        if (stem == 3 || stem == 4 || stem == 6 || stem == 7) {
            size_t i;
            for (i = 0; i < stem; ++i) device[i] = (char)toupper((unsigned char)start[i]);
            device[stem] = 0;
            if (!strcmp(device, "CON") || !strcmp(device, "PRN") || !strcmp(device, "AUX") || !strcmp(device, "NUL") || !strcmp(device, "CONIN$") || !strcmp(device, "CONOUT$") ||
                (stem == 4 && (!strncmp(device, "COM", 3) || !strncmp(device, "LPT", 3)) && device[3] >= '1' && device[3] <= '9')) return 0;
        }
        if (!*end) return 1;
        start = end + 1;
        if (!*start) return 0;
    }
    return 0;
}
maage_status maage_minecraft_asset_path_status(const char *application_data, const char *name, char *path, size_t capacity)
{
    char root[MAAGE_PATH_CAPACITY], reconst[MAAGE_PATH_CAPACITY];
    if (!maage_minecraft_name_valid(name) || !path || !capacity)
        return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_LOOKUP,
            (unsigned char)(!maage_minecraft_name_valid(name) ? 1 : (!path ? 2 : 3)));
    if (!application_data) {
        if (!maage_storage_data_path(root, sizeof(root)))
            return maage_status_make(MAAGE_STATUS_IO_ERROR, MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_LOOKUP, 0);
        application_data = root;
    }
#ifdef _WIN32
    if (strlen(application_data) < 3 || application_data[1] != ':' ||
        (application_data[2] != '/' && application_data[2] != '\\'))
        return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_LOOKUP, 0);
#else
    if (application_data[0] != '/')
        return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_LOOKUP, 0);
#endif
    if (!maage_storage_join(reconst, sizeof(reconst), application_data, "minecraft/assets/reconst") ||
        !maage_storage_join(path, capacity, reconst, name))
        return maage_status_make(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_LOOKUP, 0);
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_LOOKUP, 0);
}
int maage_minecraft_asset_path(const char *application_data, const char *name, char *path, size_t capacity)
{
    return !maage_status_failed(maage_minecraft_asset_path_status(application_data, name, path, capacity));
}
