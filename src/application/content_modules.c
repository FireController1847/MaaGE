#include "content_modules.h"
#include "maage/base.h"
#include <string.h>

void maage_application_content_config(maage_content_config *config) {
    static const maage_content_module *const modules[] = {&maage_base_module};
    memset(config, 0, sizeof(*config));
    config->modules = modules;
    config->module_count = sizeof(modules) / sizeof(modules[0]);
}
