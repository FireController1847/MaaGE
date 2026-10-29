#ifndef MAAGE_LOG_INTERNAL_H
#define MAAGE_LOG_INTERNAL_H

#include "maage/log.h"
#include <stddef.h>
#include <stdio.h>

#define MAAGE_LOG_PATH_SIZE 1024
#define MAAGE_LOG_NAME_SIZE 128
#define MAAGE_LOG_EVENT_SIZE 8192

typedef struct maage_log_override {
    char name[MAAGE_LOG_NAME_SIZE];
    maage_log_level level;
} maage_log_override;

typedef struct maage_log_state {
    maage_log_config config;
    char *path;
    maage_log_override *overrides;
    size_t count, capacity;
    FILE *output;
    int stdio_captured;
} maage_log_state;

/* All state operations below require the platform lock. */
extern maage_log_state maage_log_current;
maage_status maage_log_override_set(const char *name, maage_log_level level);
maage_status maage_log_emit(maage_log_level level, const char *logger, const char *message);
maage_status maage_log_configure(const maage_log_config *config, int argc, char *const argv[]);

/* Caller holds the log lock; root failure precedes the chronological trail. */
void maage_log_format_status(maage_status result, const maage_status_trace *trace, const char *description, char *buffer, size_t capacity);

void maage_log_lock(void);
void maage_log_unlock(void);
/* Native bounded formatting and local-time APIs stay behind this boundary. */
int maage_log_format(char *buffer, size_t capacity, const char *format, va_list args);
void maage_log_timestamp(char *buffer, size_t capacity);
int maage_log_console_available(int error);
void maage_log_platform_diagnostic(const char *message);
/* Publish a dated GZip copy, retaining the plain file. closing stamps the session
 * at close; startup recovery reuses that stamp and skips identical archives. */
maage_status maage_log_rotate(const char *path, int closing, maage_status_trace *trace);
/* Archive I/O: time returns 1 nonempty, 0 absent/empty, -1 failure. Publish returns
 * 1 success, 0 name collision, -1 failure; create is exclusive (errno=EEXIST). */
int maage_log_archive_time(const char *path, int closing, char timestamp[32]);
FILE *maage_log_archive_create(const char *path);
int maage_log_archive_publish(const char *temporary, const char *archive);
int maage_log_archive_remove(const char *path);
int maage_log_archive_sync(FILE *file);
FILE *maage_log_open(const char *path, const char *mode);
int maage_log_capture_stdio(FILE *output);

#endif
