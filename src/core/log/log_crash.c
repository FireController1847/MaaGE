/* Native fault delivery stays outside the normal logger: its lock, FILE streams
 * and formatter may be the interrupted operation. Only bounded numeric records
 * and native writes are used after a fault. OS crash reporting remains active. */
#ifdef __APPLE__
#define _DARWIN_C_SOURCE 1
#endif
#ifdef __linux__
#define _GNU_SOURCE 1
#endif
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include "log_internal.h"
#include "cmakecfg.h"
#include <limits.h>
#include <signal.h>
#include <string.h>
#include <errno.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <io.h>
typedef UINT_PTR maage_crash_word;
#else
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <sys/ucontext.h>
#include <pthread.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#if defined(__arm64__)
#include <mach/arm/thread_status.h>
#endif
#elif defined(__linux__)
#include <link.h>
#endif
typedef uintptr_t maage_crash_word;
#endif

#if defined(_M_ARM64) || defined(__aarch64__) || defined(__arm64__)
#define MAAGE_CRASH_ARCH "arm64"
#elif defined(_M_X64) || defined(__x86_64__)
#define MAAGE_CRASH_ARCH "x86_64"
#elif defined(_M_IX86) || defined(__i386__)
#define MAAGE_CRASH_ARCH "x86"
#else
#define MAAGE_CRASH_ARCH "unknown"
#endif

typedef struct maage_crash_record {
    char bytes[1024];
    size_t length;
} maage_crash_record;
static maage_crash_record maage_crash_header;
static void maage_crash_text(maage_crash_record *record, const char *text) {
    while (*text && record->length < sizeof(record->bytes) - 1)
        record->bytes[record->length++] = *text++;
}
static void maage_crash_hex(maage_crash_record *record, maage_crash_word value) {
    static const char digits[] = "0123456789abcdef";
    char reverse[(sizeof(value) * CHAR_BIT + 3) / 4];
    size_t count = 0;
    maage_crash_text(record, "0x");
    do {
        reverse[count++] = digits[value & 15U];
        value >>= 4;
    } while (value);
    while (count && record->length < sizeof(record->bytes) - 1)
        record->bytes[record->length++] = reverse[--count];
}
static void maage_crash_decimal(maage_crash_record *record, unsigned long value) {
    char reverse[sizeof(value) * CHAR_BIT];
    size_t count = 0;
    do {
        reverse[count++] = (char) ('0' + value % 10UL);
        value /= 10UL;
    } while (value);
    while (count && record->length < sizeof(record->bytes) - 1)
        record->bytes[record->length++] = reverse[--count];
}
#ifndef _WIN32
static void maage_crash_signed(maage_crash_record *record, long value) {
    unsigned long magnitude;
    if (value < 0) {
        maage_crash_text(record, "-");
        magnitude = (unsigned long) (-(value + 1)) + 1UL;
    } else magnitude = (unsigned long) value;
    maage_crash_decimal(record, magnitude);
}
#endif
#ifdef _WIN32
static void maage_crash_time(maage_crash_record *record) {
    SYSTEMTIME stamp;
    unsigned int parts[5], index;
    GetSystemTime(&stamp);
    maage_crash_text(record, " utc=");
    maage_crash_decimal(record, stamp.wYear);
    parts[0] = stamp.wMonth;
    parts[1] = stamp.wDay;
    parts[2] = stamp.wHour;
    parts[3] = stamp.wMinute;
    parts[4] = stamp.wSecond;
    for (index = 0; index < 5; ++index) {
        maage_crash_text(record, index < 2 ? "-" : index == 2 ? "T" : ":");
        if (parts[index] < 10) maage_crash_text(record, "0");
        maage_crash_decimal(record, parts[index]);
    }
    maage_crash_text(record, "Z");
}
#endif
static void maage_crash_begin(maage_crash_record *record, const char *kind, unsigned long process) {
    size_t index;
    record->length = 0;
    for (index = 0; index < maage_crash_header.length; ++index)
        record->bytes[record->length++] = maage_crash_header.bytes[index];
    maage_crash_text(record, kind);
    maage_crash_text(record, " pid=");
    maage_crash_decimal(record, process);
#ifdef _WIN32
    maage_crash_time(record);
#endif
}
static void maage_crash_registers(maage_crash_record *record, maage_crash_word pc, maage_crash_word sp, maage_crash_word fp, maage_crash_word lr, int has_lr) {
    maage_crash_text(record, " pc=");
    maage_crash_hex(record, pc);
    maage_crash_text(record, " sp=");
    maage_crash_hex(record, sp);
    maage_crash_text(record, " fp=");
    maage_crash_hex(record, fp);
    if (has_lr) {
        maage_crash_text(record, " lr=");
        maage_crash_hex(record, lr);
    }
}

#ifdef _WIN32
static HANDLE maage_crash_output;
static int maage_crash_filter_installed;
static LONG maage_crash_entered;
static void maage_crash_write(maage_crash_record *record) {
    DWORD written;
    unsigned int attempts = 0;
    size_t offset = 0;
    record->bytes[record->length++] = '\n';
    if (!maage_crash_output) return;
    while (offset < record->length && attempts++ < 8U) {
        if (!WriteFile(maage_crash_output, record->bytes + offset, (DWORD) (record->length - offset), &written, NULL) || !written) break;
        offset += written;
    }
}
static LONG WINAPI maage_crash_exception(EXCEPTION_POINTERS *exception) {
    maage_crash_record record;
    EXCEPTION_RECORD *fault;
    CONTEXT *context;
    if (exception && exception->ExceptionRecord && !InterlockedCompareExchange(&maage_crash_entered, 1, 0)) {
        fault = exception->ExceptionRecord;
        context = exception->ContextRecord;
        maage_crash_begin(&record, "Unhandled exception", GetCurrentProcessId());
        maage_crash_text(&record, " thread=");
        maage_crash_decimal(&record, GetCurrentThreadId());
        maage_crash_text(&record, " exception_code=");
        maage_crash_hex(&record, fault->ExceptionCode);
        maage_crash_text(&record, " exception_address=");
        maage_crash_hex(&record, (maage_crash_word) fault->ExceptionAddress);
        if ((fault->ExceptionCode == EXCEPTION_ACCESS_VIOLATION || fault->ExceptionCode == EXCEPTION_IN_PAGE_ERROR) && fault->NumberParameters >= 2) {
            maage_crash_text(&record, " access=");
            maage_crash_hex(&record, fault->ExceptionInformation[0]);
            maage_crash_text(&record, " fault_address=");
            maage_crash_hex(&record, fault->ExceptionInformation[1]);
        }
        if (context) {
#if defined(_M_ARM64) || defined(__aarch64__)
            maage_crash_registers(&record, context->Pc, context->Sp, context->Fp, context->Lr, 1);
#elif defined(_M_X64) || defined(__x86_64__)
            maage_crash_registers(&record, context->Rip, context->Rsp, context->Rbp, 0, 0);
#elif defined(_M_IX86) || defined(__i386__)
            maage_crash_registers(&record, context->Eip, context->Esp, context->Ebp, 0, 0);
#endif
        }
        maage_crash_write(&record);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
static maage_crash_word maage_crash_image_base(void) {
    return (maage_crash_word) GetModuleHandleW(NULL);
}
#else
static volatile sig_atomic_t maage_crash_output = -1;
static volatile sig_atomic_t maage_crash_entered;
static const int maage_crash_signals[] = {SIGABRT, SIGSEGV, SIGBUS, SIGILL, SIGFPE};
static struct sigaction maage_crash_previous_signals[5];
static unsigned char maage_crash_installed[5];
/* Only the initializing thread can own this alternate stack. Other threads
 * still report ordinary faults; exhausted/corrupt worker stacks remain OS-owned. */
static union {
    long double alignment;
    unsigned char bytes[64U * 1024U];
} maage_crash_stack;
static stack_t maage_crash_previous_stack;
static pthread_t maage_crash_stack_owner;
static int maage_crash_stack_installed;
static void maage_crash_write(maage_crash_record *record) {
    size_t offset = 0;
    ssize_t written;
    unsigned int attempts = 0;
    struct sigaction ignore_pipe, previous_pipe;
    int pipe_changed;
    int descriptor = (int) maage_crash_output;
    record->bytes[record->length++] = '\n';
    if (descriptor < 0) return;
    /* A broken fallback stderr pipe must not replace the original fault with
     * SIGPIPE. Ignore it only during the raw write, then restore its owner. */
    ignore_pipe.sa_handler = SIG_IGN;
    ignore_pipe.sa_flags = 0;
    sigemptyset(&ignore_pipe.sa_mask);
    pipe_changed = sigaction(SIGPIPE, &ignore_pipe, &previous_pipe) == 0;
    while (offset < record->length && attempts++ < 8U) {
        written = write(descriptor, record->bytes + offset, record->length - offset);
        if (written < 0 && errno == EINTR) continue;
        if (written <= 0) break;
        offset += (size_t) written;
    }
    if (pipe_changed) sigaction(SIGPIPE, &previous_pipe, NULL);
}
static void maage_crash_context(maage_crash_record *record, void *native) {
    ucontext_t *context = (ucontext_t *) native;
    if (!context) return;
#if defined(__APPLE__) && defined(__arm64__)
    if (context->uc_mcontext) maage_crash_registers(record, arm_thread_state64_get_pc(context->uc_mcontext->__ss), arm_thread_state64_get_sp(context->uc_mcontext->__ss), arm_thread_state64_get_fp(context->uc_mcontext->__ss), arm_thread_state64_get_lr(context->uc_mcontext->__ss), 1);
#elif defined(__APPLE__) && defined(__x86_64__)
    if (context->uc_mcontext) maage_crash_registers(record, context->uc_mcontext->__ss.__rip, context->uc_mcontext->__ss.__rsp, context->uc_mcontext->__ss.__rbp, 0, 0);
#elif defined(__APPLE__) && defined(__i386__)
    if (context->uc_mcontext) maage_crash_registers(record, context->uc_mcontext->__ss.__eip, context->uc_mcontext->__ss.__esp, context->uc_mcontext->__ss.__ebp, 0, 0);
#elif defined(__linux__) && defined(__aarch64__)
    maage_crash_registers(record, context->uc_mcontext.pc, context->uc_mcontext.sp, context->uc_mcontext.regs[29], context->uc_mcontext.regs[30], 1);
#elif defined(__linux__) && defined(__x86_64__)
    maage_crash_registers(record, context->uc_mcontext.gregs[REG_RIP], context->uc_mcontext.gregs[REG_RSP], context->uc_mcontext.gregs[REG_RBP], 0, 0);
#elif defined(__linux__) && defined(__i386__)
    maage_crash_registers(record, context->uc_mcontext.gregs[REG_EIP], context->uc_mcontext.gregs[REG_ESP], context->uc_mcontext.gregs[REG_EBP], 0, 0);
#endif
}
static int maage_crash_generated(int number, const siginfo_t *information) {
    if (!information || number == SIGABRT || information->si_code <= 0 || information->si_code == SI_USER) return 1;
#ifdef SI_QUEUE
    if (information->si_code == SI_QUEUE) return 1;
#endif
#ifdef SI_TKILL
    if (information->si_code == SI_TKILL) return 1;
#endif
#ifdef SI_TIMER
    if (information->si_code == SI_TIMER) return 1;
#endif
#ifdef SI_ASYNCIO
    if (information->si_code == SI_ASYNCIO) return 1;
#endif
#ifdef SI_MESGQ
    if (information->si_code == SI_MESGQ) return 1;
#endif
    return 0;
}
static void maage_crash_signal(int number, siginfo_t *information, void *native) {
    maage_crash_record record;
    struct sigaction action;
    time_t now;
    int saved_errno = errno, generated = maage_crash_generated(number, information);
    if (!maage_crash_entered) {
        maage_crash_entered = 1;
        maage_crash_begin(&record, "Fatal signal", (unsigned long) getpid());
        now = time(NULL);
        if (now >= 0 && (time_t) (unsigned long) now == now) {
            maage_crash_text(&record, " unix_seconds=");
            maage_crash_decimal(&record, (unsigned long) now);
        }
        maage_crash_text(&record, " signal=");
        maage_crash_signed(&record, number);
        if (information) {
            maage_crash_text(&record, " signal_code=");
            maage_crash_signed(&record, information->si_code);
            if ((number == SIGSEGV || number == SIGBUS) && !generated) {
                maage_crash_text(&record, " fault_address=");
                maage_crash_hex(&record, (maage_crash_word) information->si_addr);
            }
        }
        maage_crash_context(&record, native);
        maage_crash_write(&record);
    }
    /* This handler only owns previously-default dispositions. A hardware fault
     * refaults at the original instruction after return, preserving the native
     * report's context. Generated signals must instead be explicitly re-raised. */
    action.sa_handler = SIG_DFL;
    action.sa_flags = 0;
    sigemptyset(&action.sa_mask);
    if (sigaction(number, &action, NULL) < 0) _exit(128 + number);
    if (generated && raise(number) != 0) _exit(128 + number);
    errno = saved_errno;
}
#ifdef __APPLE__
static maage_crash_word maage_crash_image_base(void) {
    const struct mach_header *header = _dyld_get_image_header(0);
    const struct load_command *command;
    const struct uuid_command *uuid;
    const unsigned char *cursor;
    size_t remaining;
    unsigned int index, byte;
    char text[3];
    static const char digits[] = "0123456789abcdef";
    if (!header) return 0;
    cursor = (const unsigned char *) header + (header->magic == MH_MAGIC_64 ? sizeof(struct mach_header_64) : sizeof(struct mach_header));
    remaining = header->sizeofcmds;
    for (index = 0; index < header->ncmds && remaining >= sizeof(*command); ++index) {
        command = (const struct load_command *) cursor;
        if (command->cmdsize < sizeof(*command) || command->cmdsize > remaining) break;
        if (command->cmd == LC_UUID && command->cmdsize >= sizeof(*uuid)) {
            uuid = (const struct uuid_command *) command;
            maage_crash_text(&maage_crash_header, " image_uuid=");
            for (byte = 0; byte < 16; ++byte) {
                text[0] = digits[uuid->uuid[byte] >> 4];
                text[1] = digits[uuid->uuid[byte] & 15U];
                text[2] = '\0';
                maage_crash_text(&maage_crash_header, text);
            }
            break;
        }
        cursor += command->cmdsize;
        remaining -= command->cmdsize;
    }
    return (maage_crash_word) header;
}
#elif defined(__linux__)
static int maage_crash_main_image(struct dl_phdr_info *image, size_t bytes, void *context) {
    (void) bytes;
    if (image->dlpi_name && *image->dlpi_name) return 0;
    *(maage_crash_word *) context = (maage_crash_word) image->dlpi_addr;
    return 1;
}
static maage_crash_word maage_crash_image_base(void) {
    maage_crash_word base = 0;
    dl_iterate_phdr(maage_crash_main_image, &base);
    return base;
}
#else
static maage_crash_word maage_crash_image_base(void) {
    return 0;
}
#endif
#endif

void maage_log_crash_stop(void) {
#ifdef _WIN32
    LPTOP_LEVEL_EXCEPTION_FILTER current;
    if (maage_crash_filter_installed) {
        current = SetUnhandledExceptionFilter(NULL);
        if (current != maage_crash_exception) SetUnhandledExceptionFilter(current);
        maage_crash_filter_installed = 0;
    }
    if (maage_crash_output) CloseHandle(maage_crash_output);
    maage_crash_output = NULL;
#else
    unsigned int index;
    struct sigaction current;
    stack_t current_stack;
    for (index = 0; index < sizeof(maage_crash_signals) / sizeof(maage_crash_signals[0]); ++index) {
        if (maage_crash_installed[index] && !sigaction(maage_crash_signals[index], NULL, &current) && (current.sa_flags & SA_SIGINFO) && current.sa_sigaction == maage_crash_signal) sigaction(maage_crash_signals[index], &maage_crash_previous_signals[index], NULL);
        maage_crash_installed[index] = 0;
    }
    if (maage_crash_output >= 0) close((int) maage_crash_output);
    maage_crash_output = -1;
    if (maage_crash_stack_installed && pthread_equal(pthread_self(), maage_crash_stack_owner) && !sigaltstack(NULL, &current_stack) && current_stack.ss_sp == maage_crash_stack.bytes && !sigaltstack(&maage_crash_previous_stack, NULL)) maage_crash_stack_installed = 0;
#endif
}
maage_status maage_log_crash_start(FILE *output) {
    maage_crash_word image_base;
    maage_status result = maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_INITIALIZE, 0);
#ifdef _WIN32
    LPTOP_LEVEL_EXCEPTION_FILTER previous_filter;
    HANDLE source = output ? (HANDLE) _get_osfhandle(_fileno(output)) : GetStdHandle(STD_ERROR_HANDLE);
    if (!source || source == INVALID_HANDLE_VALUE) return maage_status_from_win32(ERROR_INVALID_HANDLE, MAAGE_CONTEXT_CORE, MAAGE_OPERATION_INITIALIZE);
    if (!DuplicateHandle(GetCurrentProcess(), source, GetCurrentProcess(), &maage_crash_output, output ? FILE_APPEND_DATA : 0, FALSE, output ? 0 : DUPLICATE_SAME_ACCESS)) return maage_status_from_win32(GetLastError(), MAAGE_CONTEXT_CORE, MAAGE_OPERATION_INITIALIZE);
    maage_crash_entered = 0;
#else
    unsigned int index;
    struct sigaction action;
    stack_t stack;
    int descriptor = dup(output ? fileno(output) : STDERR_FILENO);
    if (descriptor < 0) return maage_status_from_errno(errno, MAAGE_CONTEXT_CORE, MAAGE_OPERATION_INITIALIZE);
    if (fcntl(descriptor, F_SETFD, FD_CLOEXEC) < 0) {
        int error = errno;
        close(descriptor);
        return maage_status_from_errno(error, MAAGE_CONTEXT_CORE, MAAGE_OPERATION_INITIALIZE);
    }
    maage_crash_output = descriptor;
    maage_crash_entered = 0;
#endif
    maage_crash_header.length = 0;
    maage_crash_text(&maage_crash_header, "\n[FATAL]   [maage.core.crash] Native crash status=");
    maage_crash_hex(&maage_crash_header, maage_status_make(MAAGE_STATUS_FATAL, MAAGE_CONTEXT_CORE, MAAGE_OPERATION_STOP, 0));
    maage_crash_text(&maage_crash_header, " platform=" MAAGE_TARGET_SYSTEM " arch=" MAAGE_CRASH_ARCH);
    image_base = maage_crash_image_base();
    maage_crash_text(&maage_crash_header, " image_base=");
    maage_crash_hex(&maage_crash_header, image_base);
    maage_crash_text(&maage_crash_header, " | ");
#ifdef _WIN32
    previous_filter = SetUnhandledExceptionFilter(maage_crash_exception);
    /* Rewrapping a third-party filter that already chains us would create a
     * callback cycle on reinit. Own only the default filter, as for POSIX. */
    if (previous_filter && previous_filter != maage_crash_exception) SetUnhandledExceptionFilter(previous_filter);
    else maage_crash_filter_installed = 1;
#else
    if (!maage_crash_stack_installed && !sigaltstack(NULL, &stack) && (stack.ss_flags & SS_DISABLE) && sizeof(maage_crash_stack.bytes) >= (size_t) MINSIGSTKSZ) {
        maage_crash_previous_stack = stack;
        stack.ss_sp = maage_crash_stack.bytes;
        stack.ss_size = sizeof(maage_crash_stack.bytes);
        stack.ss_flags = 0;
        if (!sigaltstack(&stack, NULL)) {
            maage_crash_stack_owner = pthread_self();
            maage_crash_stack_installed = 1;
        }
    }
    memset(&action, 0, sizeof(action));
    action.sa_sigaction = maage_crash_signal;
    action.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_RESETHAND;
    sigemptyset(&action.sa_mask);
    for (index = 0; index < sizeof(maage_crash_signals) / sizeof(maage_crash_signals[0]); ++index) {
        if (sigaction(maage_crash_signals[index], NULL, &maage_crash_previous_signals[index]) < 0) {
            result = maage_status_from_errno(errno, MAAGE_CONTEXT_CORE, MAAGE_OPERATION_INITIALIZE);
            break;
        }
        /* Do not turn another component's handled faults into fatal reports. */
        if (maage_crash_previous_signals[index].sa_handler != SIG_DFL) continue;
        if (sigaction(maage_crash_signals[index], &action, NULL) < 0) {
            result = maage_status_from_errno(errno, MAAGE_CONTEXT_CORE, MAAGE_OPERATION_INITIALIZE);
            break;
        }
        maage_crash_installed[index] = 1;
    }
#endif
    if (maage_status_failed(result)) maage_log_crash_stop();
    return result;
}
