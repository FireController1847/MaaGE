#include "log_internal.h"
#include "maage/gzip.h"
#include <errno.h>
#include <string.h>

static int maage_log_archive_equal(FILE *left, FILE *right) {
    unsigned char a[4096], b[4096];
    size_t na, nb;
    do {
        na = fread(a, 1, sizeof(a), left);
        nb = fread(b, 1, sizeof(b), right);
        if (ferror(left) || ferror(right)) return -1;
        if (na != nb || memcmp(a, b, na)) return 0;
    } while (na);
    return 1;
}

static maage_status maage_log_archive_problem(maage_status_trace *trace, unsigned char operation) {
    int native = errno;
    maage_status result = native ? maage_status_from_errno(native, MAAGE_CONTEXT_CORE, operation) : maage_status_make(MAAGE_STATUS_IO_ERROR, MAAGE_CONTEXT_CORE, operation, 0);
    return maage_status_trace_record(trace, result, MAAGE_CONTEXT_CORE, operation,
                                     __FILE__, (unsigned long) __LINE__, (long) native);
}
maage_status maage_log_rotate(const char *path, int closing, maage_status_trace *trace) {
    char archive[MAAGE_LOG_PATH_SIZE + 64], temporary[MAAGE_LOG_PATH_SIZE + 64], timestamp[32];
    const char *extension, *separator;
    FILE *input = NULL, *output = NULL, *existing, *candidate;
    size_t stem;
    unsigned int sequence;
    int native, equal, created = 0;
    maage_status result, cleanup;
    errno = 0;
    native = maage_log_archive_time(path, closing, timestamp);
    if (!native) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_COMPRESS, 0);
    if (native < 0) return maage_log_archive_problem(trace, MAAGE_OPERATION_QUERY);
    input = maage_log_open(path, "rb");
    if (!input) return maage_log_archive_problem(trace, MAAGE_OPERATION_OPEN);
    result = maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_CORE, MAAGE_OPERATION_CREATE, 0);
    for (sequence = 0; sequence <= 99999; ++sequence) {
        sprintf(temporary, "%s.archive.%u.tmp", path, sequence);
        output = maage_log_archive_create(temporary);
        if (output) {
            created = 1;
            break;
        }
        if (errno != EEXIST) {
            result = maage_log_archive_problem(trace, MAAGE_OPERATION_CREATE);
            goto done;
        }
    }
    if (!output) goto done;
    result = maage_gzip_compress(input, output, trace);
    if (fclose(input) && !maage_status_failed(result)) result = maage_log_archive_problem(trace, MAAGE_OPERATION_CLOSE);
    input = NULL;
    if (!maage_status_failed(result) && !maage_log_archive_sync(output))
        result = maage_log_archive_problem(trace, MAAGE_OPERATION_WRITE);
    if (fclose(output) && !maage_status_failed(result)) result = maage_log_archive_problem(trace, MAAGE_OPERATION_CLOSE);
    output = NULL;
    if (maage_status_failed(result)) goto done;
    extension = strrchr(path, '.');
    separator = strrchr(path, '/');
#ifdef _WIN32
    {
        const char *backslash = strrchr(path, '\\');
        if (backslash && (!separator || backslash > separator)) separator = backslash;
    }
#endif
    if (!extension || (separator && extension < separator)) extension = path + strlen(path);
    stem = (size_t) (extension - path);
    result = maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_CORE, MAAGE_OPERATION_RENAME, 0);
    for (sequence = 0; sequence <= 99999; ++sequence) {
        if (!sequence)
            sprintf(archive, "%.*s.%s%s.gz", (int) stem, path, timestamp, extension);
        else
            sprintf(archive, "%.*s.%s.%u%s.gz", (int) stem, path, timestamp, sequence, extension);
        existing = maage_log_open(archive, "rb");
        if (existing) {
            candidate = maage_log_open(temporary, "rb");
            equal = candidate ? maage_log_archive_equal(candidate, existing) : -1;
            if (candidate && fclose(candidate)) equal = -1;
            if (fclose(existing)) equal = -1;
            if (equal < 0) {
                result = maage_log_archive_problem(trace, MAAGE_OPERATION_VERIFY);
                goto done;
            }
            if (equal) {
                result = maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_COMPRESS, 0);
                goto done;
            }
            continue;
        }
        if (errno != ENOENT) {
            result = maage_log_archive_problem(trace, MAAGE_OPERATION_OPEN);
            goto done;
        }
        native = maage_log_archive_publish(temporary, archive);
        if (native > 0) {
            created = 0;
            result = maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_COMPRESS, 0);
            break;
        }
        if (native < 0) {
            result = maage_log_archive_problem(trace, MAAGE_OPERATION_RENAME);
            goto done;
        }
    }
done:
    if (input && fclose(input)) {
        cleanup = maage_log_archive_problem(trace, MAAGE_OPERATION_CLOSE);
        if (!maage_status_failed(result)) result = cleanup;
    }
    if (output && fclose(output)) {
        cleanup = maage_log_archive_problem(trace, MAAGE_OPERATION_CLOSE);
        if (!maage_status_failed(result)) result = cleanup;
    }
    if (created && !maage_log_archive_remove(temporary)) {
        cleanup = maage_log_archive_problem(trace, MAAGE_OPERATION_REMOVE);
        if (!maage_status_failed(result)) result = cleanup;
    }
    return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_CORE, MAAGE_OPERATION_COMPRESS);
}
