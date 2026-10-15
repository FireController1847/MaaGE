> **AI AGENTS — PRIMARY PROJECT CONTEXT**
>
> This file is the authoritative source of persistent project instructions,
> architectural expectations, and development context. Read it before beginning
> work and maintain it as requirements evolve.
>
> **TOKEN EFFICIENCY:** Keep this file as compact as reasonably possible.
> Preserve essential information, eliminate repetition, and prioritize concise,
> actionable instructions. There is no arbitrary length limit; correctness and
> completeness take precedence over brevity.
>
> Do not create alternative AI memory or instruction files.

# MaaGE project contract

MaaGE (Minecraft as a Game Engine) is an independent, modular, cross-platform,
Minecraft-inspired native voxel game platform for different modpacks and gameplay
experiences. The engine is primarily C. Architectural goals below describe the
intended design; inspect the repository before claiming functionality exists.

## Non-negotiable engineering principles

### C90-first portability

- Default to ISO C90 without compiler extensions for portable engine code and
  public C interfaces. Set standard requirements explicitly on portable targets;
  never silently introduce newer syntax, comments, or language features.
- Isolate necessary compiler, OS, and graphics extensions behind platform-specific
  boundaries. Keep language exceptions target-local; enable Objective-C only when
  a macOS implementation target needs it. Avoid unnecessary C++.
- Preserve alternative-toolchain experimentation, including Open Watcom where
  feasible; do not impose an unnecessary modern compiler baseline. Do not assume
  pointer size or integer widths.
- Respect cross-compilation: use target/toolchain information and compiler checks,
  not build-host assumptions. Report only validation actually performed.

### Mandatory modularity

- Respect subsystem boundaries. Prefer reusable APIs, small independent modules,
  and shared utilities when behavior crosses boundaries or is reasonably reusable.
  Inspect existing interfaces before adding new ones; implement shared logic once.
- Keep implementation details private and public contracts minimal. Use opaque
  handles, explicit ownership, and backend-independent interfaces where appropriate.
- Keep launcher, client, and dedicated server independently buildable and
  executable. The launcher must not use client/server implementation internals.
  Client and server may share contracts and game definitions, not application
  implementations. Shared infrastructure must not depend on applications.
- The server must remain headless and independent of client, windowing, and
  graphics implementations. Never couple simulation to rendering, windowing, or
  OS implementations; access necessary services through appropriate boundaries.
- Extend established module boundaries instead of adding speculative abstraction
  layers. Do not create artificial APIs for trivial, localized behavior.

### Mandatory token-efficient development

- Make focused, minimal changes that solve the actual requirement. Avoid duplicated
  logic and unnecessary scaffolding, boilerplate, comments, directories, or dependencies.
- Prefer concise implementations and targeted verification over unrelated
  refactoring. Keep responses concise: report meaningful results, failures, and
  remaining limitations without repeatedly explaining established information.
- Treat token efficiency alongside portability and modularity, but never sacrifice
  correctness, readability, security, or maintainability to reduce tokens.
- Follow the current task's scope. Do not implement engine subsystems or planned
  integrations merely because they appear in the architectural goals.

## Platform and intellectual-property boundaries

- Native windowing: Win32 on Windows, X11/Wayland on Linux, Cocoa/AppKit on macOS.
- Initial rendering: Vulkan on Windows/Linux, native Metal on macOS without
  requiring MoltenVK, and null/headless operation for servers and testing.
  Rendering backends must be replaceable behind stable, implementation-independent
  interfaces. Keep platform dependencies isolated and SDKs out of portable headers.
- Planned launcher responsibilities include asset management, Microsoft account
  authentication, and Minecraft ownership verification. Do not distribute Mojang's
  proprietary assets or conflate Minecraft compatibility with original project code.
- Apache-2.0 covers original project code and documentation, not third-party
  materials or Minecraft/Mojang/Microsoft intellectual property. Preserve applicable
  licenses and disclosures; MaaGE is not an official or endorsed Minecraft project.

## Workspace and build conventions

- Put implementations under `src/` and public interfaces under `include/`.
  Configure each application in its own directory; use the `nv_` target prefix.
- Keep authored top-level build configuration and active presets at the root;
  root `CMakeLists.txt` orchestrates the workspace. Reserve `cmake/<name>/` for
  generated configuration, headers, and intermediates; corresponding compiled
  output belongs in `build/<name>/`. Isolate presets/toolchains and multi-config
  outputs. Never generate into source directories or commit generated artifacts.
- Keep build validation in `.github/workflows/build.yml` manually triggered only
  (`workflow_dispatch`) until explicitly authorized otherwise. Preserve read-only
  permissions and existing CMake layout.
- Preserve the existing Git author and committer identity. Every commit created
  or amended on the user's behalf must include:
  `Co-authored-by: Codex <noreply@openai.com>`.

## Documentation and persistent context

- `AGENTS.md` is the exclusive home for persistent AI-facing instructions,
  architectural decisions, user expectations, lessons, recurring mistakes, and
  project context. Do not create separate AI instructions, memory files,
  scratchpads, agent notes, duplicate context documents, or a secondary hierarchy.
- Keep `README.md` human-facing: overview, goals, status, build guidance, and
  disclosures. Preserve its opening AI-routing HTML comment, human–AI development
  disclosure, and legally relevant notices. Do not add visible agent instructions.
- Source holds implementation and necessary local comments; useful API references
  describe public contracts and usage. Human technical specifications may exist.
  Build files configure builds. None is a substitute home for persistent AI context.

## Maintaining this contract

- Consult this file before changes. When a conversation or task establishes a
  durable requirement, preference, decision, convention, or lesson that materially
  affects future work, update the relevant section here rather than relying on
  conversation history or saving it elsewhere.
- Newer explicit user requirements are authoritative. Reconcile conflicts,
  consolidate duplicates, and replace superseded guidance instead of appending
  historical notes indefinitely. Preserve useful compatible instructions.
- Make targeted edits as requirements evolve; do not rewrite the document after
  every task. Keep headings easy to scan and requirements concise and actionable.
- Exclude transient task details, trivial interactions, speculative ideas, and
  facts already evident in source. This is a living architectural contract, not
  a transcript, changelog, or scratchpad.
