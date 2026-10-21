#include "log_internal.h"
#include "maage/gzip.h"
#include <errno.h>
#include <string.h>

static int maage_log_archive_equal(FILE *left, FILE *right)
{
    unsigned char a[4096], b[4096];
    size_t na, nb;
    do {
        na = fread(a, 1, sizeof(a), left); nb = fread(b, 1, sizeof(b), right);
        if (ferror(left) || ferror(right)) return -1;
        if (na != nb || memcmp(a, b, na)) return 0;
    } while (na);
    return 1;
}

int maage_log_rotate(const char *path, int closing)
{
    char archive[MAAGE_LOG_PATH_SIZE + 64], temporary[MAAGE_LOG_PATH_SIZE + 64], timestamp[32];
    const char *extension, *separator;
    FILE *input = NULL, *output = NULL, *existing, *candidate;
    size_t stem;
    unsigned int sequence;
    int result, equal, created = 0, ok = 0;
    result = maage_log_archive_time(path, closing, timestamp);
    if (result <= 0) return result == 0;
    input = maage_log_open(path, "rb");
    if (!input) return 0;
    for (sequence = 0; sequence <= 99999; ++sequence) {
        sprintf(temporary, "%s.archive.%u.tmp", path, sequence);
        output = maage_log_archive_create(temporary);
        if (output) { created = 1; break; }
        if (errno != EEXIST) goto done;
    }
    if (!output) goto done;
    ok = maage_gzip_compress(input, output);
    if (fclose(input)) ok = 0;
    input = NULL;
    if (ok && !maage_log_archive_sync(output)) ok = 0;
    if (fclose(output)) ok = 0;
    output = NULL;
    if (!ok) goto done;
    ok = 0;
    extension = strrchr(path, '.'); separator = strrchr(path, '/');
#ifdef _WIN32
    {
        const char *backslash = strrchr(path, '\\');
        if (backslash && (!separator || backslash > separator)) separator = backslash;
    }
#endif
    if (!extension || (separator && extension < separator)) extension = path + strlen(path);
    stem = (size_t)(extension - path);
    for (sequence = 0; sequence <= 99999; ++sequence) {
        if (!sequence) sprintf(archive, "%.*s.%s%s.gz", (int)stem, path, timestamp, extension);
        else sprintf(archive, "%.*s.%s.%u%s.gz", (int)stem, path, timestamp, sequence, extension);
        existing = maage_log_open(archive, "rb");
        if (existing) {
            candidate = maage_log_open(temporary, "rb");
            equal = candidate ? maage_log_archive_equal(candidate, existing) : -1;
            if (candidate && fclose(candidate)) equal = -1;
            if (fclose(existing)) equal = -1;
            if (equal < 0) goto done;
            if (equal) { ok = 1; goto done; }
            continue;
        }
        if (errno != ENOENT) goto done;
        result = maage_log_archive_publish(temporary, archive);
        if (result > 0) { created = 0; ok = 1; break; }
        if (result < 0) goto done;
    }
done:
    if (input) fclose(input);
    if (output) fclose(output);
    if (created && !maage_log_archive_remove(temporary)) ok = 0;
    return ok;
}
