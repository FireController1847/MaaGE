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
    MAAGE_CONTEXT_NONE = 0, MAAGE_CONTEXT_RESOURCE = 0x10,
    MAAGE_CONTEXT_RENDER = 0x20, MAAGE_CONTEXT_WORLD_IO = 0x30,
    MAAGE_CONTEXT_GUI = 0x40, MAAGE_CONTEXT_SCHEDULER = 0x50,
    MAAGE_CONTEXT_WORLD = 0x60
} maage_status_context_value;

/* Operations are shared by these scopes: RESOURCE lookup/load_texture/decode/
 * atlas/initialize; RENDER upload/initialize; WORLD_IO load/save;
 * GUI load_texture/atlas/initialize; SCHEDULER initialize/job;
 * WORLD initialize/mesh/job/generate/light. With context NONE, these identify a
 * successful operation without subsystem context. Other pairs are UNKNOWN. */
typedef enum maage_status_operation_value {
    MAAGE_OPERATION_LOOKUP = 1, MAAGE_OPERATION_LOAD_TEXTURE = 2,
    MAAGE_OPERATION_DECODE = 3, MAAGE_OPERATION_ATLAS = 4,
    MAAGE_OPERATION_UPLOAD = 5, MAAGE_OPERATION_INITIALIZE = 6,
    MAAGE_OPERATION_LOAD = 7, MAAGE_OPERATION_SAVE = 8,
    MAAGE_OPERATION_MESH = 9, MAAGE_OPERATION_JOB = 10,
    MAAGE_OPERATION_GENERATE = 11, MAAGE_OPERATION_LIGHT = 12
} maage_status_operation_value;

/* Detail interpretation: status first (INVALID_ARGUMENT, DEPENDENCY_FAILURE),
 * then context (WORLD/SCHEDULER: 1 cancelled, 2 stale), then operation.
 * No other context/operation detail values are assigned; those are unknown. */
#define MAAGE_DETAIL_CANCELLED 1
#define MAAGE_DETAIL_STALE 2

typedef struct maage_status_definition {
    unsigned char code;
    const char *name;
    const char *message;
} maage_status_definition;

/* Numeric layout is 0xSSCCOODD, independent of host byte order. make enforces
 * context zero for all success codes, including reserved success-range values.
 * Byte serialization is explicitly SS, CC, OO, DD (most significant first),
 * never native integer/struct layout. from_bytes preserves the exact bits. */
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
const char *maage_status_context_name(unsigned char context);
const char *maage_status_operation_name(unsigned char context, unsigned char operation);
/* Returns required bytes excluding NUL, always terminates a nonempty buffer.
 * NULL/zero capacity may measure the result. No allocations, locks or I/O. */
size_t maage_status_format(maage_status result, char *buffer, size_t capacity);

/* For functions returning maage_status. Evaluate once, preserve every field. */
#define MAAGE_STATUS_RETURN_IF_FAILED(expression) do { \
    maage_status maage_status_propagated = (expression); \
    if (maage_status_failed(maage_status_propagated)) return maage_status_propagated; \
} while (0)

#ifdef __cplusplus
}
#endif
#endif
