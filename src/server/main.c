#include "server.h"
#include "maage/log.h"
#include "maage/world.h"
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static maage_status maage_server_parse(int argc, char **argv,
    maage_server_config *config, maage_generation_policy *policy)
{
    int i;
    unsigned long number;
    char *end;
    const char *argument;
    memset(config, 0, sizeof(*config));
    config->execution_mode = MAAGE_SERVER_DEDICATED;
    config->render_distance = 8;
    config->initial_game_mode = -1;
    for (i = 1; i < argc; ++i) {
        argument = argv[i];
        if (!strncmp(argument, "--instance-root=", 16)) config->instance_root = argument + 16;
        else if (!strncmp(argument, "--world=", 8)) {
            if (config->world_id || config->create_world) goto invalid;
            config->world_id = argument + 8;
        } else if (!strncmp(argument, "--create-world=", 15)) {
            if (config->world_id || config->create_world) goto invalid;
            config->world_id = argument + 15; config->create_world = 1;
        } else if (!strcmp(argument, "--new-world")) {
            if (config->world_id || config->create_world) goto invalid;
            config->create_world = 1;
        } else if (!strncmp(argument, "--world-name=", 13)) config->world_name = argument + 13;
        else if (!strncmp(argument, "--seed=", 7)) {
            if (maage_status_failed(maage_world_seed_parse(argument + 7, &config->seed))) goto invalid;
            config->has_seed = 1;
        } else if (!strncmp(argument, "--render-distance=", 18) ||
            !strncmp(argument, "--world-workers=", 16) ||
            !strncmp(argument, "--generator=", 12)) {
            const char *value = argument + (!strncmp(argument, "--render-distance=", 18) ? 18 :
                (!strncmp(argument, "--world-workers=", 16) ? 16 : 12));
            errno = 0;
            number = strtoul(value, &end, 10);
            if (errno || !*value || *end || number > UINT_MAX) goto invalid;
            if (!strncmp(argument, "--render-distance=", 18)) {
                if (number < 1 || number > MAAGE_WORLD_MAX_RADIUS) goto invalid;
                config->render_distance = (int)number;
            } else if (!strncmp(argument, "--world-workers=", 16)) config->world_workers = (unsigned int)number;
            else {
                if (maage_status_failed(maage_generation_policy_default(number, policy))) goto invalid;
                config->generation_policy = policy;
            }
        } else if (!strcmp(argument, "--survival")) config->initial_game_mode = 0;
        else if (!strcmp(argument, "--creative")) config->initial_game_mode = 1;
        else if (!strncmp(argument, "--reach=", 8)) {
            errno = 0;
            config->interaction_range = strtod(argument + 8, &end);
            if (errno || !argument[8] || *end || config->interaction_range <= 0 ||
                config->interaction_range > 64) goto invalid;
        } else if (!strcmp(argument, "--help")) {
            puts("Usage: server --instance-root=<absolute-instance-directory> [--world=<uuid> | --create-world=<uuid> | --new-world] [--world-name=<name>] [--seed=<signed-seed>] [--generator=<version>] [--render-distance=1..32] [--world-workers=<count>] [--creative | --survival] [--reach=0..64]");
            return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_PARSE, 0);
        } else goto invalid;
    }
    if (!config->instance_root || !*config->instance_root) goto invalid;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_PARSE, 0);
invalid:
    return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT,
        MAAGE_CONTEXT_SERVER, MAAGE_OPERATION_PARSE, 0);
}

int main(int argc, char **argv)
{
    maage_status result, shutdown;
    maage_status_trace trace;
    maage_server_config config;
    maage_generation_policy policy;
    maage_status_trace_init(&trace);
    result = maage_log_init(NULL, argc, argv);
    if (!maage_status_failed(result)) {
        result = maage_server_parse(argc, argv, &config, &policy);
        if (maage_status_code(result) == MAAGE_STATUS_NOOP) result = 0;
        else if (!maage_status_failed(result)) result = maage_server_run(&config, &trace);
    }
    if (maage_status_failed(result))
        maage_log_trace_status(MAAGE_LOG_ERROR, "maage.server",
            result, &trace, "Server stopped.");
    shutdown = maage_log_shutdown();
    return maage_status_failed(result) || maage_status_failed(shutdown) ? 1 : 0;
}
