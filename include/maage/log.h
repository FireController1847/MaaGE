#ifndef MAAGE_LOG_H
#define MAAGE_LOG_H

#include <stdarg.h>
#include "maage/status.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum maage_log_level {
    MAAGE_LOG_VERBOSE, MAAGE_LOG_INFO, MAAGE_LOG_WARNING,
    MAAGE_LOG_ERROR, MAAGE_LOG_FATAL, MAAGE_LOG_OFF
} maage_log_level;

typedef struct maage_log_config {
    maage_log_level level;
    int console;
    int file;
    int timestamp;
    const char *file_path;
    const char *config_path;
    int capture_stdio;
} maage_log_config;

/* Defaults: Debug VERBOSE, other build configurations INFO; console enabled,
 * file disabled, local timestamps enabled. Thresholds include the selected
 * level and every more severe level. Build classification is compiled into Log.
 * init copies strings; caller storage need only survive the call. Precedence:
 * supplied defaults, optional INI, then recognized --log-* arguments. Missing
 * INI files are optional; malformed options return PARTIAL with usable settings retained.
 * With file output enabled, the configured filename is live while running.
 * Shutdown/reinit creates a dated .gz copy of a nonempty session while retaining
 * the latest plain log. Empty files create no archive. Init preserves unarchived
 * previous output before resetting the plain file; identical compressed copies
 * are reused. Archives are staged, synced and published without overwriting
 * history; failures retain the plain log. The filename uses local close time
 * (previous last-write time for recovery), with a suffix only on collision.
 * capture_stdio defaults to zero. With file output enabled it redirects native
 * stdout/stderr and their C streams to the same append-only file, suppressing
 * duplicate console records. This process-wide redirection survives shutdown;
 * failed redirection retains the previous destinations. Set stdout/stderr
 * unbuffered before their first I/O when immediate raw output is required.
 * File records are flushed immediately and the file permits external sharing.
 * Unknown application arguments are ignored and argv is never modified. */
maage_status maage_log_config_init(maage_log_config *config);
maage_status maage_log_init(const maage_log_config *config, int argc, char *const argv[]);
/* Exact logger overrides inherit at dot boundaries; NULL/empty selects root.
 * Invalid levels/names and allocation failures return explicit errors. */
maage_status maage_log_set_level(const char *logger, maage_log_level level);
int maage_log_enabled(const char *logger, maage_log_level level);
maage_status maage_log_flush(void);
/* Flushes/closes owned files, GZip-copies the session and resets defaults.
 * Archive failures preserve the current file and emit an error. Logging before init or after
 * shutdown is safe and uses defaults. FATAL never terminates the process.
 * Operations are serialized; do not call from signal handlers. Messages longer
 * than the internal bound are marked truncated. printf arguments must be valid. */
maage_status maage_log_shutdown(void);
/* Write outcomes describe diagnostic delivery, independently of any attached
 * operation result. Disabled records return NOOP; output errors return IO_ERROR.
 * Callers preserve their operation result even if a diagnostic was delivered. */
maage_status maage_log_write(maage_log_level level, const char *logger, const char *format, ...);
maage_status maage_log_vwrite(maage_log_level level, const char *logger, const char *format, va_list args);
/* Structured occurrence: includes stable Operation Status identity and caller
 * context (path, numeric ID, OS error, job ID). Severity is the caller's logging
 * decision, independent of result severity. Success never logs automatically.
 * Formatting is skipped when filtered. Report once at the handling boundary;
 * callers own occurrence/failure caches rather than global last-error state. */
maage_status maage_log_status(maage_log_level level, const char *logger,
    maage_status result, const char *format, ...);
/* One bounded single-line causal diagnostic. No trace formatting or allocation
 * occurs if filtered; trace/source strings are borrowed only during this call. */
maage_status maage_log_trace_status(maage_log_level level, const char *logger,
    maage_status result, const maage_status_trace *trace, const char *format, ...);
maage_status maage_log_verbose(const char *logger, const char *format, ...);
maage_status maage_log_warning(const char *logger, const char *format, ...);
maage_status maage_log_info(const char *logger, const char *format, ...);
maage_status maage_log_error(const char *logger, const char *format, ...);
maage_status maage_log_fatal(const char *logger, const char *format, ...);

#ifdef __cplusplus
}
#endif
#endif
