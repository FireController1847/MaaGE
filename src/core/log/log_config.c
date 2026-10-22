#include "log_internal.h"
#include <ctype.h>
#include <errno.h>
#include <string.h>

typedef struct maage_log_config_status {
    maage_status result;
    maage_status_trace trace;
    char problem[256];
} maage_log_config_status;

static maage_status maage_log_problem(maage_log_config_status *status,
    maage_status result, const char *message, const char *detail)
{
    size_t length;
    MAAGE_STATUS_PROPAGATE(&status->trace, result, MAAGE_CONTEXT_CONFIG, MAAGE_OPERATION_PARSE);
    if (maage_status_is_success(status->result)) status->result = result;
    if (status->problem[0]) return result;
    length = strlen(message);
    if (length > sizeof(status->problem) - 1) length = sizeof(status->problem) - 1;
    memcpy(status->problem, message, length);
    status->problem[length] = '\0';
    if (detail) strncat(status->problem, detail, sizeof(status->problem) - length - 1);
    return result;
}

static char *maage_log_trim(char *text)
{
    char *end;
    while (isspace((unsigned char)*text)) ++text;
    end = text + strlen(text);
    while (end > text && isspace((unsigned char)end[-1])) --end;
    *end = '\0';
    return text;
}

static maage_status maage_log_parse_level(const char *text, maage_log_level *level)
{
    static const char *names[] = { "VERBOSE", "INFO", "WARNING", "ERROR", "FATAL", "OFF" };
    int index;
    for (index = 0; index <= (int)MAAGE_LOG_OFF; ++index) {
        if (strcmp(text, names[index]) == 0) {
            *level = (maage_log_level)index;
            return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_PARSE, 0);
        }
    }
    return maage_status_make(MAAGE_STATUS_MALFORMED_INPUT, MAAGE_CONTEXT_CONFIG, MAAGE_OPERATION_PARSE, 0);
}

static maage_status maage_log_parse_bool(const char *text, int *value)
{
    if (strcmp(text, "true") == 0) *value = 1;
    else if (strcmp(text, "false") == 0) *value = 0;
    else return maage_status_make(MAAGE_STATUS_MALFORMED_INPUT, MAAGE_CONTEXT_CONFIG, MAAGE_OPERATION_PARSE, 0);
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_PARSE, 0);
}

static maage_status maage_log_path(const char *value)
{
    size_t length;
    if (!value || !*value) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT,
        MAAGE_CONTEXT_CONFIG, MAAGE_OPERATION_SET, 0);
    length = strlen(value);
    if (length >= MAAGE_LOG_PATH_SIZE) return maage_status_make(MAAGE_STATUS_BUFFER_OVERFLOW,
        MAAGE_CONTEXT_CONFIG, MAAGE_OPERATION_SET, 0);
    memmove(maage_log_current.path, value, length + 1);
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_SET, 0);
}

static maage_status maage_log_setting(const char *key, const char *value)
{
    maage_log_config *config = &maage_log_current.config;
    if (strcmp(key, "level") == 0) return maage_log_parse_level(value, &config->level);
    if (strcmp(key, "console") == 0) return maage_log_parse_bool(value, &config->console);
    if (strcmp(key, "file") == 0) return maage_log_parse_bool(value, &config->file);
    if (strcmp(key, "timestamp") == 0) return maage_log_parse_bool(value, &config->timestamp);
    if (strcmp(key, "capture_stdio") == 0) return maage_log_parse_bool(value, &config->capture_stdio);
    if (strcmp(key, "file_path") == 0) return maage_log_path(value);
    return maage_status_make(MAAGE_STATUS_NOT_FOUND, MAAGE_CONTEXT_CONFIG, MAAGE_OPERATION_SET, 0);
}

static maage_status maage_log_read_ini(const char *path, maage_log_config_status *status)
{
    FILE *file;
    char line[2048], *text, *separator, *key, *value;
    int section = 0, discard;
    maage_log_level level;
    maage_status result;
    errno = 0;
    file = maage_log_open(path, "r");
    if (!file) {
        if (errno == ENOENT) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_LOAD, 0);
        result = maage_status_from_errno(errno, MAAGE_CONTEXT_CONFIG, MAAGE_OPERATION_OPEN);
        return maage_log_problem(status, result, "Cannot read logging configuration: ", path);
    }
    while (fgets(line, sizeof(line), file)) {
        if (!strchr(line, '\n') && !feof(file)) {
            while ((discard = fgetc(file)) != '\n' && discard != EOF) { }
            maage_log_problem(status, maage_status_make(MAAGE_STATUS_BUFFER_OVERFLOW,
                MAAGE_CONTEXT_CONFIG, MAAGE_OPERATION_READ, 0), "Logging configuration line is too long: ", path);
            continue;
        }
        text = maage_log_trim(line);
        if (!*text || *text == '#' || *text == ';') continue;
        if (*text == '[') {
            if (strcmp(text, "[logging]") == 0) section = 1;
            else if (strcmp(text, "[loggers]") == 0) section = 2;
            else {
                section = 0;
                maage_log_problem(status, maage_status_make(MAAGE_STATUS_MALFORMED_INPUT,
                    MAAGE_CONTEXT_CONFIG, MAAGE_OPERATION_PARSE, 0), "Unknown logging section: ", text);
            }
            continue;
        }
        separator = strchr(text, '=');
        if (!separator) {
            maage_log_problem(status, maage_status_make(MAAGE_STATUS_MALFORMED_INPUT,
                MAAGE_CONTEXT_CONFIG, MAAGE_OPERATION_PARSE, 0), "Invalid logging setting: ", text);
            continue;
        }
        *separator = '\0';
        key = maage_log_trim(text); value = maage_log_trim(separator + 1);
        result = maage_status_make(MAAGE_STATUS_MALFORMED_INPUT, MAAGE_CONTEXT_CONFIG, MAAGE_OPERATION_PARSE, 0);
        if (section == 1) result = maage_log_setting(key, value);
        else if (section == 2 && *key) {
            result = maage_log_parse_level(value, &level);
            if (!maage_status_failed(result)) result = maage_log_override_set(key, level);
        }
        if (maage_status_failed(result)) maage_log_problem(status, result, "Invalid logging setting: ", key);
    }
    if (ferror(file)) maage_log_problem(status, maage_status_make(MAAGE_STATUS_IO_ERROR,
        MAAGE_CONTEXT_CONFIG, MAAGE_OPERATION_READ, 0), "Error reading logging configuration: ", path);
    if (fclose(file)) maage_log_problem(status, maage_status_from_errno(errno, MAAGE_CONTEXT_CONFIG,
        MAAGE_OPERATION_CLOSE), "Error closing logging configuration: ", path);
    return status->result;
}

maage_status maage_log_configure(const maage_log_config *defaults, int argc, char *const argv[])
{
    maage_log_config_status status;
    maage_log_config *config = &maage_log_current.config;
    const char *ini = defaults ? defaults->config_path : NULL;
    const char *argument;
    char message[2048];
    size_t used;
    int index, console;
    maage_status result;
    status.result = maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_PARSE, 0);
    maage_status_trace_init(&status.trace);
    status.problem[0] = '\0';
    maage_log_config_init(config);
    maage_log_path(config->file_path);
    if (defaults) {
        if (defaults->level >= MAAGE_LOG_VERBOSE && defaults->level <= MAAGE_LOG_OFF) config->level = defaults->level;
        else maage_log_problem(&status, maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT,
            MAAGE_CONTEXT_CONFIG, MAAGE_OPERATION_SET, 0), "Invalid default logging level", NULL);
        config->console = defaults->console != 0; config->file = defaults->file != 0;
        config->timestamp = defaults->timestamp != 0; config->capture_stdio = defaults->capture_stdio != 0;
        if (defaults->file_path) {
            result = maage_log_path(defaults->file_path);
            if (maage_status_failed(result)) maage_log_problem(&status, result, "Invalid default logging file path", NULL);
        }
    }
    /* Find the INI before applying other CLI overrides. Last argument wins. */
    for (index = 1; argv && index < argc; ++index) {
        if (argv[index] && strncmp(argv[index], "--log-config=", 13) == 0) {
            if (argv[index][13]) ini = argv[index] + 13;
            else maage_log_problem(&status, maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT,
                MAAGE_CONTEXT_CONFIG, MAAGE_OPERATION_SET, 2), "Empty --log-config path", NULL);
        }
    }
    if (ini && *ini) maage_log_read_ini(ini, &status);
    for (index = 1; argv && index < argc; ++index) {
        argument = argv[index];
        if (!argument) continue;
        if (strncmp(argument, "--log-level=", 12) == 0) {
            result = maage_log_setting("level", argument + 12);
            if (maage_status_failed(result)) maage_log_problem(&status, result, "Invalid --log-level: ", argument + 12);
        } else if (strncmp(argument, "--log-file=", 11) == 0) {
            result = maage_log_path(argument + 11);
            if (!maage_status_failed(result)) config->file = 1;
            else maage_log_problem(&status, result, "Invalid --log-file path: ", argument + 11);
        } else if (strncmp(argument, "--log-console=", 14) == 0) {
            result = maage_log_setting("console", argument + 14);
            if (maage_status_failed(result)) maage_log_problem(&status, result, "Invalid --log-console: ", argument + 14);
        } else if (strncmp(argument, "--log-capture-stdio=", 20) == 0) {
            result = maage_log_setting("capture_stdio", argument + 20);
            if (maage_status_failed(result)) maage_log_problem(&status, result, "Invalid --log-capture-stdio: ", argument + 20);
        }
    }
    config->file_path = maage_log_current.path; config->config_path = NULL;
    if (config->file) {
        /* Preserve a prior session before resetting the latest plain file. */
        result = maage_log_rotate(config->file_path, 0, &status.trace);
        if (!maage_status_failed(result)) maage_log_current.output = maage_log_open(config->file_path, "w");
        else maage_log_problem(&status, result, "Cannot archive previous logging file: ", config->file_path);
        if (!maage_log_current.output) {
            config->file = 0;
            if (!maage_status_failed(result)) result = maage_status_from_errno(errno, MAAGE_CONTEXT_CONFIG, MAAGE_OPERATION_OPEN);
            maage_log_problem(&status, result, "Cannot start logging file: ", config->file_path);
        }
    }
    if (maage_log_current.output && config->capture_stdio) {
        maage_log_current.stdio_captured = maage_log_capture_stdio(maage_log_current.output);
        if (!maage_log_current.stdio_captured) maage_log_problem(&status, maage_status_make(MAAGE_STATUS_IO_ERROR,
            MAAGE_CONTEXT_CONFIG, MAAGE_OPERATION_SET, 0), "Cannot capture stdout/stderr in logging file: ", config->file_path);
    }
    if (!maage_status_is_success(status.result)) {
        /* Usable settings survive; report the original cause and explicit
         * partial recovery once without recursively entering the log lock. */
        result = maage_status_make(MAAGE_STATUS_PARTIAL, MAAGE_CONTEXT_CONFIG, MAAGE_OPERATION_INITIALIZE, 0);
        maage_status_trace_recover(&status.trace, result, MAAGE_CONTEXT_CONFIG, MAAGE_OPERATION_INITIALIZE,
            __FILE__, (unsigned long)__LINE__);
        used = maage_status_format(status.result, message, sizeof(message));
        if (used + 3 < sizeof(message)) {
            memcpy(message + used, " | ", 3); used += 3;
            used += maage_status_trace_format(&status.trace, message + used, sizeof(message) - used);
        }
        if (used + strlen(status.problem) + 4 < sizeof(message)) { strcat(message, " | "); strcat(message, status.problem); }
        console = config->console;
        if (!maage_log_current.output) config->console = 1;
        maage_log_emit(MAAGE_LOG_WARNING, "maage.core.log", message);
        if (!maage_log_current.output && !maage_log_console_available(1)) maage_log_platform_diagnostic(message);
        config->console = console;
        return result;
    }
    return status.result;
}
