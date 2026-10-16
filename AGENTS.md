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

MaaGE (Minecraft as a Game Engine): **A modular, cross-platform reimagining of
Minecraft in C, built for performance, extensibility, and limitless modding.**

This independently developed native voxel platform takes **Minecraft: Java Edition
1.7.10** as its canonical gameplay and compatibility reference, not later releases.
Recreate its familiar mechanics and modpack experience while retaining freedom to
redesign, optimize, extend, or replace systems. Version 1.7.10 is the starting
point, not an architectural limitation. Support many independently configurable
modpacks rather than one predefined game. Do not reproduce the Java engine,
preserve Forge compatibility, or execute existing Java mods; develop independent
native or data-driven systems and content instead.

The repository has a CMake workspace, a minimal shared core, and a reusable native
windowing and rendering subsystems consumed by the Launcher. The architecture and roadmap describe
future responsibilities,
not implemented capabilities or authorization to build them. Inspect current code
before claiming functionality or successful platform validation.

## Non-negotiable engineering principles

### C90-first portability

- Default to ISO C90 without compiler extensions for portable engine code and
  public C interfaces. Set standard requirements explicitly on portable targets;
  never silently introduce newer syntax, comments, or language features.
- Isolate necessary compiler, OS, and graphics extensions behind platform-specific
  boundaries. Keep language exceptions target-local; enable Objective-C only when
  a macOS implementation target needs it. Avoid unnecessary C99, C11, or C++;
  do not sacrifice essential modern functionality to universal C90 compatibility.
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
  handles, explicit ownership, versioned interfaces, and backend-independent
  contracts where appropriate. Avoid circular dependencies.
- Keep Launcher, Client, and Server independently buildable and versioned.
  The Client is a separate executable with Launcher-managed startup and lifetime
  independent of the Launcher; it is not an in-process Launcher library. The launcher must not use client/server implementation internals.
  Client and server may share contracts and game definitions, not application
  implementations. Shared infrastructure must not depend on applications.
- Server simulation/networking and its headless executable must remain independent
  of client, windowing, and graphics implementations. An optional native GUI is a
  frontend to the same server library, never a second server implementation. Never
  couple simulation to rendering, windowing, or OS implementations; access necessary
  services through appropriate boundaries.
- Extend established module boundaries instead of adding speculative abstraction
  layers. Do not create artificial APIs for trivial, localized behavior.

### Mandatory token-efficient development

- Make focused, minimal changes that solve the actual requirement. Avoid duplicated
  logic and unnecessary scaffolding, boilerplate, comments, directories, or dependencies.
- Prefer concise implementations and necessary compilation over unrelated
  refactoring. Develop incrementally rather than in enormous implementation passes.
  Keep responses concise: report meaningful results, failures, and remaining
  limitations without repeatedly explaining established information.
- Treat token efficiency alongside portability and modularity, but never sacrifice
  correctness, readability, security, or maintainability to reduce tokens.
- Follow the current task's scope. Do not implement engine subsystems or planned
  integrations merely because they appear in the architectural goals.

### Build, but do not test

- Configure, compile, and link affected targets; fix introduced configuration,
  compiler, and linker failures without disabling targets or hiding relevant errors.
- Unless the programmer explicitly requests otherwise, never execute applications,
  unit/integration/smoke/UI tests, benchmarks, CTest, or runtime validation. Do not
  add automated testing steps or run separate validation scripts/passes beyond
  what successful compilation requires. Runtime testing belongs to the programmer.
- Report builds, errors, and uncompiled platforms concisely. Compilation does not
  establish correct runtime behavior. This policy applies to subsequent tasks.
- Prefer focused builds of directly affected targets, shared components, and
  relevant primary backends. Build fallback backends only when modified or
  affected by shared API/build changes; do not repeatedly rebuild every platform
  or renderer after unrelated edits. Do not fix unrelated legacy problems during
  primary development or trigger GitHub Actions without explicit authorization.
  Keep exploration focused and report unsupported/uncompiled backends honestly.
  Runtime testing and visual validation belong to the programmer.

## Application responsibilities

These are long-term responsibilities; consult source for implemented portions.
Shared functionality belongs in reusable modules, not duplicate implementations.

- **Launcher (`src/launcher/`):** engine installation/version management; modpack
  installation/configuration; authorized Minecraft asset acquisition; Microsoft
  authentication and legitimate ownership verification through authorized mechanisms;
  profiles, client launch configuration, updates, and dependency management.
  Remain independent of client/server internals. Existing-launcher Minecraft session
  verification gates native first-run asset setup. New Microsoft sign-in, modpack
  installation, and game launching remain deferred.
- **Client (`src/client/`):** native windowing, rendering, audio, input, UI, world
  presentation, simulation interaction, and multiplayer connectivity. Consume shared
  game definitions/protocols without depending on the server executable or private
  server implementation. Integrated singleplayer may use an in-process or separate
  server; that decision remains open. The Launcher distributes/selects independently
  versioned Client installations and starts them detached, so closing the Launcher
  never terminates the game. Client startup consumes an explicit versioned launch
  contract when implemented; no standalone desktop entry or app bundle. Do not
  invent launch flags as authentication or implement distribution/launching yet.
- **Server (`src/server/`):** authoritative simulation of worlds, chunks, blocks,
  entities, ticks, scheduled updates, physics, interactions, generation, inventories,
  gameplay, modpack definitions/registries, persistence, and multiplayer synchronization.
  One CLI executable owns both headless operation and an optional future native
  administration GUI frontend over the same server library. No separate Server GUI
  executable, macOS app bundle, or desktop entry. Headless builds/runs require no
  display server, graphics device, Vulkan, Metal, X11, Wayland, Cocoa, or GUI
  dependencies. Future GUI dependencies must load only when requested, never at
  headless startup; simulation/networking remain independent. Do not implement the
  administration interface until explicitly requested.
- **Core (`src/core/`):** reusable platform-independent foundations and shared
  contracts, without application implementation dependencies.

## Layering and dependencies

Use CatalystUI and `lib-core-c` as inspiration for these conceptual layers:
portable foundations → platform abstractions → replaceable backend implementations
→ engine services → gameplay/content → applications. This expresses separation of
responsibilities, not permission for generic services to depend on concrete backends.

Cross-layer interactions use explicit contracts: Window API → native windowing;
Render API → capability-driven native/software backends; Input API → native event
adapters; Storage API → filesystem implementations; Engine API → world/simulation services. Preserve
interchangeable implementations without building a universal abstraction framework.

Evaluate `lib-core-c` types, memory utilities, and other foundations before reuse;
do not make it a mandatory external dependency without explicit user approval.
Keep platform dependencies isolated and SDK types out of portable public headers.

## Native platform and windowing

| Target | Native windowing | Initial renderer |
| --- | --- | --- |
| Windows | Win32 | Vulkan |
| Linux | X11 and Wayland | Vulkan |
| macOS | Cocoa/AppKit | Native Metal |
| Headless | Null | Null |

Native windowing is mandatory and independent of rendering. Its eventual contract
covers window creation/destruction, event processing, resize/display changes,
fullscreen/windowed modes, logical/framebuffer dimensions, DPI/scaling,
focus/visibility, cursors, and native surface interoperability. Input is a separate
subsystem consuming appropriate native events. Permit Objective-C AppKit bridges
behind C90-compatible public interfaces. Do not require SDL or GLFW as foundations.

Windowing is a reusable Core library, independent of applications and renderers;
use native implementations without general-purpose third-party windowing frameworks.
Its C90 public API uses opaque owned windows, explicit results, distinct windowed,
borderless-windowed, borderless-fullscreen, and native-fullscreen modes, and typed
native-handle interoperability. Report unsupported operations explicitly. Linux
backends are independently selectable; automatic selection prefers an available
Wayland session with X11 fallback. Keep OS/protocol language exceptions backend-local.
Wayland generated inline helpers require C99 only in its adapter target; the public
API and common implementation remain C90. Linux builds require the selected native
development libraries and protocol tools. Disable the optional windowing target for
headless-only workspaces; never link it into Server core or the headless executable.
A future optional GUI frontend inside the Server process may consume it. Native fullscreen
is currently AppKit-only; other adapters offer borderless fullscreen. Wayland
client-requested resize and unnegotiable borderless decorations return unsupported.
macOS calls belong on the main thread. Full-display content is configurable:
borderless fullscreen uses the screen frame, and native fullscreen requests maximum
permitted content via public AppKit APIs. Expose safe-area insets for UI; never
unnecessarily crop drawable content to the safe area. The physical camera cutout
cannot display pixels, and macOS retains control over Spaces/menu-bar/Dock policy.

## First-class rendering backends

Rendering is a reusable Core subsystem consumed through a C90, opaque,
backend-independent API. Direct Vulkan on Windows/Linux and genuine native Apple
Metal on macOS are the primary backends; Metal is the current development focus.
Keep rendering separate from window lifecycle, applications, and gameplay;
renderers borrow windows, which must outlive
them. Native-content ownership is acquired exclusively through window interop;
Wayland's empty backing must not overwrite external rendering. CPU presentation
uses the shared RGBA8 pixel-buffer contract and native Window adapters, without a
graphics SDK dependency. Preserve consistent renderer lifecycle and the Launcher
RGB demonstration across backends.
Headless targets must not link rendering or acquire graphics dependencies; disable
`MAAGE_BUILD_RENDER` for headless-only workspaces. Missing Vulkan development files
are configuration errors when that backend is enabled. Isolate graphics SDK/C99
requirements in backend targets; portable headers and common implementation stay C90.

| Project priority | Renderer | Role |
| --- | --- | --- |
| Primary | Native Metal | Main macOS development focus. |
| Primary | Vulkan | Main Windows/Linux renderer. |
| Secondary | Modern OpenGL | Compatibility fallback. |
| Secondary | Software | CPU fallback and portability. |
| Legacy | OpenGL 1.1 | Older hardware/OS compatibility. |

Metal and Vulkan are the only actively prioritized GPU backends. Do not
significantly expand, optimize, refactor, or chase feature parity in lower-priority backends unless explicitly requested, needed
for a small shared-interface adjustment, or required to fix compilation broken by
the current task. Metal/Vulkan guide modern capabilities; optional capabilities
let older backends support relevant subsets without unnecessary constraints or
emulation. **Modularity is mandatory; feature parity is not.** Keep backend logic
isolated and avoid speculative interfaces for hypothetical legacy requirements.

- **Vulkan:** isolate API types, device management, swapchains, pipelines, command
  recording, and synchronization inside the backend, away from generic engine and
  gameplay interfaces.
- **Metal:** prepare the initial frame before revealing the Launcher window; match
  native backing to the initial clear color and synchronize the first drawable
  with Core Animation. Normal presentation continues to skip hidden windows.
  Use Apple's Metal API, with Cocoa/AppKit and `CAMetalLayer` where
  appropriate. MoltenVK must not be the required implementation. Isolate necessary
  Objective-C/Objective-C++ translation units in macOS implementation directories;
  exclude Metal objects/types from portable C interfaces. Provide a suitable shader
  asset pipeline for Metal Shading Language when implementing this backend.
- **OpenGL:** Modern (3.3+ Core; macOS requests 4.1) and Legacy (1.1 fixed-function
  baseline, no shader/VBO/VAO requirement) share `render/opengl/`. Context creation
  stays native: Cocoa, WGL, GLX/X11 (requires a compatible window visual).
  Wayland OpenGL contexts are not implemented;
  report unsupported. Legacy WGL does not require modern context extensions.
- **Software:** retain the C90 CPU framebuffer as the universal fallback, with
  native AppKit/GDI/X11/Wayland presentation. Do not add GPU API dependencies or
  prematurely implement a full rasterizer.
- **Selection/capabilities:** CLI families are `auto`, `metal`, `vulkan`,
  `opengl`, and `software` only. There is no renderer-version option.
  OpenGL selects modern or 1.1 legacy internally, not as a user-facing option.
  Explicit renderer selection never falls back to a different family.
  AUTO tries Metal → modern GL → legacy GL → Software on macOS, and Vulkan →
  modern GL → legacy GL → Software on Windows/Linux. Advertise only actual
  compiled capabilities; log selection/failures and clean up failed attempts.
  Load optional Vulkan runtime DLLs dynamically on Windows to permit fallback.
- Version capability contracts; query native limits/formats conservatively and
  report zero for unknown limits. Device capabilities do not imply unimplemented
  public resource/draw commands exist. Keep interfaces backend-neutral and useful
  to legacy fixed-function and CPU paths; unsupported operations return meaningful
  errors, never successful no-ops or forced emulation of modern GPU features.
  Only implemented, compiled backends may be called build-supported. Runtime
  functionality requires programmer testing; compilation alone is not evidence.
- **Rendering contracts:** expose backend-independent buffers, textures, shader and
  pipeline resources, resource binding, command submission, synchronization, render
  targets/presentation, capabilities, and optional features as needed. Backend
  replacement must be demonstrable, not merely theoretical.
- Keep high-level voxel rendering separate from the low-level graphics device API.
  Chunk meshing, visibility, world simulation, and content must not depend directly
  on graphics APIs. Headless operation must not acquire graphics dependencies;
  changing the rendering backend must not require rewriting simulation.

## Launcher UI and asset setup

Prefer native operating-system infrastructure for the lightweight Launcher:
AppKit on macOS, Win32 controls on Windows, and GTK 3 on Linux. Launcher-owned
adapters remain separate from Game UI; do not create a shared widget framework.
Ordinary Launcher UI must not initialize a renderer. Core graphics remain for
actual rendering needs; the existing RGB demonstration follows successful setup.
New Microsoft sign-in and game launching remain deferred.

First-run setup acquires Minecraft Java Edition 1.7.10 indexed assets and game
resources from its official client JAR, after verifying an existing Minecraft
account. Read the client URL, SHA-1, and size from official version metadata; cache
the verified source archive at `minecraft/cache/client-1.7.10.jar`. Never execute
it, extract Java bytecode/implementation code, or distribute acquired resources.
Minecraft 1.7.10 remains the fixed resource baseline, independent of MaaGE versions;
no Java runtime, server JAR, modpack import, or version-management system.

Verify Minecraft Access precedes Preparing MaaGE. Discovery, explicit connection,
and authenticated verification are distinct states. Prioritize existing compatible
PrismLauncher/MultiMC V3 MSA sessions; MaaGE needs no Microsoft OAuth Client ID and
must not perform a new Microsoft sign-in. After explicit Import Account consent,
selectively read only the selected Minecraft Services `ygg.token`; never import
Microsoft tokens, refresh tokens, passwords, full authentication databases, or
other applications' protected/vault data. Source launcher files remain read-only.
Check authenticated `minecraft/profile` UUID/username and `entitlements/mcstore`
Java access over verified HTTPS; failures/app-bound tokens never grant access.
Never log credentials or raw responses. Wipe transient token and JSON copies.

Import creates a MaaGE-owned account with stable UUID/name and a versioned
`minecraft-session-import` credential provider, allowing future Microsoft OAuth
credentials to replace it without recreating identity. Store AES-256-GCM account
records at `config/accounts/<uuid>.account` with a random local `key.bin`, bound to
the UUID and restricted to the current user (0600/protected DACL). Atomic writes
and a store lock protect updates. No system credential stores/DPAPI; local key and
ciphertext are both accessible to the local user. Keep public metadata in
`config/minecraft-profiles.json` separate from credentials. Disconnect removes the
saved credential; source launchers/resources remain unchanged.

The UI-independent access service restores authenticated local leases across
restarts for at most one hour, capped by imported token expiry. Public registry
flags cannot grant authority. Recheck saved tokens automatically when leases need
renewal; valid local accounts skip the import screen without source-launcher I/O
for credentials. Verify Saved reads only MaaGE's account; Re-import requires fresh
consent. `--manage-accounts` keeps account controls accessible despite automatic
startup continuation. Unknown token expiry is capped at one hour. Expired tokens require
re-import until a separately requested Microsoft sign-in/refresh provider exists;
do not impersonate another launcher or use its OAuth client/refresh tokens.
Discovery/connection/import/verification remain distinct, with UUID deduplication,
multiple local accounts, rescan/manual folders, and responsive native controls.
Official/embedded formats remain discovery-only; opaque FTB/CurseForge stores are
unsupported. Directory detection, installation profiles, and cached source flags
never prove access. The controller and asset manager require one current account
lease, including repair/retry/active work; expiry, disconnect, and cancellation
revoke active access without deleting resources. Local credential storage and
background verification stay out of game clients and generic Core APIs; Core
provides only generic filesystem permissions/atomic I/O and other foundations.
Expose **Verify Through Minecraft — Coming Soon** as disabled; future multiplayer
verification belongs in a separate provider, with no protocol/server implementation
or new Microsoft authentication path until requested.

The Launcher asset manager is UI-independent, with worker-thread operations,
locked progress snapshots, cancellation, Retry, and full repair (`--repair-assets`).
Core provides generic UTF-8 storage/path and HTTPS transfer APIs, without Minecraft
knowledge. Dependencies are libcurl >=7.85, json-c >=0.15, OpenSSL Crypto >=1.1.0, and libzip >=1.7;
GTK >=3.22 stays in the Linux UI adapter. Keep these dependencies out of
Server/headless targets.

Use `%APPDATA%/MaaGE`, `~/Library/Application Support/MaaGE`, or
`$XDG_DATA_HOME/maage` (fallback `~/.local/share/maage`) for assets, metadata,
configuration, and Launcher logs. Minecraft-specific data belongs beneath
`minecraft/`: `assets/indexes/1.7.10.json`, `assets/objects/<prefix>/<hash>`,
`assets/reconst/<logical-name>`, and `metadata/1.7.10.json`. Rename the previous
`assets/reconstructed/` directory when `reconst/` is absent; if both exist, preserve
the old directory and prepare the active `reconst/` from verified caches. Receipts and the
manifest also live in `minecraft/metadata/`; MaaGE configuration/logs remain outside.
Reuse/migrate verified files from the previous `assets/` and `versions/1.7.10/`
layout without redownloading them. Never create new Minecraft `versions/` trees.
Previous `.maage` logs remain untouched.
Downloads are content-addressed, SHA-1/size-verified, bounded to four concurrent
requests with three attempts and backoff, and atomically promoted from temporary
files. Resume preserves completed objects; interrupted individual files restart.
An installation lock serializes writers. Initial setup and repair share one
verification/download/reconstruction implementation: cover every indexed entry,
deduplicate hashes, and reconstruct every logical path, without resource-type
filters. Completion requires both the verified object cache and valid logical
files. Track unique objects, logical files, cached/downloaded objects,
reconstruction progress, and outstanding missing/corrupt files separately.
Log actual file operations through Core logging: indexed names/hashes, download
URLs/destinations/attempts/retries, verified installation, migration, reconstruction
source/destination and hard-link/copy results, cancellation, failures, and summaries.
Use INFO for performed work and DEBUG for per-file verification and unchanged cache
reuse. Derive resource names and mappings from the downloaded index; keep static
resources minimal rather than embedding asset lists or duplicate resource tables.

Reconstruction prefers hard links from verified objects, with verified copies as
fallback, staged outside the logical tree and atomically promoted. Logical names
must remain relative, preserve spelling, and pass cross-platform path validation;
reject traversal, symlink/reparse directories, and unsafe/unrepresentable names
rather than silently omitting assets. Derived files can be rebuilt from the cache;
repairing an object invalidates associated reconstruction shortcuts. Completion
receipts optimize startup but cannot replace checking metadata and all object/file
sizes/fingerprints; changed files and incomplete installations require hash
verification. Full Repair hashes every unique object and independently copied
logical file; identical hard links share their verified source's content.

The same asset manager owns indexed assets and JAR-resource preparation; its
private archive adapter uses libzip without leaking ZIP APIs into game consumers.
Validate all archive entry paths and types; reject traversal, absolute/aliased paths,
links, and selected resource file/directory conflicts. Select actual files beneath
`assets/<namespace>/` and root pack resources; exclude Java/executable extensions
and unrelated package/signature metadata. Strip only the leading `assets/` from
JAR resources: `assets/minecraft/textures/...` maps to
`assets/reconst/minecraft/textures/...`. Preserve every namespace, root pack
paths, and resource bytes; record original archive paths alongside logical names.
Reuse verified files from either previous nested or flattened layouts, then remove
only verified obsolete originals and empty directories after
reconstruction succeeds; preserve indexed/unknown files. Never invent categories. Bound entry count/size/expansion and verify CRC,
size, and derived SHA-1 before atomic promotion. Equal-content JAR entries can share
hard links; indexed/JAR collisions share a path only for identical spelling and
content, otherwise fail before reconstruction. Receipt sections `reconstructed`
(index) and `client_resources` (JAR, per-file SHA-1/size/fingerprint), plus source
hashes, identify provenance. Completion requires both sources and their logical
files; always hash-check the cached JAR and selected extracted resources. Repair
from verified local sources without resource downloads when available, retaining
the existing account prerequisite. UI reports actual JAR bytes and applicable
archive entry counts. The client consumes reconstructed data, never ZIPs or Java
implementation code. Keep all acquired files outside source/releases/packages.

Game consumers resolve logical names with `maage_minecraft_asset_path` from
`MaaGE::MinecraftAssets` (`maage/minecraft_assets.h`), which writes a caller-owned
UTF-8 path and depends on storage rather than Launcher UI, HTTP, JSON, or rendering.
`MAAGE_BUILD_MINECRAFT_ASSETS` enables this shared resource module independently.
Keep Minecraft-specific reconstruction out of generic Core and graphics.
Runtime/UI behavior and non-macOS builds still require programmer validation.

## Planned engine systems

These are responsibility boundaries, not instructions to create empty directories
or interfaces now. Establish contracts incrementally as systems become necessary.

| System | Planned responsibilities |
| --- | --- |
| World | Worlds/dimensions, chunks, block-state storage, lighting, persistence, streaming. |
| Simulation | Configurable fixed-step ticking, scheduled block updates, events, parallelizable workloads, deterministic ordering where gameplay requires it. |
| Entities | Lifecycle, components/behaviors, AI, spawning, spatial indexing. |
| Physics | Collision detection, raycasting, movement, spatial queries. |
| Generation | Seeds, deterministic randomness, biomes, terrain, structures, configurable pipelines. |
| Resources | Textures/models/audio/data, loading/caching, format decoders, resource-pack composition. |
| Networking | Client/server communication, protocol versioning, serialization, entity/chunk synchronization, multiplayer sessions. |
| Content | Block/item registries, recipes, machines, materials, game rules, modpack-defined behavior. |
| Supporting services | Audio, input, UI, job scheduling, memory allocation, logging/diagnostics, optional scripting where appropriate. |

## Minecraft 1.7.10 compatibility and intellectual property

- Use Java Edition **1.7.10** for block/item behavior, chunk-based worlds,
  entities/physics, survival/creative foundations, crafting/inventories, redstone,
  scheduled updates, terrain generation, and traditional modded gameplay. Do not
  silently adopt later Minecraft versions as the baseline.
- Selected 1.7.10 world/resource formats or network protocols may eventually be
  supported. Exact binary compatibility, Java execution, and Forge compatibility
  are not requirements; existing Java mods will not be loaded or executed.
  Optimizations and improvements may intentionally diverge from vanilla. Make
  compatibility decisions explicit and document them here when durable.
- Independently implement project code. Do not copy or translate proprietary
  Minecraft or third-party mod source without appropriate authorization. Keep
  Mojang/Microsoft and third-party mod assets separate from original MaaGE code
  and content; never distribute Mojang's proprietary assets. A legitimate Minecraft
  installation alone does not resolve all asset-use permissions.
- Apache-2.0 covers original project code and documentation, not third-party
  materials or Minecraft/Mojang/Microsoft intellectual property. Preserve applicable
  licenses and disclosures. MaaGE is not official, affiliated with, or endorsed by
  Mojang or Microsoft.

## Modpack platform

Support many independently configurable modpacks, conceptually composed of a
versioned manifest, content definitions, resources, game rules, dependencies,
optional executable modules, and optional scripts. Prefer data-driven content
where appropriate; never hardcode individual modpacks into the core.

Native extensions should use stable, versioned C-compatible interfaces with explicit
ownership and lifecycle rules. Treat them as trusted executable code unless genuine
security isolation exists. Do not implement third-party mod compatibility layers
unless explicitly requested.

## Workspace and build conventions

- Put implementations under `src/` and public interfaces under `include/`.
  Configure each application in its own directory.
- Permanent naming: `maage_` for functions, variables, typedefs, symbols, and
  CMake targets/helpers; `MAAGE_` for constants, macros, include guards, and CMake
  options/configuration variables. Public headers/include paths use `maage`;
  project namespaces and aliases use `MaaGE`. Preserve external identifiers.
- Keep the root clean: `README.md`, `AGENTS.md`, `LICENSE`, `.gitignore`,
  `CMakeLists.txt`, `CMakePresets.json`, `CMakeVersions.cmake`, `CMakeApplications.cmake`, and the
  `CMakeConfig.h` template belong here.
  The three application directories and `src/core/` are the established source layout.
- Target compact Release executables (currently aiming below 60 KiB for the
  Launcher). `MAAGE_OPTIMIZE_SIZE` enables size optimization, supported LTO,
  dead-code removal and symbol stripping. Retain runtime metadata, protections,
  required backends, and useful diagnostics; report actual uncompressed sizes
  and distinguish executable bytes from bundle/disk allocation. Debug builds
  retain debugging support. Do not remove functionality merely to meet a size target.
- Keep authored top-level build configuration and active presets at the root;
  root `CMakeLists.txt` orchestrates the workspace. Reserve `cmake/<name>/` for
  generated configuration, headers, and intermediates; corresponding compiled
  output belongs in `build/<name>/`. Isolate presets/toolchains and multi-config
  outputs. Never generate into source directories or commit generated artifacts.
  Select platform-specific sources conditionally and exclude graphics dependencies
  from headless targets; portable targets retain strict C90 requirements.
- Keep build validation in `.github/workflows/build.yml` manually triggered only
  (`workflow_dispatch`) until explicitly authorized otherwise. Preserve read-only
  permissions and existing CMake layout.
- CI must provision and verify required build dependencies before configuring
  CMake. Keep them platform-specific and minimal, pin versions where practical,
  and fail clearly rather than disabling required applications or backends.
  Default graphics builds require Vulkan headers/loader on Windows/Linux and native
  Metal/AppKit/QuartzCore from the Apple SDK on macOS; do not install MoltenVK.
- Preserve the existing Git author and committer identity. Every commit created
  or amended on the user's behalf must include:
  `Co-authored-by: Codex <noreply@openai.com>`.

## Native application builds

- Launcher is the standalone graphical application: Windows GUI subsystem/WinMain,
  macOS `.app` bundle with generated Info.plist, and Linux `Terminal=false` desktop
  entry. Client is a separately versioned Launcher-managed native executable:
  Windows GUI subsystem, plain macOS/Linux binary, no bundle or desktop entry.
  Keep startup logic in application callbacks; share only entry-point glue.
- `maage_server` is one CLI executable using `MaaGE::Server`; an optional future
  native GUI runs inside that process. No `maage_server_gui` target or packaging.
  Preserve independent headless builds and defer GUI implementation.
- Keep GUI identity/installation configuration in root `CMakeApplications.cmake`.
  Preserve `cmake/` intermediates and `build/` outputs, including complete bundles.
  Apple bundle version fields are numeric; `MaaGESemanticVersion` stores full SemVer
  and Git metadata from the shared build-time version generator. Desktop commands
  must use actual installation paths, including install-prefix overrides.
- Core `MaaGE::Log` (`maage/log.h`) is the standard diagnostic API; use named
  `maage.*` loggers instead of ad hoc printf/file logging. Levels inherit through
  dot-separated names. Defaults are INFO, console on, file off, local timestamps;
  Launcher logs belong in `logs/` beneath the platform MaaGE application-data
  directory described above. Create directories
  as needed; never fall back to working/install directories for Launcher logs.
  Client logs currently remain beside its executable. Logging overrides remain
  supported; Server keeps console output.
  Configuration precedence is defaults → optional INI → explicit `--log-*` CLI
  overrides. Initialize early and shut down cleanly; FATAL never exits by itself.
  Keep logging synchronous, thread-safe, bounded, C90-compatible, and headless-safe.
- File logging rotates on initialization: the configured filename holds the latest
  session; previous files receive a local `YYYY-MM-DD_HH-MM-SS` rotation timestamp
  before the extension; add a numeric suffix only on filename collisions. Preserve archives without overwriting
  or automatic deletion; report rotation failures rather than append across sessions.
- Log severity labels are exactly `[TRACE]`, `[DEBUG]`, `[INFO]`, `[WARN]`,
  `[ERROR]`, or `[FATAL]`. Align logger names with padding after the closing `]`,
  never inside brackets (for example, `[INFO]  [maage.launcher]`).

## Independent application versions

- Launcher, Client, and Server independently follow SemVer 2.0.0, initially
  `1.0.0-alpha.1`. Maintain release fields centrally in root `CMakeVersions.cmake`,
  independently of `PROJECT_VERSION`; never duplicate constants in source.
  Headless and optional GUI modes of the Server share its release fields.
- Each build appends the same abbreviated Git HEAD hash as build metadata
  (`unknown` without Git metadata), refreshing generated C90 headers under the
  active `cmake/` tree even without reconfiguration. Metadata uses SemVer-safe
  ASCII characters and does not affect version precedence.
- Increment the affected application's version when appropriate to the scope of
  changes; increment prerelease identifiers during alpha development. Do not bump
  release numbers merely for a new commit: the hash distinguishes individual builds.

## Conceptual development sequence

Phases may overlap; this is a roadmap, not a schedule or authorization for later work.
The workspace foundation exists; phase labels do not certify completion or validation.

0. **Workspace Foundation:** CMake, independent targets, C90, platform configuration,
   manual GitHub Actions build validation, documentation.
1. **Portable Core:** allocation/memory, logging, error handling, basic containers,
   filesystem/timing abstractions, module lifecycle.
2. **Native Platform Layer:** Win32, X11, Wayland, Cocoa/AppKit, window events, input.
3. **Graphics Foundation:** backend-independent API, Vulkan, native Metal, Null,
   shader pipeline, basic rendering validation.
4. **Voxel Engine:** block registries, chunks, world storage, meshing, terrain
   generation, persistence, streaming.
5. **Gameplay:** simulation, entities, physics, player controls, inventory,
   interactions, survival/creative foundations.
6. **Client/Server Integration:** networking, dedicated-server simulation, client
   synchronization, singleplayer integration, multiplayer compatibility.
7. **Modpack Platform:** manifests, resource composition, native modules, game rules,
   dependency management, optional scripting.
8. **Launcher and Distribution:** profiles/installations, Microsoft authentication,
   authorized asset acquisition, ownership verification, modpack management,
   updates/distribution.

## Documentation and persistent context

- `AGENTS.md` is the exclusive home for persistent AI-facing instructions,
  architectural decisions, user expectations, lessons, recurring mistakes, and
  project context. Do not create separate AI instructions, memory files,
  scratchpads, agent notes, duplicate context documents, or a secondary hierarchy.
- Keep `README.md` human-facing: overview, goals, status, build guidance, and
  disclosures. Preserve its opening AI-routing HTML comment, human–AI development
  disclosure, and legally relevant notices. Do not add visible agent instructions.
  Use MLA title capitalization for README headings and concise, professional prose.
  Do not add a design-goals section or feature-card block to the README.
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
