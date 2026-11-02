# Diagnostics

Core Log flushes live records. Client logs are instance-local `logs/client.log`, including captured stdout/stderr; Launcher logs use application-root `logs/launcher.log`. Dedicated Server output defaults to stdout/stderr unless file logging is selected. Normal close retains the plain log and produces a timestamped GZip archive; startup preserves otherwise unarchived output.

## Native Fault Records

`maage_log_init` installs supported native crash hooks and a separate append handle. The emergency writer bypasses the usual log mutex, streams, formatters, heap allocation and filters. It writes a bounded FATAL/Core/Stop record with OS/architecture, executable image base, process, signal/exception, fault address and available PC/SP/FP/ARM64 LR context. macOS can record image UUID and Windows a thread ID. Consult the matching binary/symbols and OS crash report for full symbolication, accounting for ASLR.

A fault handler does not attempt World saves, stack walking, cleanup or recovery. Capture is best effort: SIGKILL, forced OS termination, CRT fast-fail, supplanted hooks and destroyed stacks may prevent it. Ordinary Status failures use normal causal traces and controlled shutdown. Platform reference: [POSIX signals](https://developer.apple.com/library/archive/documentation/System/Conceptual/ManPages_iPhoneOS/man2/sigaction.2.html) and [Windows exception filters](https://learn.microsoft.com/en-us/windows/win32/api/errhandlingapi/nf-errhandlingapi-setunhandledexceptionfilter).
