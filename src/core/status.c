#include "maage/status.h"
#include <stdio.h>
#include <string.h>

maage_status maage_status_make(unsigned char status, unsigned char context,
    unsigned char operation, unsigned char detail)
{
    if (status < 0x40) context = 0;
    return ((maage_status)status << 24) | ((maage_status)context << 16) |
        ((maage_status)operation << 8) | (maage_status)detail;
}
unsigned char maage_status_code(maage_status result) { return (unsigned char)(result >> 24); }
unsigned char maage_status_context(maage_status result) { return (unsigned char)(result >> 16); }
unsigned char maage_status_operation(maage_status result) { return (unsigned char)(result >> 8); }
unsigned char maage_status_detail(maage_status result) { return (unsigned char)result; }
void maage_status_to_bytes(maage_status result, unsigned char bytes[4])
{
    bytes[0] = maage_status_code(result); bytes[1] = maage_status_context(result);
    bytes[2] = maage_status_operation(result); bytes[3] = maage_status_detail(result);
}
maage_status maage_status_from_bytes(const unsigned char bytes[4])
{
    return ((maage_status)bytes[0] << 24) | ((maage_status)bytes[1] << 16) |
        ((maage_status)bytes[2] << 8) | (maage_status)bytes[3];
}
maage_status_severity_value maage_status_severity(maage_status result)
{ return (maage_status_severity_value)(result >> 30); }
int maage_status_is_success(maage_status result) { return maage_status_severity(result) == MAAGE_SEVERITY_SUCCESS; }
int maage_status_is_warning(maage_status result) { return maage_status_severity(result) == MAAGE_SEVERITY_WARNING; }
int maage_status_is_error(maage_status result) { return maage_status_severity(result) == MAAGE_SEVERITY_ERROR; }
int maage_status_is_fatal(maage_status result) { return maage_status_severity(result) == MAAGE_SEVERITY_FATAL; }
int maage_status_failed(maage_status result) { return maage_status_severity(result) >= MAAGE_SEVERITY_ERROR; }

const maage_status_definition *maage_status_resolve(unsigned char code)
{
#define MAAGE_STATUS_DEFINITION(value, description) \
    case MAAGE_STATUS_##value: { \
        static const maage_status_definition definition = { MAAGE_STATUS_##value, #value, description }; \
        return &definition; \
    }
    switch (code) {
        MAAGE_STATUS_DEFINITION(SUCCESS, "Operation completed successfully.")
        MAAGE_STATUS_DEFINITION(NOOP, "Operation completed without performing any action.")
        MAAGE_STATUS_DEFINITION(WARNING, "Operation completed with an unexpected or undesirable result.")
        MAAGE_STATUS_DEFINITION(PARTIAL, "Operation completed partially; output may be incomplete.")
        MAAGE_STATUS_DEFINITION(DEPRECATED, "Operation used deprecated behavior.")
        MAAGE_STATUS_DEFINITION(ERROR, "Generic recoverable failure.")
        MAAGE_STATUS_DEFINITION(INVALID_ARGUMENT, "Invalid or out-of-range argument.")
        MAAGE_STATUS_DEFINITION(INVALID_STATE, "Invalid or inconsistent system state.")
        MAAGE_STATUS_DEFINITION(MALFORMED_INPUT, "Malformed input.")
        MAAGE_STATUS_DEFINITION(ACCESS_DENIED, "Required resource denied access.")
        MAAGE_STATUS_DEFINITION(NOT_IMPLEMENTED, "Unsupported or unimplemented behavior.")
        MAAGE_STATUS_DEFINITION(SYSTEM_NOT_SUPPORTED, "Unsupported system or configuration.")
        MAAGE_STATUS_DEFINITION(TIMEOUT, "Required operation timed out.")
        MAAGE_STATUS_DEFINITION(NOT_FOUND, "Required resource does not exist.")
        MAAGE_STATUS_DEFINITION(INTERRUPTED, "External interruption.")
        MAAGE_STATUS_DEFINITION(DEPENDENCY_FAILURE, "Required dependency failed.")
        MAAGE_STATUS_DEFINITION(BUFFER_OVERFLOW, "Buffer insufficient for required data.")
        MAAGE_STATUS_DEFINITION(ALLOCATION_FAILED, "Allocation failed.")
        MAAGE_STATUS_DEFINITION(IO_ERROR, "I/O failure.")
        MAAGE_STATUS_DEFINITION(FATAL, "Generic unrecoverable failure.")
        MAAGE_STATUS_DEFINITION(INVARIANT_VIOLATION, "Fundamental invariant violation or corruption.")
        default: return NULL;
    }
#undef MAAGE_STATUS_DEFINITION
}
const char *maage_status_context_name(unsigned char context)
{
    switch (context) {
        case MAAGE_CONTEXT_NONE: return "NONE";
        case MAAGE_CONTEXT_RESOURCE: return "RESOURCE";
        case MAAGE_CONTEXT_RENDER: return "RENDER";
        case MAAGE_CONTEXT_WORLD_IO: return "WORLD_IO";
        case MAAGE_CONTEXT_GUI: return "GUI";
        case MAAGE_CONTEXT_SCHEDULER: return "SCHEDULER";
        case MAAGE_CONTEXT_WORLD: return "WORLD";
        default: return "UNKNOWN";
    }
}
const char *maage_status_operation_name(unsigned char context, unsigned char operation)
{
    unsigned int allowed;
    /* Twelve immutable operation bits per scope; no registration-time mutation. */
    switch (context) {
        case MAAGE_CONTEXT_NONE: allowed = 0x1FFE; break;
        case MAAGE_CONTEXT_RESOURCE: allowed = 0x5E; break;
        case MAAGE_CONTEXT_RENDER: allowed = 0x60; break;
        case MAAGE_CONTEXT_WORLD_IO: allowed = 0x180; break;
        case MAAGE_CONTEXT_GUI: allowed = 0x54; break;
        case MAAGE_CONTEXT_SCHEDULER: allowed = 0x440; break;
        case MAAGE_CONTEXT_WORLD: allowed = 0x1E40; break;
        default: return "UNKNOWN";
    }
    if (operation > 12 || !(allowed & (1U << operation))) return "UNKNOWN";
    switch (operation) {
        case MAAGE_OPERATION_LOOKUP: return "LOOKUP";
        case MAAGE_OPERATION_LOAD_TEXTURE: return "LOAD_TEXTURE";
        case MAAGE_OPERATION_DECODE: return "DECODE";
        case MAAGE_OPERATION_ATLAS: return "ATLAS";
        case MAAGE_OPERATION_UPLOAD: return "UPLOAD";
        case MAAGE_OPERATION_INITIALIZE: return "INITIALIZE";
        case MAAGE_OPERATION_LOAD: return "LOAD";
        case MAAGE_OPERATION_SAVE: return "SAVE";
        case MAAGE_OPERATION_MESH: return "MESH";
        case MAAGE_OPERATION_JOB: return "JOB";
        case MAAGE_OPERATION_GENERATE: return "GENERATE";
        case MAAGE_OPERATION_LIGHT: return "LIGHT";
        default: return "UNKNOWN";
    }
}
static void maage_status_append(char *buffer, size_t capacity, size_t *length, const char *text)
{
    size_t added = strlen(text), available;
    if (buffer && capacity && *length < capacity - 1) {
        available = capacity - 1 - *length;
        if (available > added) available = added;
        memcpy(buffer + *length, text, available);
        buffer[*length + available] = '\0';
    }
    *length += added;
}
size_t maage_status_format(maage_status result, char *buffer, size_t capacity)
{
    static const char *const severities[] = {"SUCCESS", "WARNING", "ERROR", "FATAL"};
    const maage_status_definition *definition = maage_status_resolve(maage_status_code(result));
    const maage_status_definition *dependency;
    char number[48];
    unsigned char context = maage_status_context(result), detail = maage_status_detail(result);
    size_t length = 0;
    if (buffer && capacity) buffer[0] = '\0';
    sprintf(number, "0x%08lX - ", (unsigned long)result);
    maage_status_append(buffer, capacity, &length, number);
    maage_status_append(buffer, capacity, &length, severities[maage_status_severity(result)]);
    maage_status_append(buffer, capacity, &length, " / ");
    maage_status_append(buffer, capacity, &length, maage_status_context_name(context));
    maage_status_append(buffer, capacity, &length, " / ");
    maage_status_append(buffer, capacity, &length, maage_status_operation_name(context, maage_status_operation(result)));
    maage_status_append(buffer, capacity, &length, " - ");
    maage_status_append(buffer, capacity, &length, definition ? definition->name : "UNKNOWN");
    maage_status_append(buffer, capacity, &length, ": ");
    maage_status_append(buffer, capacity, &length, definition ? definition->message : "Reserved status code.");
    if (maage_status_code(result) == MAAGE_STATUS_INVALID_ARGUMENT) {
        sprintf(number, " Argument index: %u.", (unsigned int)detail);
        maage_status_append(buffer, capacity, &length, number);
    } else if (maage_status_code(result) == MAAGE_STATUS_DEPENDENCY_FAILURE) {
        dependency = maage_status_resolve(detail);
        sprintf(number, " Dependency: 0x%02X ", (unsigned int)detail);
        maage_status_append(buffer, capacity, &length, number);
        maage_status_append(buffer, capacity, &length, dependency ? dependency->name : "UNKNOWN");
    } else if ((context == MAAGE_CONTEXT_SCHEDULER || context == MAAGE_CONTEXT_WORLD) &&
        (detail == MAAGE_DETAIL_CANCELLED || detail == MAAGE_DETAIL_STALE)) {
        maage_status_append(buffer, capacity, &length, detail == MAAGE_DETAIL_CANCELLED ? " Cancelled." : " Stale result.");
    } else if (detail) {
        sprintf(number, " Unknown detail: 0x%02X.", (unsigned int)detail);
        maage_status_append(buffer, capacity, &length, number);
    }
    return length;
}
