#include "log_internal.h"
#include <string.h>
#include <stdlib.h>

/* Keep empty tables in zero-fill storage instead of embedding them in the file. */
#if defined(MAAGE_LOG_BUILD_DEBUG)
#define MAAGE_LOG_DEFAULT_LEVEL MAAGE_LOG_VERBOSE
#else
#define MAAGE_LOG_DEFAULT_LEVEL MAAGE_LOG_INFO
#endif
static char maage_log_path_storage[MAAGE_LOG_PATH_SIZE];
maage_log_state maage_log_current = {
    { MAAGE_LOG_DEFAULT_LEVEL, 1, 0, 1, NULL, NULL, 0 }, maage_log_path_storage, NULL, 0, 0, NULL, 0
};

void maage_log_config_init(maage_log_config *config)
{
    if (!config) return;
    config->level = MAAGE_LOG_DEFAULT_LEVEL;
    config->console = 1;
    config->file = 0;
    config->timestamp = 1;
    config->file_path = "maage.log";
    config->config_path = NULL;
    config->capture_stdio = 0;
}

static void maage_log_close_file(void)
{
    int closed, console;
    const char *problem;
    if (!maage_log_current.output) return;
    if (maage_log_current.stdio_captured) { fflush(stdout); fflush(stderr); }
    closed = fclose(maage_log_current.output) == 0;
    maage_log_current.output = NULL;
    maage_log_current.stdio_captured = 0;
    if (closed && maage_log_rotate(maage_log_current.path, 1)) return;
    problem = closed ? "Cannot archive completed logging file; current file preserved." :
        "Cannot close logging file; current file preserved.";
    console = maage_log_current.config.console;
    maage_log_current.config.console = 1;
    maage_log_emit(MAAGE_LOG_ERROR, "maage.core.log", problem);
    if (!maage_log_console_available(1)) maage_log_platform_diagnostic(problem);
    maage_log_current.config.console = console;
}

int maage_log_init(const maage_log_config *config, int argc, char *const argv[])
{
    int result;
    maage_log_lock();
    maage_log_close_file();
    free(maage_log_current.overrides);
    maage_log_current.overrides = NULL;
    maage_log_current.count = maage_log_current.capacity = 0;
    result = maage_log_configure(config, argc, argv);
    maage_log_unlock();
    return result;
}

int maage_log_override_set(const char *name, maage_log_level level)
{
    size_t index;
    if (level < MAAGE_LOG_TRACE || level > MAAGE_LOG_OFF) return 0;
    if (!name || !*name) { maage_log_current.config.level = level; return 1; }
    if (strlen(name) >= MAAGE_LOG_NAME_SIZE) return 0;
    for (index = 0; index < maage_log_current.count; ++index)
        if (strcmp(name, maage_log_current.overrides[index].name) == 0) break;
    if (index == maage_log_current.count && index == maage_log_current.capacity) {
        size_t capacity = maage_log_current.capacity ? maage_log_current.capacity * 2 : 8;
        maage_log_override *grown;
        if (capacity < maage_log_current.capacity || capacity > (size_t)-1 / sizeof(*grown)) return 0;
        grown = (maage_log_override *)realloc(maage_log_current.overrides, capacity * sizeof(*grown));
        if (!grown) return 0;
        maage_log_current.overrides = grown;
        maage_log_current.capacity = capacity;
    }
    strcpy(maage_log_current.overrides[index].name, name);
    maage_log_current.overrides[index].level = level;
    if (index == maage_log_current.count) ++maage_log_current.count;
    return 1;
}

int maage_log_set_level(const char *logger, maage_log_level level)
{
    int result;
    maage_log_lock();
    result = maage_log_override_set(logger, level);
    maage_log_unlock();
    return result;
}

static int maage_log_is_enabled(const char *logger, maage_log_level level)
{
    maage_log_level effective = maage_log_current.config.level;
    size_t index, length, best = 0;
    if (level < MAAGE_LOG_TRACE || level >= MAAGE_LOG_OFF) return 0;
    if (!logger || !*logger) logger = "maage";
    for (index = 0; index < maage_log_current.count; ++index) {
        length = strlen(maage_log_current.overrides[index].name);
        if (length > best && strncmp(logger, maage_log_current.overrides[index].name, length) == 0 &&
            (logger[length] == '\0' || logger[length] == '.')) {
            best = length;
            effective = maage_log_current.overrides[index].level;
        }
    }
    return effective != MAAGE_LOG_OFF && level >= effective;
}

int maage_log_enabled(const char *logger, maage_log_level level)
{
    int result;
    maage_log_lock();
    result = maage_log_is_enabled(logger, level);
    maage_log_unlock();
    return result;
}

static void maage_log_sink(FILE *sink, const char *timestamp, const char *level,
    const char *logger, const char *message)
{
    if (*timestamp) fprintf(sink, "%s ", timestamp);
    /* Pad after the complete severity label, never inside its brackets. */
    fprintf(sink, "[%s]%*s [%s] %s\n", level, (int)(7 - strlen(level)), "", logger, message);
    fflush(sink);
}

void maage_log_emit(maage_log_level level, const char *logger, const char *message)
{
    static const char *names[] = { "VERBOSE", "INFO", "WARNING", "ERROR", "FATAL" };
    char timestamp[32];
    int error = level >= MAAGE_LOG_ERROR;
    timestamp[0] = '\0';
    if (maage_log_current.config.timestamp) maage_log_timestamp(timestamp, sizeof(timestamp));
    if (!logger || !*logger) logger = "maage";
    if (maage_log_current.config.console && !maage_log_current.stdio_captured && maage_log_console_available(error))
        maage_log_sink(error ? stderr : stdout, timestamp, names[level], logger, message);
    if (maage_log_current.output)
        maage_log_sink(maage_log_current.output, timestamp, names[level], logger, message);
}

void maage_log_vwrite(maage_log_level level, const char *logger, const char *format, va_list args)
{
    char message[2048];
    static const char truncated[] = "... [truncated]";
    int length;
    if (!format) return;
    maage_log_lock();
    if (maage_log_is_enabled(logger, level)) {
        message[0] = '\0';
        length = maage_log_format(message, sizeof(message), format, args);
        message[sizeof(message) - 1] = '\0';
        if (length < 0 || (size_t)length >= sizeof(message)) {
            size_t used = strlen(message);
            if (used > sizeof(message) - sizeof(truncated)) used = sizeof(message) - sizeof(truncated);
            strcpy(message + used, truncated);
        }
        maage_log_emit(level, logger, message);
    }
    maage_log_unlock();
}

void maage_log_write(maage_log_level level, const char *logger, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    maage_log_vwrite(level, logger, format, args);
    va_end(args);
}

void maage_log_status(maage_log_level level, const char *logger,
    maage_status result, const char *format, ...)
{
    char message[2048];
    static const char truncated[] = "... [truncated]";
    size_t used, available;
    int length;
    va_list args;
    maage_log_lock();
    if (maage_log_is_enabled(logger, level)) {
        used = maage_status_format(result, message, sizeof(message));
        if (used >= sizeof(message)) used = sizeof(message) - 1;
        if (format && *format && used + 3 < sizeof(message)) {
            memcpy(message + used, " | ", 3); used += 3;
            available = sizeof(message) - used;
            va_start(args, format);
            length = maage_log_format(message + used, available, format, args);
            va_end(args);
            message[sizeof(message) - 1] = '\0';
            if (length < 0 || (size_t)length >= available) {
                used = strlen(message);
                if (used > sizeof(message) - sizeof(truncated)) used = sizeof(message) - sizeof(truncated);
                strcpy(message + used, truncated);
            }
        }
        maage_log_emit(level, logger, message);
    }
    maage_log_unlock();
}

#define MAAGE_LOG_FUNCTION(name, level) \
void name(const char *logger, const char *format, ...) \
{ \
    va_list args; \
    va_start(args, format); \
    maage_log_vwrite(level, logger, format, args); \
    va_end(args); \
}

MAAGE_LOG_FUNCTION(maage_log_trace, MAAGE_LOG_TRACE)
MAAGE_LOG_FUNCTION(maage_log_debug, MAAGE_LOG_DEBUG)
MAAGE_LOG_FUNCTION(maage_log_verbose, MAAGE_LOG_VERBOSE)
MAAGE_LOG_FUNCTION(maage_log_info, MAAGE_LOG_INFO)
MAAGE_LOG_FUNCTION(maage_log_warn, MAAGE_LOG_WARN)
MAAGE_LOG_FUNCTION(maage_log_warning, MAAGE_LOG_WARNING)
MAAGE_LOG_FUNCTION(maage_log_error, MAAGE_LOG_ERROR)
MAAGE_LOG_FUNCTION(maage_log_fatal, MAAGE_LOG_FATAL)

void maage_log_flush(void)
{
    maage_log_lock();
    if (maage_log_current.config.console || maage_log_current.stdio_captured) {
        if (maage_log_console_available(0)) fflush(stdout);
        if (maage_log_console_available(1)) fflush(stderr);
    }
    if (maage_log_current.output) fflush(maage_log_current.output);
    maage_log_unlock();
}

void maage_log_shutdown(void)
{
    maage_log_flush();
    maage_log_lock();
    maage_log_close_file();
    free(maage_log_current.overrides);
    maage_log_current.overrides = NULL;
    maage_log_current.count = maage_log_current.capacity = 0;
    maage_log_config_init(&maage_log_current.config);
    maage_log_unlock();
}
