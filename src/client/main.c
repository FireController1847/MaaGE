#include "maage/log.h"
#include "maage_core.h"
#include "maage/client_version.h"
#include "maage/application.h"
#include "maage/client.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

int maage_application_query(int argc, char *const argv[], int *result)
{
    const maage_client_metadata *m = maage_client_identity();
    char *output;
    size_t capacity;
    int json;
    if (argc != 2 || (strcmp(argv[1], "--version") && strcmp(argv[1], "--version-json"))) return 0;
    json = !strcmp(argv[1], "--version-json");
    capacity = strlen(m->version) + strlen(m->build) + strlen(m->build_type) + strlen(m->platform) + strlen(m->architecture) + 256;
    output = (char *)malloc(capacity);
    if (!output) { *result = 1; return 1; }
    if (json) sprintf(output, "{\"schema\":1,\"application\":\"maage-client\",\"version\":\"%s\",\"build\":\"%s\",\"build_type\":\"%s\",\"platform\":\"%s\",\"architecture\":\"%s\",\"instance_storage\":1,\"player_appearance\":1,\"protocol\":null}\n",
        m->version, m->build, m->build_type, m->platform, m->architecture);
    else sprintf(output, "MaaGE Client %s+%s (%s; %s %s)\n", m->version, m->build, m->build_type, m->platform, m->architecture);
#ifdef _WIN32
    {
        HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
        DWORD written = 0;
        if (!out || out == INVALID_HANDLE_VALUE) {
            AttachConsole(ATTACH_PARENT_PROCESS); out = GetStdHandle(STD_OUTPUT_HANDLE);
        }
        /* GUI-subsystem CRT stdout can be invalid even with an inherited pipe. */
        *result = out && out != INVALID_HANDLE_VALUE && WriteFile(out, output, (DWORD)strlen(output), &written, NULL) && written == strlen(output) ? 0 : 1;
    }
#else
    *result = fputs(output, stdout) >= 0 && fflush(stdout) == 0 ? 0 : 1;
#endif
    free(output); return 1;
}

maage_status maage_client_run(int argc, char *const argv[], maage_status_trace *trace);
maage_status maage_application_main(int argc, char *const argv[])
{
    maage_status_trace trace;
    maage_status_trace_init(&trace);
    MAAGE_STATUS_TRACE(&trace, 0, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_INITIALIZE);
    maage_log_info("maage.client", "%s Client %s", maage_core_project_name, MAAGE_CLIENT_VERSION);
    return maage_client_run(argc, argv, &trace);
}
