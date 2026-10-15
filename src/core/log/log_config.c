#include "log_internal.h"
#include <ctype.h>
#include <errno.h>
#include <string.h>

typedef struct maage_log_config_status {
    int ok;
    char problem[256];
} maage_log_config_status;

static void maage_log_problem(maage_log_config_status *status, const char *message, const char *detail)
{
    size_t length;
    status->ok = 0;
    if (status->problem[0]) return;
    length = strlen(message);
    if (length > sizeof(status->problem) - 1) length = sizeof(status->problem) - 1;
    memcpy(status->problem, message, length);
    status->problem[length] = '\0';
    if (detail) strncat(status->problem, detail, sizeof(status->problem) - length - 1);
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

static int maage_log_parse_level(const char *text, maage_log_level *level)
{
    static const char *names[] = { "TRACE", "DEBUG", "INFO", "WARN", "ERROR", "FATAL", "OFF" };
    int index;
    for (index = 0; index <= (int)MAAGE_LOG_OFF; ++index) {
        if (strcmp(text, names[index]) == 0) { *level = (maage_log_level)index; return 1; }
    }
    return 0;
}

static int maage_log_parse_bool(const char *text, int *value)
{
    if (strcmp(text, "true") == 0) { *value = 1; return 1; }
    if (strcmp(text, "false") == 0) { *value = 0; return 1; }
    return 0;
}

static int maage_log_path(const char *value)
{
    size_t length;
    if (!value) return 0;
    length = strlen(value);
    if (!length || length >= sizeof(maage_log_current.path)) return 0;
    memcpy(maage_log_current.path, value, length + 1);
    return 1;
}

static int maage_log_setting(const char *key, const char *value)
{
    maage_log_config *config = &maage_log_current.config;
    if (strcmp(key, "level") == 0) return maage_log_parse_level(value, &config->level);
    if (strcmp(key, "console") == 0) return maage_log_parse_bool(value, &config->console);
    if (strcmp(key, "file") == 0) return maage_log_parse_bool(value, &config->file);
    if (strcmp(key, "timestamp") == 0) return maage_log_parse_bool(value, &config->timestamp);
    if (strcmp(key, "file_path") == 0) return maage_log_path(value);
    return 0;
}

static void maage_log_read_ini(const char *path, maage_log_config_status *status)
{
    FILE *file;
    char line[2048], *text, *separator, *key, *value;
    int section = 0, discard;
    maage_log_level level;
    errno = 0;
    file = maage_log_open(path, "r");
    if (!file) {
        if (errno != ENOENT) maage_log_problem(status, "Cannot read logging configuration: ", path);
        return;
    }
    while (fgets(line, sizeof(line), file)) {
        if (!strchr(line, '\n') && !feof(file)) {
            while ((discard = fgetc(file)) != '\n' && discard != EOF) { }
            maage_log_problem(status, "Logging configuration line is too long: ", path);
            continue;
        }
        text = maage_log_trim(line);
        if (!*text || *text == '#' || *text == ';') continue;
        if (*text == '[') {
            if (strcmp(text, "[logging]") == 0) section = 1;
            else if (strcmp(text, "[loggers]") == 0) section = 2;
            else { section = 0; maage_log_problem(status, "Unknown logging section: ", text); }
            continue;
        }
        separator = strchr(text, '=');
        if (!separator) { maage_log_problem(status, "Invalid logging setting: ", text); continue; }
        *separator = '\0';
        key = maage_log_trim(text);
        value = maage_log_trim(separator + 1);
        if (section == 1 && maage_log_setting(key, value)) continue;
        if (section == 2 && *key && maage_log_parse_level(value, &level) &&
            maage_log_override_set(key, level)) continue;
        maage_log_problem(status, "Invalid logging setting: ", key);
    }
    if (ferror(file)) maage_log_problem(status, "Error reading logging configuration: ", path);
    fclose(file);
}

int maage_log_configure(const maage_log_config *defaults, int argc, char *const argv[])
{
    maage_log_config_status status;
    maage_log_config *config = &maage_log_current.config;
    const char *ini = defaults ? defaults->config_path : NULL;
    const char *argument;
    int index, console;
    status.ok = 1;
    status.problem[0] = '\0';
    maage_log_config_init(config);
    maage_log_path(config->file_path);
    if (defaults) {
        if (defaults->level >= MAAGE_LOG_TRACE && defaults->level <= MAAGE_LOG_OFF)
            config->level = defaults->level;
        else maage_log_problem(&status, "Invalid default logging level", NULL);
        config->console = defaults->console != 0;
        config->file = defaults->file != 0;
        config->timestamp = defaults->timestamp != 0;
        if (defaults->file_path && !maage_log_path(defaults->file_path))
            maage_log_problem(&status, "Invalid default logging file path", NULL);
    }
    /* Find the INI before applying any other CLI overrides. Last argument wins. */
    for (index = 1; argv && index < argc; ++index) {
        if (argv[index] && strncmp(argv[index], "--log-config=", 13) == 0) {
            if (argv[index][13]) ini = argv[index] + 13;
            else maage_log_problem(&status, "Empty --log-config path", NULL);
        }
    }
    if (ini && *ini) maage_log_read_ini(ini, &status);
    for (index = 1; argv && index < argc; ++index) {
        argument = argv[index];
        if (!argument) continue;
        if (strncmp(argument, "--log-level=", 12) == 0) {
            if (!maage_log_setting("level", argument + 12))
                maage_log_problem(&status, "Invalid --log-level: ", argument + 12);
        } else if (strncmp(argument, "--log-file=", 11) == 0) {
            if (maage_log_path(argument + 11)) config->file = 1;
            else maage_log_problem(&status, "Invalid --log-file path: ", argument + 11);
        } else if (strncmp(argument, "--log-console=", 14) == 0) {
            if (!maage_log_setting("console", argument + 14))
                maage_log_problem(&status, "Invalid --log-console: ", argument + 14);
        }
    }
    config->file_path = maage_log_current.path;
    config->config_path = NULL;
    if (config->file) {
        maage_log_current.output = maage_log_open(config->file_path, "a");
        if (!maage_log_current.output) {
            config->file = 0;
            maage_log_problem(&status, "Cannot append to logging file: ", config->file_path);
        }
    }
    if (!status.ok) {
        /* Configuration failures remain visible even if console=false disabled
         * the only usable output. No console window is ever allocated. */
        console = config->console;
        if (!maage_log_current.output) config->console = 1;
        maage_log_emit(MAAGE_LOG_ERROR, "maage.core.log", status.problem);
        if (!maage_log_current.output && !maage_log_console_available(1))
            maage_log_platform_diagnostic(status.problem);
        config->console = console;
    }
    return status.ok;
}
