# Diagnostics

Core Log writes live records synchronously and flushes each record. Client logs
are instance-local `logs/client.log`, including native stdout/stderr. Launcher
uses its data-root `logs/launcher.log`; the dedicated Server uses stderr/stdout
unless file logging is selected. Normal close creates a dated GZip copy and
retains the latest plain file. Startup preserves unarchived output before reset.

## Native Crashes

`maage_log_init` prepares a separate, non-inherited append handle for the current
log and installs native fault hooks. Without a file, the handle targets stderr.
The emergency writer is independent of the regular logger's mutex, FILE stream,
formatter and level filters. It emits one bounded numeric record, then lets the
original fatal condition reach the operating system. No World save, teardown,
heap allocation, stack walking or recovery is attempted in a fault handler.

Records identify the universal FATAL/Core/Stop status, platform, architecture,
main executable image base, process, native signal/exception code and available
fault address/registers. macOS includes the loaded executable UUID; Windows
exceptions include the thread ID. POSIX time is Unix seconds; Windows uses UTC.
PC, SP, FP and ARM64 LR come from the fault context rather than an invented stack.
Use the matching executable/debug symbols and the system crash report for full
stack symbolication. Addresses and image bases account for ASLR during that work.
No credentials, command lines, environment variables or arbitrary memory dumps
are collected.

POSIX hooks cover SIGABRT, SIGSEGV, SIGBUS, SIGILL and SIGFPE. They preserve
already-owned signal dispositions. The initializing thread gets an alternate
signal stack when possible; ordinary faults on other threads still use the same
writer. Windows uses an unhandled exception filter only when that hook is unowned,
retaining earlier exception reporting and CRT abort behavior. Closing/reinitializing Log releases
its duplicate handle and restores hooks it still owns. Configure logging at
application lifecycle boundaries, outside concurrent reconfiguration.

Recording is best effort: SIGKILL, OS memory termination, Windows CRT abort/fast-fail, inaccessible
output, superseding third-party hooks and severely damaged/exhausted stacks may
prevent it. Native hooks use only bounded formatting and native writes; a broken
stderr pipe cannot replace the original fatal signal with SIGPIPE. Normal Status
failures continue through the ordinary causal-trace logger and clean shutdown.

The writer follows the native contracts for [POSIX signal delivery and safe
operations](https://developer.apple.com/library/archive/documentation/System/Conceptual/ManPages_iPhoneOS/man2/sigaction.2.html),
[Windows unhandled exceptions](https://learn.microsoft.com/en-us/windows/win32/api/errhandlingapi/nf-errhandlingapi-setunhandledexceptionfilter)
and [append writes](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-writefile).
