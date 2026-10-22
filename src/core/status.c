#include "maage/status.h"
#include <stdio.h>
#include <string.h>
#include <errno.h>

maage_status maage_status_make(unsigned char status, unsigned char context,
    unsigned char operation, unsigned char detail)
{
    return MAAGE_STATUS_VALUE(status, context, operation, detail);
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
const char *maage_status_string(maage_status result)
{
    const maage_status_definition *definition = maage_status_resolve(maage_status_code(result));
    return definition ? definition->message : "Unknown Operation Status";
}
const char *maage_status_context_name(unsigned char context)
{
    switch (context) {
        case MAAGE_CONTEXT_NONE: return "NONE";
        case MAAGE_CONTEXT_CORE: return "CORE";
        case MAAGE_CONTEXT_STORAGE: return "STORAGE";
        case MAAGE_CONTEXT_THREAD: return "THREAD";
        case MAAGE_CONTEXT_PROCESS: return "PROCESS";
        case MAAGE_CONTEXT_DOWNLOAD: return "DOWNLOAD";
        case MAAGE_CONTEXT_INSTANCE: return "INSTANCE";
        case MAAGE_CONTEXT_WINDOW: return "WINDOW";
        case MAAGE_CONTEXT_INPUT: return "INPUT";
        case MAAGE_CONTEXT_CLIENT: return "CLIENT";
        case MAAGE_CONTEXT_LAUNCHER: return "LAUNCHER";
        case MAAGE_CONTEXT_SERVER: return "SERVER";
        case MAAGE_CONTEXT_AUTH: return "AUTH";
        case MAAGE_CONTEXT_CONFIG: return "CONFIG";
        case MAAGE_CONTEXT_REGISTRY: return "REGISTRY";
        case MAAGE_CONTEXT_RESOURCE: return "RESOURCE";
        case MAAGE_CONTEXT_AUDIO: return "AUDIO";
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
    if (context > MAAGE_CONTEXT_REGISTRY && context != MAAGE_CONTEXT_RESOURCE &&
        context != MAAGE_CONTEXT_AUDIO && context != MAAGE_CONTEXT_RENDER && context != MAAGE_CONTEXT_WORLD_IO &&
        context != MAAGE_CONTEXT_GUI && context != MAAGE_CONTEXT_SCHEDULER &&
        context != MAAGE_CONTEXT_WORLD) return "UNKNOWN";
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
        case MAAGE_OPERATION_DESTROY: return "DESTROY";
        case MAAGE_OPERATION_VALIDATE: return "VALIDATE";
        case MAAGE_OPERATION_REGISTER: return "REGISTER";
        case MAAGE_OPERATION_FINALIZE: return "FINALIZE";
        case MAAGE_OPERATION_UPDATE: return "UPDATE";
        case MAAGE_OPERATION_QUERY: return "QUERY";
        case MAAGE_OPERATION_CREATE: return "CREATE";
        case MAAGE_OPERATION_OPEN: return "OPEN";
        case MAAGE_OPERATION_CLOSE: return "CLOSE";
        case MAAGE_OPERATION_READ: return "READ";
        case MAAGE_OPERATION_WRITE: return "WRITE";
        case MAAGE_OPERATION_REMOVE: return "REMOVE";
        case MAAGE_OPERATION_COPY: return "COPY";
        case MAAGE_OPERATION_RENAME: return "RENAME";
        case MAAGE_OPERATION_ACQUIRE: return "ACQUIRE";
        case MAAGE_OPERATION_RELEASE: return "RELEASE";
        case MAAGE_OPERATION_SUBMIT: return "SUBMIT";
        case MAAGE_OPERATION_CANCEL: return "CANCEL";
        case MAAGE_OPERATION_POLL: return "POLL";
        case MAAGE_OPERATION_WAIT: return "WAIT";
        case MAAGE_OPERATION_STOP: return "STOP";
        case MAAGE_OPERATION_START: return "START";
        case MAAGE_OPERATION_SELECT: return "SELECT";
        case MAAGE_OPERATION_AUTHENTICATE: return "AUTHENTICATE";
        case MAAGE_OPERATION_VERIFY: return "VERIFY";
        case MAAGE_OPERATION_DOWNLOAD: return "DOWNLOAD";
        case MAAGE_OPERATION_RELOAD: return "RELOAD";
        case MAAGE_OPERATION_NAVIGATE: return "NAVIGATE";
        case MAAGE_OPERATION_MUTATE: return "MUTATE";
        case MAAGE_OPERATION_TICK: return "TICK";
        case MAAGE_OPERATION_RAYCAST: return "RAYCAST";
        case MAAGE_OPERATION_HASH: return "HASH";
        case MAAGE_OPERATION_COMPRESS: return "COMPRESS";
        case MAAGE_OPERATION_PARSE: return "PARSE";
        case MAAGE_OPERATION_FORMAT: return "FORMAT";
        case MAAGE_OPERATION_RESOLVE: return "RESOLVE";
        case MAAGE_OPERATION_RESIZE: return "RESIZE";
        case MAAGE_OPERATION_PRESENT: return "PRESENT";
        case MAAGE_OPERATION_SET: return "SET";
        case MAAGE_OPERATION_SNAPSHOT: return "SNAPSHOT";
        case MAAGE_OPERATION_MIGRATE: return "MIGRATE";
        case MAAGE_OPERATION_TERRAIN: return "TERRAIN";
        case MAAGE_OPERATION_CAVES: return "CAVES";
        case MAAGE_OPERATION_ORES: return "ORES";
        case MAAGE_OPERATION_SURFACE: return "SURFACE";
        case MAAGE_OPERATION_VEGETATION: return "VEGETATION";
        case MAAGE_OPERATION_STRUCTURES: return "STRUCTURES";
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
        if (detail == MAAGE_DETAIL_ARGUMENT_UNKNOWN)
            maage_status_append(buffer, capacity, &length, " Argument index unknown.");
        else {
            sprintf(number, " Argument index: %u.", (unsigned int)detail);
            maage_status_append(buffer, capacity, &length, number);
        }
    } else if (maage_status_code(result) == MAAGE_STATUS_DEPENDENCY_FAILURE) {
        dependency = maage_status_resolve(detail);
        sprintf(number, " Dependency: 0x%02X ", (unsigned int)detail);
        maage_status_append(buffer, capacity, &length, number);
        maage_status_append(buffer, capacity, &length, dependency ? dependency->name : "UNKNOWN");
    } else if ((context == MAAGE_CONTEXT_SCHEDULER || context == MAAGE_CONTEXT_WORLD) &&
        (detail == MAAGE_DETAIL_CANCELLED || detail == MAAGE_DETAIL_STALE)) {
        maage_status_append(buffer, capacity, &length, detail == MAAGE_DETAIL_CANCELLED ? " Cancelled." : " Stale result.");
    } else if (context == MAAGE_CONTEXT_WINDOW && detail == MAAGE_DETAIL_WRONG_THREAD) {
        maage_status_append(buffer, capacity, &length, " Wrong owning thread.");
    } else if (context == MAAGE_CONTEXT_AUDIO && detail == MAAGE_DETAIL_AUDIO_DEVICE_UNAVAILABLE) {
        maage_status_append(buffer, capacity, &length, " Audio device unavailable.");
    } else if (context == MAAGE_CONTEXT_AUDIO && detail == MAAGE_DETAIL_AUDIO_DEVICE_LOST) {
        maage_status_append(buffer, capacity, &length, " Audio output failed.");
    } else if (context == MAAGE_CONTEXT_RENDER && detail == MAAGE_DETAIL_DEVICE_LOST) {
        maage_status_append(buffer, capacity, &length, " Device lost.");
    } else if (context == MAAGE_CONTEXT_RENDER && detail == MAAGE_DETAIL_SURFACE_LOST) {
        maage_status_append(buffer, capacity, &length, " Surface lost.");
    } else if (context == MAAGE_CONTEXT_RENDER && detail == MAAGE_DETAIL_SURFACE_UNAVAILABLE) {
        maage_status_append(buffer, capacity, &length, " Native surface unavailable.");
    } else if (detail) {
        sprintf(number, " Unknown detail: 0x%02X.", (unsigned int)detail);
        maage_status_append(buffer, capacity, &length, number);
    }
    return length;
}

maage_status maage_status_from_errno(int native_code, unsigned char context,
    unsigned char operation)
{
    unsigned char code = MAAGE_STATUS_IO_ERROR;
    if (!native_code) code = MAAGE_STATUS_SUCCESS;
    else if (native_code == ENOENT || native_code == ENOTDIR) code = MAAGE_STATUS_NOT_FOUND;
    else if (native_code == EACCES || native_code == EPERM) code = MAAGE_STATUS_ACCESS_DENIED;
    else if (native_code == EINVAL) code = MAAGE_STATUS_INVALID_ARGUMENT;
    else if (native_code == ENOMEM) code = MAAGE_STATUS_ALLOCATION_FAILED;
    else if (native_code == EINTR) code = MAAGE_STATUS_INTERRUPTED;
#ifdef ETIMEDOUT
    else if (native_code == ETIMEDOUT) code = MAAGE_STATUS_TIMEOUT;
#endif
#ifdef ENOSYS
    else if (native_code == ENOSYS) code = MAAGE_STATUS_NOT_IMPLEMENTED;
#endif
#ifdef ENOTSUP
    else if (native_code == ENOTSUP) code = MAAGE_STATUS_SYSTEM_NOT_SUPPORTED;
#endif
#ifdef EOVERFLOW
    else if (native_code == EOVERFLOW) code = MAAGE_STATUS_BUFFER_OVERFLOW;
#endif
#ifdef EBUSY
    else if (native_code == EBUSY) code = MAAGE_STATUS_INVALID_STATE;
#endif
    return maage_status_make(code, context, operation, code == MAAGE_STATUS_INVALID_ARGUMENT ? MAAGE_DETAIL_ARGUMENT_UNKNOWN : 0);
}
maage_status maage_status_from_win32(unsigned long native_code,
    unsigned char context, unsigned char operation)
{
    unsigned char code;
    /* Win32 error numbers are wire-sized numeric inputs; native headers/types
     * stay in the platform adapter. Preserve the full original in a trace. */
    switch (native_code) {
    case 0: code = MAAGE_STATUS_SUCCESS; break;
    case 2: case 3: code = MAAGE_STATUS_NOT_FOUND; break;
    case 5: code = MAAGE_STATUS_ACCESS_DENIED; break;
    case 8: case 14: code = MAAGE_STATUS_ALLOCATION_FAILED; break;
    case 50: case 120: code = MAAGE_STATUS_NOT_IMPLEMENTED; break;
    case 87: code = MAAGE_STATUS_INVALID_ARGUMENT; break;
    case 122: case 234: code = MAAGE_STATUS_BUFFER_OVERFLOW; break;
    case 995: code = MAAGE_STATUS_INTERRUPTED; break;
    case 1460: code = MAAGE_STATUS_TIMEOUT; break;
    default: code = MAAGE_STATUS_IO_ERROR; break;
    }
    return maage_status_make(code, context, operation, code == MAAGE_STATUS_INVALID_ARGUMENT ? MAAGE_DETAIL_ARGUMENT_UNKNOWN : 0);
}
void maage_status_trace_init(maage_status_trace *trace)
{
    if (!trace) return;
    trace->origin = trace->outcome = trace->recovery = (maage_status)0;
    trace->count = trace->dropped = 0;
    trace->recovered = 0;
}
maage_status maage_status_trace_record(maage_status_trace *trace,
    maage_status result, unsigned char context, unsigned char operation,
    const char *static_file, unsigned long line, long native_code)
{
    maage_status_trace_frame *frame;
    if (!trace) return result;
    if (maage_status_is_success(trace->origin) && !maage_status_is_success(result))
        trace->origin = result;
    trace->outcome = result;
    if (trace->count >= MAAGE_STATUS_TRACE_CAPACITY) {
        if (trace->dropped != UINT_MAX) ++trace->dropped;
        return result;
    }
    frame = &trace->frames[trace->count++];
    frame->result = result; frame->context = context; frame->operation = operation;
    frame->file = static_file; frame->line = line; frame->native_code = native_code;
    return result;
}
maage_status maage_status_trace_recover(maage_status_trace *trace,
    maage_status recovery, unsigned char context, unsigned char operation,
    const char *static_file, unsigned long line)
{
    if (trace) { trace->recovery = recovery; trace->recovered = 1; }
    return maage_status_trace_record(trace, recovery, context, operation,
        static_file, line, 0);
}
size_t maage_status_trace_format(const maage_status_trace *trace,
    char *buffer, size_t capacity)
{
    const maage_status_trace_frame *frame;
    const char *file;
    char number[112];
    size_t length = 0;
    unsigned int i, count;
    if (buffer && capacity) buffer[0] = 0;
    if (!trace) return 0;
    sprintf(number, "origin=0x%08lX", (unsigned long)trace->origin);
    maage_status_append(buffer, capacity, &length, number);
    count = trace->count < MAAGE_STATUS_TRACE_CAPACITY ? trace->count : MAAGE_STATUS_TRACE_CAPACITY;
    for (i = 0; i < count; ++i) {
        frame = &trace->frames[i];
        maage_status_append(buffer, capacity, &length, " -> ");
        maage_status_append(buffer, capacity, &length, maage_status_context_name(frame->context));
        maage_status_append(buffer, capacity, &length, "/");
        maage_status_append(buffer, capacity, &length, maage_status_operation_name(frame->context, frame->operation));
        sprintf(number, "[0x%08lX]", (unsigned long)frame->result);
        maage_status_append(buffer, capacity, &length, number);
        if (frame->native_code) {
            sprintf(number, " native=%ld", frame->native_code);
            maage_status_append(buffer, capacity, &length, number);
        }
        if (frame->file) {
            /* The basename is sufficient to find an authored source location;
             * avoid unnecessarily repeating a build machine's absolute path. */
            file = strrchr(frame->file, '/');
            file = file ? file + 1 : frame->file;
            maage_status_append(buffer, capacity, &length, "@");
            maage_status_append(buffer, capacity, &length, file);
            sprintf(number, ":%lu", frame->line);
            maage_status_append(buffer, capacity, &length, number);
        }
    }
    if (trace->dropped) {
        sprintf(number, " (+%u bounded frames omitted)", trace->dropped);
        maage_status_append(buffer, capacity, &length, number);
    }
    if (trace->recovered) {
        sprintf(number, " recovery=0x%08lX", (unsigned long)trace->recovery);
        maage_status_append(buffer, capacity, &length, number);
    }
    return length;
}

maage_status maage_status_trace_propagate(maage_status_trace *trace,
    maage_status result, unsigned char context, unsigned char operation,
    const char *static_file, unsigned long line)
{
    if (maage_status_is_success(result)) return result;
    return maage_status_trace_record(trace, result, context, operation,
        static_file, line, 0);
}
