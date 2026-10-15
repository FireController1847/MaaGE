# Development conventions

- Preserve the existing Git author and committer identity. Every commit created
  or amended on the user's behalf must include this trailer:
  `Co-authored-by: Codex <noreply@openai.com>`.
- Default to ISO C90 without compiler extensions. Set portable target standards
  explicitly; keep necessary language or compiler exceptions local to adapters.
- Put implementations under `src/` and public interfaces under `include/`.
- Keep the root `CMakeLists.txt` as the workspace orchestrator. Configure each
  application in its own directory; use the `nv_` target prefix.
- Keep launcher, client, and server independently buildable. The launcher must
  not use client/server internals. The server must remain headless and must never
  depend on the client, windowing, or a graphics backend.
- Shared core infrastructure must not depend on application implementations or
  include platform/graphics SDKs in portable headers.
- Isolate platform sources with target-local CMake conditions. Future windowing
  uses Win32, X11/Wayland, or Cocoa/AppKit; graphics uses Vulkan on Windows/Linux,
  native Metal on macOS, and null/headless operation for servers and tests.
  Keep renderer interfaces independent of graphics implementations. Enable
  Objective-C only when a macOS implementation target needs it.
- Keep authored top-level build configuration at the root. Reserve `cmake/` for
  generated build trees and headers; keep compiled artifacts under `build/`.
  Use a separate `cmake/<name>/` tree for each toolchain/preset and corresponding
  `build/<name>/` output. Never generate into source directories.
- Respect cross-compilation: use target/toolchain information and compiler
  checks rather than build-host assumptions. Report only platforms actually tested.
- Do not introduce abstractions, dependencies, speculative directories, or
  engine systems before they are needed. The current scope is build scaffolding.
- Keep README.md human-focused, and preserve the AI-development disclosure.
