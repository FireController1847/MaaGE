#ifndef MAAGE_LOG_H
#define MAAGE_LOG_H

#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum maage_log_level {
    MAAGE_LOG_TRACE, MAAGE_LOG_DEBUG, MAAGE_LOG_INFO, MAAGE_LOG_WARN,
    MAAGE_LOG_ERROR, MAAGE_LOG_FATAL, MAAGE_LOG_OFF
} maage_log_level;

typedef struct maage_log_config {
    maage_log_level level;
    int console;
    int file;
    int timestamp;
    const char *file_path;
    const char *config_path;
} maage_log_config;

/* Defaults: INFO, console enabled, file disabled, local timestamps enabled.
 * init copies strings; caller storage need only survive the call. Precedence:
 * supplied defaults, optional INI, then recognized --log-* arguments. Missing
 * INI files are optional; other errors return zero with usable settings retained.
 * With file output enabled, init archives any previous file using a sortable
 * local rotation timestamp (numeric suffix only on collision), then starts a new
 * latest file.
 * Archives are retained; rotation failure preserves the old file and returns zero.
 * Unknown application arguments are ignored and argv is never modified. */
void maage_log_config_init(maage_log_config *config);
int maage_log_init(const maage_log_config *config, int argc, char *const argv[]);
/* Exact logger overrides inherit at dot boundaries; NULL/empty selects root.
 * Returns zero for invalid levels, overlong names, or a full override table. */
int maage_log_set_level(const char *logger, maage_log_level level);
int maage_log_enabled(const char *logger, maage_log_level level);
void maage_log_flush(void);
/* Closes owned files and resets to defaults. Logging before init or after
 * shutdown is safe and uses defaults. FATAL never terminates the process.
 * Operations are serialized; do not call from signal handlers. Messages longer
 * than the internal bound are marked truncated. printf arguments must be valid. */
void maage_log_shutdown(void);
void maage_log_write(maage_log_level level, const char *logger, const char *format, ...);
void maage_log_vwrite(maage_log_level level, const char *logger, const char *format, va_list args);
void maage_log_trace(const char *logger, const char *format, ...);
void maage_log_debug(const char *logger, const char *format, ...);
void maage_log_info(const char *logger, const char *format, ...);
void maage_log_warn(const char *logger, const char *format, ...);
void maage_log_error(const char *logger, const char *format, ...);
void maage_log_fatal(const char *logger, const char *format, ...);

#ifdef __cplusplus
}
#endif
#endif
