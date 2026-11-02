#include "content_modules.h"
#include "maage/base.h"
#include "maage/log.h"
#include <string.h>

void maage_application_content_config(maage_content_config *config) {
    static const maage_content_module *const modules[] = {&maage_base_module};
    memset(config, 0, sizeof(*config));
    config->modules = modules;
    config->module_count = sizeof(modules) / sizeof(modules[0]);
}

maage_status maage_application_content_create(const maage_content_config *config, maage_content **output, maage_status_trace *trace) {
    maage_content_config registration;
    maage_content_failure local_failure, *failure;
    maage_status result;
    if (!config) return maage_content_create(config, output, trace);
    registration = *config;
    failure = config->failure ? config->failure : &local_failure;
    registration.failure = failure;
    result = maage_content_create(&registration, output, trace);
    if (maage_status_failed(result)) maage_log_trace_status(MAAGE_LOG_ERROR, "maage.content", result, trace, "Content initialization failed: module=%s phase=%u dependency=%s.", failure->module_name ? failure->module_name : "<shared registries>", (unsigned int) failure->phase, failure->dependency_name ? failure->dependency_name : "<none>");
    return result;
}
