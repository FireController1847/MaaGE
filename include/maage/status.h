#ifndef MAAGE_STATUS_H
#define MAAGE_STATUS_H
#include <limits.h>
#include <stddef.h>

/* Authoritative Operation Status registry. Values are stable; unlisted status,
 * context and operation bytes are reserved and resolve as UNKNOWN. */
#if CHAR_BIT != 8
#error MaaGE Operation Status requires eight-bit bytes.
#endif
#if UINT_MAX == 0xFFFFFFFFUL
typedef unsigned int maage_status;
#elif ULONG_MAX == 0xFFFFFFFFUL
typedef unsigned long maage_status;
#else
#error MaaGE Operation Status requires an unsigned 32-bit integer type.
#endif
typedef char maage_status_requires_32_bits[sizeof(maage_status) == 4 ? 1 : -1];

#ifdef __cplusplus
extern "C" {
#endif

typedef enum maage_status_code_value {
    MAAGE_STATUS_SUCCESS = 0x00, /* Operation completed successfully. */
    MAAGE_STATUS_NOOP = 0x01, /* Completed without performing any action. */
    MAAGE_STATUS_WARNING = 0x40, /* Completed with an undesirable result. */
    MAAGE_STATUS_PARTIAL = 0x41, /* Completed partially; output may be incomplete. */
    MAAGE_STATUS_DEPRECATED = 0x42, /* Used deprecated behavior. */
    MAAGE_STATUS_ERROR = 0x80, /* Generic recoverable failure. */
    MAAGE_STATUS_INVALID_ARGUMENT = 0x81, /* Detail: zero-based argument index. */
    MAAGE_STATUS_INVALID_STATE = 0x82, /* Invalid or inconsistent state. */
    MAAGE_STATUS_MALFORMED_INPUT = 0x83, /* Malformed input. */
    MAAGE_STATUS_ACCESS_DENIED = 0x84, /* Required resource denied access. */
    MAAGE_STATUS_NOT_IMPLEMENTED = 0x85, /* Unsupported/unimplemented behavior. */
    MAAGE_STATUS_SYSTEM_NOT_SUPPORTED = 0x86, /* Unsupported system/configuration. */
    MAAGE_STATUS_TIMEOUT = 0x87, /* Required operation timed out. */
    MAAGE_STATUS_NOT_FOUND = 0x88, /* Required resource does not exist. */
    MAAGE_STATUS_INTERRUPTED = 0x89, /* External interruption. */
    MAAGE_STATUS_DEPENDENCY_FAILURE = 0x8A, /* Detail: dependency's status byte. */
    MAAGE_STATUS_BUFFER_OVERFLOW = 0x90, /* Buffer insufficient for required data. */
    MAAGE_STATUS_ALLOCATION_FAILED = 0x91, /* Allocation failed. */
    MAAGE_STATUS_IO_ERROR = 0xA0, /* I/O failure. */
    MAAGE_STATUS_FATAL = 0xC0, /* Generic unrecoverable failure. */
    MAAGE_STATUS_INVARIANT_VIOLATION = 0xC1 /* Critical logic error/corruption. */
} maage_status_code_value;
typedef enum maage_status_severity_value {
    MAAGE_SEVERITY_SUCCESS, MAAGE_SEVERITY_WARNING,
    MAAGE_SEVERITY_ERROR, MAAGE_SEVERITY_FATAL
} maage_status_severity_value;

/* Success-level results always have context zero. All other contexts identify
 * the originating subsystem; platform error numbers/paths/IDs belong separately
 * in logs, never truncated into a detail byte. */
typedef enum maage_status_context_value {
    MAAGE_CONTEXT_NONE = 0, MAAGE_CONTEXT_CORE = 0x01,
    MAAGE_CONTEXT_STORAGE = 0x02, MAAGE_CONTEXT_THREAD = 0x03,
    MAAGE_CONTEXT_PROCESS = 0x04, MAAGE_CONTEXT_DOWNLOAD = 0x05,
    MAAGE_CONTEXT_INSTANCE = 0x06, MAAGE_CONTEXT_WINDOW = 0x07,
    MAAGE_CONTEXT_INPUT = 0x08, MAAGE_CONTEXT_CLIENT = 0x09,
    MAAGE_CONTEXT_LAUNCHER = 0x0A, MAAGE_CONTEXT_SERVER = 0x0B,
    MAAGE_CONTEXT_AUTH = 0x0C, MAAGE_CONTEXT_CONFIG = 0x0D,
    MAAGE_CONTEXT_REGISTRY = 0x0E, MAAGE_CONTEXT_RESOURCE = 0x10,
    MAAGE_CONTEXT_AUDIO = 0x11,
    MAAGE_CONTEXT_RENDER = 0x20, MAAGE_CONTEXT_WORLD_IO = 0x30,
    MAAGE_CONTEXT_GUI = 0x40, MAAGE_CONTEXT_SCHEDULER = 0x50,
    MAAGE_CONTEXT_WORLD = 0x60
} maage_status_context_value;

/* Operation numbers have the same meaning in every registered subsystem.
 * Context identifies the owner; it is explicitly retained in trace frames even
 * for successful outcomes (whose compact result context is necessarily zero).
 * Unknown context or operation bytes remain numeric UNKNOWN diagnostics. */
typedef enum maage_status_operation_value {
    MAAGE_OPERATION_LOOKUP = 1, MAAGE_OPERATION_LOAD_TEXTURE = 2,
    MAAGE_OPERATION_DECODE = 3, MAAGE_OPERATION_ATLAS = 4,
    MAAGE_OPERATION_UPLOAD = 5, MAAGE_OPERATION_INITIALIZE = 6,
    MAAGE_OPERATION_LOAD = 7, MAAGE_OPERATION_SAVE = 8,
    MAAGE_OPERATION_MESH = 9, MAAGE_OPERATION_JOB = 10,
    MAAGE_OPERATION_GENERATE = 11, MAAGE_OPERATION_LIGHT = 12,
    MAAGE_OPERATION_DESTROY = 13, MAAGE_OPERATION_VALIDATE = 14,
    MAAGE_OPERATION_REGISTER = 15, MAAGE_OPERATION_FINALIZE = 16,
    MAAGE_OPERATION_UPDATE = 17, MAAGE_OPERATION_QUERY = 18,
    MAAGE_OPERATION_CREATE = 19, MAAGE_OPERATION_OPEN = 20,
    MAAGE_OPERATION_CLOSE = 21, MAAGE_OPERATION_READ = 22,
    MAAGE_OPERATION_WRITE = 23, MAAGE_OPERATION_REMOVE = 24,
    MAAGE_OPERATION_COPY = 25, MAAGE_OPERATION_RENAME = 26,
    MAAGE_OPERATION_ACQUIRE = 27, MAAGE_OPERATION_RELEASE = 28,
    MAAGE_OPERATION_SUBMIT = 29, MAAGE_OPERATION_CANCEL = 30,
    MAAGE_OPERATION_POLL = 31, MAAGE_OPERATION_WAIT = 32,
    MAAGE_OPERATION_STOP = 33, MAAGE_OPERATION_START = 34,
    MAAGE_OPERATION_SELECT = 35, MAAGE_OPERATION_AUTHENTICATE = 36,
    MAAGE_OPERATION_VERIFY = 37, MAAGE_OPERATION_DOWNLOAD = 38,
    MAAGE_OPERATION_RELOAD = 39, MAAGE_OPERATION_NAVIGATE = 40,
    MAAGE_OPERATION_MUTATE = 41, MAAGE_OPERATION_TICK = 42,
    MAAGE_OPERATION_RAYCAST = 43, MAAGE_OPERATION_HASH = 44,
    MAAGE_OPERATION_COMPRESS = 45, MAAGE_OPERATION_PARSE = 46,
    MAAGE_OPERATION_FORMAT = 47, MAAGE_OPERATION_RESOLVE = 48,
    MAAGE_OPERATION_RESIZE = 49, MAAGE_OPERATION_PRESENT = 50,
    MAAGE_OPERATION_SET = 51, MAAGE_OPERATION_SNAPSHOT = 52,
    MAAGE_OPERATION_MIGRATE = 53,
    MAAGE_OPERATION_TERRAIN = 54, MAAGE_OPERATION_CAVES = 55,
    MAAGE_OPERATION_ORES = 56, MAAGE_OPERATION_SURFACE = 57,
    MAAGE_OPERATION_VEGETATION = 58, MAAGE_OPERATION_STRUCTURES = 59
} maage_status_operation_value;

/* Detail interpretation: status first (INVALID_ARGUMENT, DEPENDENCY_FAILURE),
 * then context (WORLD/SCHEDULER: 1 cancelled, 2 stale), then operation.
 * No other context/operation detail values are assigned; those are unknown. */
#define MAAGE_DETAIL_ARGUMENT_UNKNOWN 255 /* INVALID_ARGUMENT only. */
#define MAAGE_DETAIL_CANCELLED 1
#define MAAGE_DETAIL_STALE 2
#define MAAGE_DETAIL_WRONG_THREAD 3 /* WINDOW context. */
#define MAAGE_DETAIL_AUDIO_DEVICE_UNAVAILABLE 1 /* AUDIO context. */
#define MAAGE_DETAIL_AUDIO_DEVICE_LOST 2 /* AUDIO context. */
#define MAAGE_DETAIL_DEVICE_LOST 3 /* RENDER context. */
#define MAAGE_DETAIL_SURFACE_LOST 4 /* RENDER context. */
#define MAAGE_DETAIL_SURFACE_UNAVAILABLE 5 /* RENDER context. */
#define MAAGE_DETAIL_WORLD_FORMAT 1 /* WORLD_IO / MALFORMED_INPUT / LOAD. */

typedef struct maage_status_definition {
    unsigned char code;
    const char *name;
    const char *message;
} maage_status_definition;

/* Numeric layout is 0xSSCCOODD, independent of host byte order. make enforces
 * context zero for all success codes, including reserved success-range values.
 * Byte serialization is explicitly SS, CC, OO, DD (most significant first),
 * never native integer/struct layout. from_bytes preserves the exact bits. */
#define MAAGE_STATUS_VALUE(status, context, operation, detail) \
    (((maage_status)(status) << 24) | \
     ((maage_status)((status) < 0x40 ? 0 : (context)) << 16) | \
     ((maage_status)(operation) << 8) | (maage_status)(detail))
maage_status maage_status_make(unsigned char status, unsigned char context,
    unsigned char operation, unsigned char detail);
unsigned char maage_status_code(maage_status result);
unsigned char maage_status_context(maage_status result);
unsigned char maage_status_operation(maage_status result);
unsigned char maage_status_detail(maage_status result);
void maage_status_to_bytes(maage_status result, unsigned char bytes[4]);
maage_status maage_status_from_bytes(const unsigned char bytes[4]);
maage_status_severity_value maage_status_severity(maage_status result);
int maage_status_is_success(maage_status result);
int maage_status_is_warning(maage_status result);
int maage_status_is_error(maage_status result);
int maage_status_is_fatal(maage_status result);
int maage_status_failed(maage_status result); /* Error or Fatal; warnings usable. */
/* Immutable built-in definitions; NULL for reserved status values. */
const maage_status_definition *maage_status_resolve(unsigned char code);
const char *maage_status_string(maage_status result);
const char *maage_status_context_name(unsigned char context);
const char *maage_status_operation_name(unsigned char context, unsigned char operation);
/* Returns required bytes excluding NUL, always terminates a nonempty buffer.
 * NULL/zero capacity may measure the result. No allocations, locks or I/O. */
size_t maage_status_format(maage_status result, char *buffer, size_t capacity);

/* Native values remain separate, never truncated into the detail byte. The
 * translators are portable numeric boundaries; zero translates to SUCCESS. */
maage_status maage_status_from_errno(int native_code, unsigned char context,
    unsigned char operation);
maage_status maage_status_from_win32(unsigned long native_code,
    unsigned char context, unsigned char operation);

/* Caller/task-owned bounded causal record, copied by value across jobs. It
 * owns no heap or dynamic strings: source names MUST have static lifetime.
 * record is also useful for a few successful dispatch/entry breadcrumbs, but
 * ordinary successful hot calls need not record anything. origin retains the
 * first warning/error/fatal result even after forwarding/recovery/disposition.
 * Full traces retain origin and a dropped count, never grow or overwrite it.
 * Only the current owner writes a trace; handoff uses existing task queues.
 * Frames are chronological logical operations, not native unwind frames.
 * Unused traces cost no formatting, lock, I/O or allocation. Initialize before
 * use and reset after each handled occurrence, before unrelated operations.
 * Reset clears metadata only; frames beyond count are never read. */
#define MAAGE_STATUS_TRACE_CAPACITY 12U
typedef struct maage_status_trace_frame {
    maage_status result;
    const char *file;
    unsigned long line;
    long native_code;
    unsigned char context, operation;
} maage_status_trace_frame;
typedef struct maage_status_trace {
    maage_status origin, outcome, recovery;
    unsigned int count, dropped;
    int recovered;
    maage_status_trace_frame frames[MAAGE_STATUS_TRACE_CAPACITY];
} maage_status_trace;
void maage_status_trace_init(maage_status_trace *trace);
maage_status maage_status_trace_record(maage_status_trace *trace,
    maage_status result, unsigned char context, unsigned char operation,
    const char *static_file, unsigned long line, long native_code);
maage_status maage_status_trace_propagate(maage_status_trace *trace,
    maage_status result, unsigned char context, unsigned char operation,
    const char *static_file, unsigned long line);
maage_status maage_status_trace_recover(maage_status_trace *trace,
    maage_status recovery, unsigned char context, unsigned char operation,
    const char *static_file, unsigned long line);
/* Appends origin, chronological causal frames, native/source context and
 * recovery. Returns required characters; always NUL-terminates nonempty output.
 * Formatting is explicit and allocation-free, including in Release builds. */
size_t maage_status_trace_format(const maage_status_trace *trace,
    char *buffer, size_t capacity);
#define MAAGE_STATUS_TRACE(trace, result, context, operation) \
    maage_status_trace_record((trace), (result), (context), (operation), \
        __FILE__, (unsigned long)__LINE__, 0)
#define MAAGE_STATUS_PROPAGATE(trace, result, context, operation) \
    maage_status_trace_propagate((trace), (result), (context), (operation), \
        __FILE__, (unsigned long)__LINE__)
#define MAAGE_STATUS_TRACE_RETURN(trace, expression, context, operation) do { \
    maage_status maage_status_propagated = (expression); \
    if (!maage_status_is_success(maage_status_propagated)) \
        return MAAGE_STATUS_TRACE((trace), maage_status_propagated, (context), (operation)); \
} while (0)

/* For functions returning maage_status. Evaluate once, preserve every field. */
#define MAAGE_STATUS_RETURN_IF_FAILED(expression) do { \
    maage_status maage_status_propagated = (expression); \
    if (maage_status_failed(maage_status_propagated)) return maage_status_propagated; \
} while (0)

#ifdef __cplusplus
}
#endif
#endif
