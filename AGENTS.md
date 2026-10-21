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

### Operation outcomes and resource resilience

- Core `MaaGE::Status` (`maage/status.h`) is the universal operation-outcome contract:
  verified unsigned 32-bit `0xSSCCOODD` (Status, Context, Operation, Detail).
  The public header is the authoritative stable numeric registry; unassigned bytes
  remain unknown. Severity derives from the top two status bits; success context
  is zero. Serialize explicitly as SS, CC, OO, DD bytes, independently of host layout.
- Nearly all meaningful operations return or participate in status reporting,
  including successful mutations/lifecycle changes and explicit NOOP outcomes.
  Keep exceptions intentional: pure math/predicates, direct getters, native ABI
  callbacks/action outputs and invariant-only synchronization primitives. Immediate
  UI drawing accumulates a canonical batch result instead of checking each glyph.
  Native process exits explicitly convert results. Do not reintroduce competing
  error enums, boolean wrappers or logging-only operation contracts.
- Preserve originating results through synchronous propagation and task-owned
  bounded causal traces; recovery has its own result. Jobs copy traces by value
  through existing handoffs, never borrowing caller stack data. Callback outcome
  survives cancellation; cancelled and stale dispositions remain distinct. Reset
  a trace after handling an occurrence, before unrelated work. No global last-error,
  trace locks, unbounded histories, per-call heap allocation or hot-path formatting.
- Success/NOOP, usable warnings/partial results, errors and fatal outcomes remain
  distinct. Status severity and log verbosity are independent. Log hex results,
  causal operations/source locations and separate paths/IDs/native errors once at
  the handling boundary; filter logging before formatting trace diagnostics.
- Optional visual failures use a generated magenta/black nearest-sampled atlas tile,
  minimal font/default tints or omitted decorations, retaining useful visuals.
  Cache failures per immutable resource generation; explicit destroy/reload retries.
  Resolve numeric UVs before gameplay. Never download resources from the Client or
  replace authoritative corrupt/unmapped saves with air. Brief deduplicated notices
  link important recoverable outcomes to their exact result in `logs/client.log`.
- Reserve FATAL reporting for unsafe continuation: required registries, critical
  allocation/invariants, corrupt authoritative data or no usable rendering backend.
  Preserve files and clean up normally; never conceal programming corruption.

### Numeric runtime identity

MaaGE uses numeric IDs internally. External namespaced identifiers are resolved
during registration, initialization, or explicit data-import boundaries. Gameplay,
simulation, block access, collision, meshing, physics, and rendering must not
perform string-based identifier comparisons or name-to-ID lookups. Numeric
ID-to-definition lookups must be direct, deterministic O(1) operations.

- Block types and states have distinct numeric domains; states reference types
  and resolved numeric metadata/properties. Biome definitions also use direct
  numeric indexing; generation, streaming and lighting never compare names.
  Preserve 1.7.10 IDs/metadata where
  practical without permanently limiting ID capacity to the legacy format.
- Item types have their own immutable numeric registry, independent of block/state
  IDs. Portable fixed-size stacks carry count, metadata and damage; inventory and
  simulation never resolve names. Display names remain presentation data.
- Finalize registries before gameplay. Definitions then remain immutable;
  unknown/unregistered numeric IDs return explicit failure, never unrelated blocks.
  Name hashing is collision-checked at registration/import only and has expected,
  not guaranteed worst-case, O(1) lookup. Runtime blocks carry no identifier strings.
- Resolve texture handles/atlas rectangles before meshing; no resource paths,
  decoding, name resolution, or per-block/per-face allocations in gameplay loops.
- `MaaGE::World` owns portable registry, numeric chunk storage, CPU meshing and
  movement/collision contracts independently of Client, graphics and native UI.
  Client coordinates input/camera/presentation; a future Server can consume the
  same portable simulation without graphical dependencies.

### World architecture and persistence

- Use 1.7.10 geometry: 16x16 columns, up to sixteen sparse 16x16x16 sections,
  Y=0..255. Negative coordinates use floor division and positive modulo.
  Coordinate-keyed hashing is expected O(1), not deterministic worst-case O(1).
- Seeded generation is versioned and independent of load order/scheduling.
  Signed 64-bit seeds use C90-compatible word pairs. Generator 1 retains
  its frozen Y=63 and spawn-search behavior. Generator 2 permits explicit,
  immutable per-world sea level and spawn-search policy, saved in metadata
  schema 5 and propagated into asynchronous World jobs. Existing worlds
  retain their recorded version/policy; never change one on load. Generator 3
  adds ordered feature stages, bounded regional cave/ore/tree/structure plans
  clipped independently to worker-owned columns, without neighbor waits. New
  Clients select version 3 only for new worlds. No generator claims Java
  Random/GenLayer or seed-for-seed parity.
- Parallelize independent CPU-intensive world work by default through reusable
  `MaaGE::Jobs`, shared by Client and future headless Server. Use bounded pools,
  never threads per object. Automatic worker count is logical processors minus
  one, with a minimum of one; detect the processor count through Core's platform
  abstraction. If detection fails, use eight workers directly. Positive explicit
  counts have no arbitrary ceiling; allocation
  and OS resource failures remain explicit. Bound accepted contexts, retained
  snapshots and queued/running work independently of worker count. World
  cache and accepted-job capacity are runtime-configurable; defaults derive
  from footprint and pool size rather than fixed hard ceilings.
- One owner thread controls World membership, published immutable columns,
  simulation and deferred result commits. Workers own their output buffers and
  borrow immutable inputs/registries until completion. Independent column load,
  generation, lighting, CPU mesh and save tasks may overlap. No worker mutates
  published columns or Client gameplay state. Keep fixed-step player updates
  ordered; parallelize existing gameplay only when workloads are independent.
- Localize and justify synchronization: brief scheduler queue/token handoffs and
  completion notifications are necessary; global World locks, frame barriers,
  render-loop waits, and locks around generation, filesystem I/O or GPU work are
  prohibited. `maage_world_stream` polls commits and schedules bounded near-player
  work on the owner. Snapshot/column/neighbor revisions reject obsolete results;
  unloading invalidates requests, and cancellation remains cooperative.
- Lighting writes private snapshots and propagates through staged neighbor
  boundaries. Commit only when required revisions still match. Graphics owns
  copied meshes and the shared atlas on its rendering thread; validate result
  tokens before uploads, invalidate affected neighbor boundaries and preserve
  unchanged GPU resources. No graphical types belong in portable World tasks.
- Save coherent immutable snapshots asynchronously, with at most one writer per
  column file. Clear dirty state only if the saved revision is still current;
  never evict unsaved dirty data. Shutdown cancels obsolete CPU work, drains and
  joins jobs, and flushes dirty columns outside the rendering loop. Borrowed
  registries, stores and snapshots must outlive all jobs that access them.
- Missing/loading columns block collision; never turn unavailable terrain into
  collision-free air or teleport the player. Preserve fixed-step movement.
- Advance environment time/weather and bounded numeric scheduled updates on the
  owner at 20 TPS, respecting pause. Deduplicate column-owned fluid queues and
  batch copied publications; auxiliary-only edits preserve geometry epochs, and
  lighting commits retain current auxiliary state. Persist remaining update delays,
  numeric block-entity payloads and clock/weather (chunk 3, metadata 6), reading
  older formats without changing generators. Static sky/block light remains 0..15;
  frame daylight scales sky only, without tick-driven global relighting.
- Store new worlds beneath `instances/<instance-uuid>/worlds/<world-uuid>/`,
  separate from shared Minecraft resources. Preserve imported legacy IDs.
  Format/generator/blockset versions bind stable numeric state mappings; changes
  require explicit migrations. Validate save lengths, coordinates, IDs and CRCs,
  sync temporary files before atomic promotion, preserve corrupt originals,
  serialize writers with locks and flush on clean shutdown. World/biome/storage
  services remain reusable by headless Server without graphical dependencies.

### Client game UI

- Client-owned `src/client/ui/` remains separate from native Launcher widgets.
  Register screens before finalization through numeric IDs and direct O(1)
  indexing; gameplay/HUD is ID 0, Pause 1, Options 2 and Inventory 3. Navigation uses a numeric
  stack: submenus push, Back/Escape pops, and the root returns to gameplay.
  Unknown IDs and navigation cycles fail explicitly. No runtime name lookup.
- Keep registration/navigation, input routing, immediate drawing primitives,
  resource loading and debug collection separate. Reuse reconstructed 1.7.10
  icons/widgets/ASCII font through existing Image/Texture APIs at initialization;
  no resource acquisition, new texture manager or native widget toolkit.
  Logical GUI coordinates scale independently of framebuffer resolution.
- The Client Leaves option is persistent: Fast uses opaque textures and aggressive
  internal-face culling; Smart uses cutout textures and culls leaf-to-leaf faces
  obscured behind two face-facing leaf layers or a leaf backed by an opaque block,
  retaining the exposed and first interior cutout planes per direction; Fancy
  (default) uses cutout textures and preserves all internal leaf faces. Smart
  must meaningfully reduce geometry without Fast's hollow-canopy appearance.
  Mode changes invalidate only World meshes and rebuild through Jobs, preserving
  lighting and simulation.
- Client persists FPS, VSync, GUI Scale, Leaves and Window Mode in per-instance
  `config/options.ini`, using numeric settings in memory and atomic Core Storage replacement.
  Game Mode remains world data. Save on Options exit, completed F11/window transitions,
  and normal shutdown, never per FPS-slider frame or during native transitions.
  Client Window Mode cycles Windowed, Borderless and Fullscreen in one Options button.
  F11 toggles Windowed and the last non-windowed choice (default Borderless).
  Non-macOS Fullscreen uses supported borderless fullscreen, never fabricated exclusive mode.
  macOS Borderless stays in its desktop Space and covers the display, while Fullscreen
  uses native AppKit Spaces. Both request full-display content; GUI respects the safe
  area without cropping the world. Embed a Client-specific Info.plist in its standalone
  Mach-O executable (not the Launcher bundle) to disable notch compatibility mode;
  native AppKit fullscreen may still reserve the top camera/menu-bar strip. Track asynchronous mode completion and restore
  windowed geometry and input capture correctly.
- Modal screens release capture and pause fixed-step gameplay; closing them
  restores gameplay capture appropriately. Keep rendering/events and the safe
  World completion/scheduling pump alive while paused. HUD/F3 is not a modal.
- Visible UI, input and F3 gameplay state update every rendered frame without
  artificial menu delays. Keep F3 compact: one version/hash, FPS/TPS, chunks,
  live position/facing/target/biome/light, memory, OS/GPU and compact workers.
  Performance uses a 0.75-second exponential history displayed at 250 ms intervals;
  memory uses the same observation interval. Format static identity only on change.
  Reuse unchanged right-column geometry with text/viewport/font/texture invalidation.
  Never delay or smooth player coordinates, targeting, biome or light.
  Read-only telemetry uses no per-frame allocation,
  World lock, job wait or disk I/O. One brief scheduler snapshot lock
  may copy bounded numeric job states; never read worker-owned output fields.
  Diagnostic text/names are presentation only, never gameplay identifier lookup.
  Omit unavailable data or report N/A; resident sections are not visibility counts.

### Player interaction and inventory

- `MaaGE::World` owns portable item/inventory and Creative/Survival rules alongside
  movement. Owner-thread 20 TPS intents control breaking, placement, vitals and
  bounded pickups; Client owns native input, camera, GUI and visual feedback.
- Shape-aware numeric DDA stops at unavailable terrain. Definitions resolve
  hardness, tool requirements, drops and separate selection/collision AABBs before
  gameplay. Placement-state callbacks consume numeric metadata and face inputs.
- All block changes use owner-thread expected-state mutations that publish copied
  immutable columns, invalidate lighting/neighbor mesh revisions and reject stale
  jobs/uploads. Retain asynchronous saves and dirty-eviction protection; never
  mutate worker-borrowed storage or introduce a global World lock.
- Player metadata schemas 1-3 remain readable with their legacy numeric item
  mapping; schema 4 preserves namespaced item identities and extensible pickup
  counts. Schema 5 additionally persists generator-2 parameters. New chunk writes
  use local stable-name palettes (chunk format 2); format-1 mappings stay readable.
  Save final player metadata only after rendering and successful World flush.
  Respawn retains inventory at session-start position; full death/entity/food
  rules remain provisional against Java Edition 1.7.10.

### Physical instances and shared folders

- Instances are persistent UUID-identified gameplay environments, with authoritative
  `instances/<uuid>/instance.json` manifests. Launcher schema 3 stores UUID references
  and selected/default UUIDs, not duplicate name/build/mapping records. Display names
  never determine paths; Client binaries and Minecraft resources stay globally shared.
- `MaaGE::Instance` is reusable, C90 and independent of Launcher/UI/graphics. Numeric
  folder kinds select cached startup paths. Worlds, mods, config and logs are private;
  only screenshots, shaderpacks and resourcepacks currently support explicit private,
  managed-shared or user-selected external mappings. No virtual/home instances,
  global world library, implicit data moves or version-dependent config sharing.
- Manifests govern links; Core Storage implements POSIX directory symlinks and
  Windows local-volume junctions. Validate UUIDs, root containment, link targets and
  conflicts. Never replace populated directories. Serialize admin changes with the
  UUID-scoped use lease; currently one Client per instance. Preserve world locks,
  async chunk I/O, atomic saves and revision rejection without a global World lock.
- Resolve `--instance=<uuid>` (or the Launcher selection) before Client logging and
  world startup. The global data root locates shared resources only; gameplay never
  falls back to global worlds. Resolve paths only at initialization/I/O boundaries.
  Client descriptors declare numeric `instance_storage: 1`; retain but refuse to
  launch legacy builds that could ignore instance selection and use global worlds.
- Legacy migration requires the sole configured instance or explicit destination.
  Lock all source worlds, journal before atomic relocation, verify copy fallbacks,
  retain copy originals/recovery data and refuse occupied destinations. Ambiguity,
  active writers and invalid data defer migration without resetting saves. Instance
  deletion archives its physical directory, preserving worlds and shared targets.

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
  Remain independent of client/server internals. Official Microsoft authentication
  and Java Edition access verification gate native first-run asset setup. Modpack installation and gameplay remain deferred; the selected native Client can
  now be started as an independent subprocess.
- **Client (`src/client/`):** native windowing, rendering, audio, input, UI, world
  presentation, simulation interaction, and multiplayer connectivity. Consume shared
  game definitions/protocols without depending on the server executable or private
  server implementation. Integrated singleplayer may use an in-process or separate
  server; that decision remains open. The Launcher distributes/selects independently
  versioned Client installations and starts them detached, so closing the Launcher
  never terminates the game. Client startup consumes an explicit versioned launch
  contract as gameplay develops; no standalone desktop entry or macOS app bundle.
  The Client initializes its own window/renderer. Never invent launch flags as
  authentication or implement remote distribution until requested.
  The Client extends Phase 1 movement/rendering with persistent seeded chunk
  streaming, numeric biomes, basic lighting, Phase 2.5 asynchronous World jobs,
  Phase 2.75 HUD/F3/Pause UI and Phase 3 interaction/inventory/gameplay foundations;
  see `docs/development-roadmap.md` for implemented versus provisional behavior.
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
- The indexed textured-mesh commands use neutral CPU geometry, shared owned RGBA8
  atlas resources, batched presentation and column-major transforms (clip depth
  0..1). Advertise `TEXTURED_MESH` only for implemented commands: currently Metal.
  Other families retain their existing clear/presentation functionality and return
  unsupported for gameplay mesh commands. MSL lives in an original `.metal` asset,
  embedded at build time and compiled by native Metal at initialization.
- Indexed UI batches use backend-neutral logical positions, atlas UVs and RGBA,
  drawn after world geometry with alpha blending and depth disabled, then one
  presentation. Advertise `UI` only where implemented: currently Metal. Other
  families explicitly reject unsupported UI commands; no feature-parity claim.
- Keep high-level voxel rendering separate from the low-level graphics device API.
  Chunk meshing, visibility, world simulation, and content must not depend directly
  on graphics APIs. Headless operation must not acquire graphics dependencies;
  changing the rendering backend must not require rewriting simulation.

## Launcher UI and asset setup

Prefer native operating-system infrastructure for the lightweight Launcher:
AppKit on macOS, Win32 controls on Windows, and GTK 3 on Linux. Launcher-owned
adapters remain separate from Game UI; do not create a shared widget framework.
Ordinary Launcher UI must not initialize a renderer. Core graphics remain for
actual rendering needs; `--renderer-demo` explicitly opens the existing RGB
demonstration after setup instead of the main Launcher.
Client processes own their graphics; the native Launcher never hosts their renderer.

Preserve the native initial setup experience. The main Launcher uses a compact
native sidebar with Play as the default page/principal action; News, Installations,
and Settings (including About) remain separate. Account selection belongs in the
Play header and controls service authorization. Keep Launcher state/configurations
in Launcher-owned modules, independent of game UI/rendering; use `config/launcher.json`
for nonsecret global preferences and UUID instance references; each instance manifest
owns its name, Client association and folder mappings. Persist instance/account selections and supported appearance
preferences; default appearance follows the system. Reuse existing account/setup
services for management and repair. Play starts the exact selected available Client
after access/resource prerequisites; show an empty News state until a real source exists. Do not introduce
webviews, heavyweight GUI frameworks, or a shared Launcher/Client widget framework.

First-run setup acquires Minecraft Java Edition 1.7.10 indexed assets and game
resources from its official client JAR, after verifying an existing Minecraft
account. Read the client URL, SHA-1, and size from official version metadata; cache
the verified source archive at `minecraft/cache/client-1.7.10.jar`. Never execute
it, extract Java bytecode/implementation code, or distribute acquired resources.
Minecraft 1.7.10 remains the fixed resource baseline, independent of MaaGE versions;
no Java runtime, server JAR, modpack import, or version-management system.

Verify Minecraft Access precedes Preparing MaaGE. MaaGE uses its own officially
approved Microsoft Entra public-client application
`ff5d912a-45ca-409e-a364-9d1bf9512f61`, with the consumer authority, system-browser
OAuth authorization code + S256 PKCE, and device-code fallback. The registered
Mobile and desktop redirect is `http://localhost`. A temporary worker-owned HTTP
listener binds only `127.0.0.1` on an OS-selected ephemeral port; authorization and
token exchange use the same pathless `http://localhost:<port>` URI, percent-encoded
once per request; the HTTP callback normalizes to `/`. Log only public endpoint,
client ID and redirect configuration, never the full authorization URL. Accept only
the expected GET path/Host and validated unpredictable OAuth state; bound reads, timeout and
cancellation release the listener. No custom URI protocol handler is required.
Never substitute application IDs, client secrets, or third-party credentials.

Xbox Live, XSTS, and Minecraft Services establish the Minecraft session. Verify a
valid Java UUID/username plus an explicit Java entitlement from authenticated
store/license responses; empty lists, Bedrock-only access, registry flags, and
Xbox sign-in alone never grant access. Confirm current access without inventing a
permanent-purchase guarantee. All network work runs on the existing access worker.
Never log credentials, callback URLs, payloads, or raw responses; wipe transient
secrets. Authorization diagnostics use `maage.launcher.access` and child loggers:
INFO for milestones, WARNING/ERROR for failures, VERBOSE for request timing and routine
polling. Log only static/allowlisted routes, bound loopback ports, error names and numeric
service/OS codes; exclude query strings, OAuth state/PKCE/device codes, account identities,
credential paths, untrusted error descriptions and response bodies at every level.
Keep native AppKit/Win32/GTK authentication UI lightweight and consistent
with setup, with actionable progress, cancellation/retry, and Continue disabled
until verification succeeds. No embedded browser or new widget framework.

Authentication and profile management share `config/minecraft-profiles.json` and
stable Minecraft UUIDs. Support account addition/selection, refresh/reverification,
sign-out and removal; imported profiles must sign in through MaaGE without reading
other launchers' credentials. Microsoft refresh rotation securely replaces the
saved credential before downstream exchanges. Keep Xbox/Microsoft access tokens
transient; persist only required refresh/Minecraft credentials and verified leases.
**Do not use native credential storage** (Keychain, Credential Manager, DPAPI,
Secret Service/libsecret). The programmer explicitly requires MaaGE-owned
AES-256-GCM records at `config/accounts/<uuid>.account`, authenticated to the UUID,
with a random local `key.bin`, owner-only permissions (0600/protected DACL), atomic
writes and a store lock. The local user can read both key and ciphertext.
Use OS cryptographic randomness for OAuth state, PKCE and local encryption.

The access service restores encrypted authenticated leases for at most one hour,
bounded by Minecraft token expiry, and refreshes saved Microsoft authorization
without interactive login when possible. Public registry metadata cannot grant
access. `--manage-accounts` keeps account controls accessible when startup could
automatically continue. Account selection restricts Launcher service authorization;
the controller and asset manager require a current verified lease for acquisition,
repair, retry and active work. Expiry, sign-out and closing revoke active access
without deleting resources. Authentication/storage remain Launcher-owned, outside
Client and generic Core. New Microsoft login must never access other launchers'
authentication files, OAuth clients, refresh tokens, or protected data.

The Launcher asset manager is UI-independent, with worker-thread operations,
locked progress snapshots, cancellation, Retry, and full repair (`--repair-assets`).
Core provides generic UTF-8 storage/path and HTTPS transfer APIs, without Minecraft
knowledge. Dependencies are libcurl >=7.85, json-c >=0.15, OpenSSL Crypto >=1.1.0, and libzip >=1.7.
Discover libzip through pkg-config on Linux to avoid distro CMake exports requiring
uninstalled command-line tools. GTK >=3.22 stays in the Linux UI adapter. Keep these
dependencies out of Server/headless targets.

Client resource loading uses `MaaGE::Image` (libpng >=1.6) for bounded PNG decoding
and `MaaGE::MinecraftAssets` for existing reconstructed paths. It never downloads,
authenticates or reads JARs. `--data-directory=<absolute MaaGE root>` is a resource
location argument passed explicitly by Launcher; direct startup defaults to the
platform MaaGE data directory. Missing/invalid required textures fail clearly.
Native input snapshots/capture are separate from gameplay state; focus loss clears
keys and releases capture. The Client uses bounded fixed 20 TPS updates with
interpolated presentation, pausing when capture/focus is lost. Physics and camera
tuning remain provisional until programmer fidelity verification.
`--world=<id>` loads a saved world; `--new-world` or `--create-world=<id>` creates
one, optionally with `--seed=<signed64>` and `--world-name=<name>`. A seed alone
creates a unique UUID world; default startup resumes the instance's `config/last-world` or creates a
unique world if none exists. Never overwrite an existing ID during creation or
change its generator/seed implicitly. `--render-distance=<positive integer>`
defaults to 8, subject to World-coordinate, allocation and resource constraints;
`--world-workers=<count>` selects a positive pool size (0 uses automatic policy).
`--mesh-upload-budget-ms=<milliseconds>` optionally fixes the GPU upload
budget; 0 uses an adaptive fraction of `--mesh-frame-target-ms` (60 FPS default).
Dynamic Client GPU mesh storage follows resident presentation demand.
World metadata opens while the viewport is hidden; final player metadata and
blocking flush/join occur after rendering stops, never inside the frame loop.
Generic `MaaGE::Storage`, `MaaGE::Thread` and `MaaGE::Jobs` have no Launcher acquisition
dependencies; optional `MaaGE::StorageHash` owns OpenSSL SHA-1 for asset acquisition.

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
Downloads are content-addressed, SHA-1/size-verified, use configurable
concurrency, retry count and network timeout/low-speed policies
(`MAAGE_DOWNLOAD_*` environment settings), and atomically promote temporary
files. Defaults are conservative rather than immutable capacity limits. Resume preserves completed objects; interrupted individual files restart.
An installation lock serializes writers. Initial setup and repair share one
verification/download/reconstruction implementation: cover every indexed entry,
deduplicate hashes, and reconstruct every logical path, without resource-type
filters. Completion requires both the verified object cache and valid logical
files. Track unique objects, logical files, cached/downloaded objects,
reconstruction progress, and outstanding missing/corrupt files separately.
Log actual file operations through Core logging: indexed names/hashes, download
URLs/destinations/attempts/retries, verified installation, migration, reconstruction
source/destination and hard-link/copy results, cancellation, failures, and summaries.
Use INFO for performed work and VERBOSE for per-file verification and unchanged cache
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
  `maage-client` on macOS/Linux and `maage-client.exe` on Windows. These stable,
  unversioned filenames are never macOS app bundles or separate desktop entries.
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
  dot-separated names. Defaults are VERBOSE for Debug and INFO otherwise, selected
  from the actual build configuration; thresholds include the configured level
  and all higher levels. Console is on, file off, local timestamps enabled;
  Launcher logs belong in `logs/` beneath the platform MaaGE application-data
  directory described above. Create directories
  as needed; never fall back to working/install directories for Launcher logs.
  Client uses the same Core logger with one live `logs/client.log` beneath its
  selected instance. Combine every severity and native stdout/stderr there, without
  duplicate console copies or separate capture files. Launcher startup capture
  appends to that path; Client adopts it after acquiring its instance use lease,
  redirects both streams there and GZip-copies it at shutdown. Preserve early startup errors
  and independent Client lifetime after Launcher exit.
  Logging overrides remain
  supported; Server keeps console output.
  Configuration precedence is defaults → optional INI → explicit `--log-*` CLI
  overrides. Initialize early and shut down cleanly; FATAL never exits by itself.
  Keep logging synchronous, thread-safe, bounded, C90-compatible, and headless-safe.
  Flush every record immediately and keep captured stdout/stderr unbuffered, including
  startup output. Log files are writable by the application user, without exclusive
  file locks; Windows handles share read, write and delete access so files can be
  inspected, overwritten or renamed while open. Keep scheduler/storage/use locks
  independent of log-file sharing.
- Retain the latest plain `client.log`/`launcher.log` after close. Shutdown/reinit
  flushes/closes it and creates a `client.YYYY-MM-DD_HH-MM-SS.log.gz` copy using
  local close time; suffix only on collisions. Skip empty archives. Before resetting
  the next plain session, preserve any unarchived output; reuse identical compressed
  copies. Stage/sync before collision-safe publication, never overwrite history,
  and retain the plain original on failure. `MaaGE::GZip` (`maage/gzip.h`) provides
  reusable, bounded C90 stream compression through private zlib >=1.2.3; all Core
  logging builds, including headless, require zlib without graphical dependencies.
- Log levels/labels are exactly `[VERBOSE]`, `[INFO]`, `[WARNING]`, `[ERROR]`,
  `[FATAL]`, in increasing severity. Old TRACE/DEBUG/WARN APIs and configuration
  spellings are compatibility aliases. Routine detail uses VERBOSE; lifecycle
  uses INFO. Align logger names with padding after the closing `]`,
  never inside brackets (for example, `[INFO]  [maage.launcher]`).

## Independent application versions

- Launcher, Client, and Server independently follow SemVer 2.0.0. The Client
  starts at `0.1.0-alpha.1`; its explicit release version is independent of Git,
  Minecraft 1.7.10, and the future network protocol. Maintain release fields centrally
  in root `CMakeVersions.cmake`,
  independently of `PROJECT_VERSION`; never duplicate constants in source.
  Headless and optional GUI modes of the Server share its release fields.
- Each build appends the same abbreviated Git HEAD hash as build metadata
  (`unknown` without Git metadata), refreshing generated C90 headers under the
  active `cmake/` tree even without reconfiguration. Metadata uses SemVer-safe
  ASCII characters and does not affect version precedence.
- Increment the affected application's version when appropriate to the scope of
  changes; increment prerelease identifiers during alpha development. Do not bump
  release numbers merely for a new commit: the hash distinguishes individual builds.

Client builds embed immutable version/build/platform metadata through a C90 API;
`--version` and schema-1 `--version-json` bypass logging and game initialization.
CMake produces standalone Client executables in versioned package directories
and an adjacent SHA-256-bound `maage-client.json`
development descriptor; importing never executes selected code. The descriptor
is not a remote-distribution signature. Explicit releases control Client SemVer;
do not bump it automatically for builds or commits.

The Launcher owns `config/client-builds.json` and side-by-side `clients/<build-id>/`
packages, separate from installation configurations and shared `minecraft/` resources.
UUID-referenced instance manifests select stable build IDs; never silently
substitute another Client. Migrate old configurations only when one available build
is unambiguous. Strict SemVer ordering ignores build metadata; identity also includes
build/platform/type and executable SHA-256. Exact duplicate imports are rejected;
changed builds coexist, and referenced builds must be unassigned before removal.
Development-only Replace Client copies/verifies a new build, saves all affected
installation assignments, then removes the old local package on the worker.
Keep the old build assigned if copying or saving fails; never replace running files.
Native Installations distinguishes Installed Clients from MaaGE Installations.
Debug Launchers (or explicit `MAAGE_LAUNCHER_DEVELOPER_IMPORT`) can inspect, confirm
and copy trusted local executable packages with the existing native UI. Pick the
standalone executable; reject app bundles and never execute code for inspection. Release omits
import APIs and controls by default; a Release Client remains importable by a Debug
Launcher. Package I/O runs on a worker; copy the executable and adjacent runtimes,
never move originals or link to developer source trees. Preserve only relative
POSIX package links whose resolved targets stay inside the package; reject escaping
links, Windows reparse points, special files, unsafe paths and bounded-tree
violations. Future verified remote distribution uses
the same registry origin distinction; no download service is implemented. Legacy
bundle registry records retain their IDs/references but require standalone reimport.
Play starts only the exact selected verified build, with current Minecraft access
and prepared resources. The reusable Core Process API invokes executables directly
with structured arguments, an explicit working directory, inherited environment,
nonblocking monitoring and combined append capture to the instance's `logs/client.log`. POSIX
fork/exec and Windows CreateProcess never use shells or Launch Services. Launch
validation/spawning runs on the Client worker; prevent duplicate launches and
removal of the active build. Closing the Launcher releases monitoring without
terminating the Client. The Client owns its native viewport and existing renderer
selection; metadata queries bypass both. Gameplay uses the portable persistent
World and Metal mesh renderer. Dedicated Server builds/registry/process management are a separate
future Launcher milestone; software versions do not define protocol compatibility.

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
