#ifndef MAAGE_LOG_INTERNAL_H
#define MAAGE_LOG_INTERNAL_H

#include "maage/log.h"
#include <stddef.h>
#include <stdio.h>

#define MAAGE_LOG_PATH_SIZE 1024
#define MAAGE_LOG_NAME_SIZE 128
#define MAAGE_LOG_OVERRIDE_COUNT 64

typedef struct maage_log_override {
    char name[MAAGE_LOG_NAME_SIZE];
    maage_log_level level;
} maage_log_override;

typedef struct maage_log_state {
    maage_log_config config;
    char path[MAAGE_LOG_PATH_SIZE];
    maage_log_override overrides[MAAGE_LOG_OVERRIDE_COUNT];
    size_t count;
    FILE *output;
} maage_log_state;

/* All state operations below require the platform lock. */
extern maage_log_state maage_log_current;
int maage_log_override_set(const char *name, maage_log_level level);
void maage_log_emit(maage_log_level level, const char *logger, const char *message);
int maage_log_configure(const maage_log_config *config, int argc, char *const argv[]);

void maage_log_lock(void);
void maage_log_unlock(void);
/* Native bounded formatting and local-time APIs stay behind this boundary. */
int maage_log_format(char *buffer, size_t capacity, const char *format, va_list args);
void maage_log_timestamp(char *buffer, size_t capacity);
int maage_log_console_available(int error);
void maage_log_platform_diagnostic(const char *message);
FILE *maage_log_open(const char *path, const char *mode);

#endif
