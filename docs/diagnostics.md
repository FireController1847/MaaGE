# Diagnostics

Core Log flushes live records. Client: instance `logs/client.log`, including captured stdout/stderr. Launcher: application-root `logs/launcher.log`. Dedicated Server defaults to console. Close retains the plain log and timestamped GZip archive; startup preserves unarchived output.

DEBUG `maage.launcher.startup` reports monotonic durations for Core/log init, minimal Launcher init, native show/map, networking, account restoration/verification, catalog, resources and first Play readiness. Native show/map is not compositor/display latency. See [AGENTS.md](../AGENTS.md) for levels, filtering, traces and archive rules.

## Native Fault Records

`maage_log_init` installs supported crash hooks and a separate append handle. Emergency writing bypasses normal locks, streams, formatting, heap and filters. Bounded FATAL/Core/Stop records contain OS/architecture, image base, process, signal/exception, fault address and available PC/SP/FP/ARM64 LR; macOS may include image UUID, Windows thread ID. Symbolicate with matching binary/symbols, OS report and ASLR adjustment.

No saving, stack walking, cleanup or recovery in fault handlers. Capture is best effort: SIGKILL, forced termination, CRT fast-fail, replaced hooks or destroyed stacks can prevent it. Ordinary Status failures use causal traces and controlled shutdown. Platform references: [POSIX signals](https://developer.apple.com/library/archive/documentation/System/Conceptual/ManPages_iPhoneOS/man2/sigaction.2.html), [Windows exception filters](https://learn.microsoft.com/en-us/windows/win32/api/errhandlingapi/nf-errhandlingapi-setunhandledexceptionfilter).
