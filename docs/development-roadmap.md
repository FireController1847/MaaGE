# MaaGE Gameplay Roadmap

Minecraft: Java Edition **1.7.10** is a loose gameplay/content reference, not an
implementation, interoperability, seed-parity or architectural requirement.
Phases 1–5, including milestones 2.5, 2.75, 3.5 and 4.5, record retained foundations
and historical compilation notes. The descriptions reflect the current source:
Phase 7's shared Server owns simulation and persistence, and Phase 6 supplies
Client audio. Prior macOS ARM64 Debug/Release Launcher, Client and Server builds
and a separate headless Release Server build compiled and linked. The headless
build disables Window, Render, Launcher and Client. The subsequent Metal/Vulkan
consolidation has not been configured, compiled or executed on any platform.
No applications, tests, benchmarks, runtime/visual/audio validation or GitHub
Actions ran; Windows/Linux remain uncompiled here. Historical build notes below
do not establish current runtime correctness or vanilla fidelity.

MaaGE is in predevelopment. There is one current implementation of generation,
metadata/chunk serialization, Launcher preferences, packages and resource layout.
Old development data and APIs are disposable; format identifiers reject older
encodings rather than selecting historical algorithms, migrations or fallback
writers. Recreate rejected worlds/configurations/installations/caches as needed;
no data directory is automatically deleted.

**Permanent boundary:** Server architecture and multiplayer networking are
separate milestones. Phase 7 establishes a single authoritative Server runtime
shared by integrated single-player and dedicated headless hosting; Phase 12
extends it to remote multiplayer. All gameplay introduced after Phase 7 must be
Server-authoritative from the outset. Never create separate single-player and
multiplayer gameplay implementations. Preserve the independently buildable
Client, Server and Launcher and the portable shared libraries, numeric registries,
Operation Status, concurrency, instance ownership and rendering abstractions.

**Completion standard:** Each future phase requires observable behavior, not
merely compiling new infrastructure. Preserve existing functionality throughout
development, and fix known defects on their merits rather than automatically
deferring them as future polish. This document records scope and objectives; it
does not authorize implementing a phase.

## Phase 1 — Initial Voxel Client

Implemented foundation: standalone `maage-client`, version queries, native Window
and Input, reconstructed PNG resources, finalized numeric block/state registration,
indexed visible-face cube meshing, native Metal textured rendering, first-person
camera, fixed 20 TPS movement, jumping/gravity and swept AABB collision. Phase 2
extends these same modules; the nine-section static demonstration is superseded
by the normal generated world.

WASD walks and Space jumps. Phase 2.75 adds Escape pause-menu navigation and F3.
Focus loss releases capture and clears held keys. Camera position is interpolated;
long frames cap simulation catch-up. Inactive/hidden windows use event waits.
The player bounds, eye height, acceleration/drag/jump values and camera tuning
remain provisional pending programmer fidelity verification.

Metal and Vulkan now implement the same textured-mesh/UI/scene API in source.
Shared C90 code owns frame submission/planning, CPU geometry, material traversal,
translucent sorting, UI packing and pipeline rules; native backends own GPU resources,
command encoding and presentation. Native GLSL and MSL consume shared vertex/uniform
layouts, shading/animation equations and screen-coordinate calculations. Vulkan
shader builds require host `glslc` or `glslangValidator` and embed SPIR-V; Metal
requires neither Vulkan tooling nor runtime shader files. Compilation and visual
parity remain unverified after this consolidation. OpenGL and Software preserve
their existing clear/presentation paths and reject unsupported gameplay commands.
macOS Vulkan is enabled by default; `MAAGE_ENABLE_VULKAN=OFF` disables it. Its build
requires Vulkan headers (discoverable through `VULKAN_SDK`), host GLSL tools and
`libMoltenVK.a`. `MAAGE_MOLTENVK_STATIC_LIBRARY` can select a target-compatible
static archive explicitly. Graphical executables link MoltenVK and its required
Apple frameworks/system C++ runtime without requiring a C++ compiler for MaaGE.
No adjacent MoltenVK dylib, system Vulkan loader or ICD manifest is needed on
macOS. Used extension entry points are linked directly rather than rooting a
MaaGE procedure-name dispatch table. Release size optimization/dead stripping
remain enabled; the prebuilt archive's reachable implementation determines the
retained size, and cross-library LTO requires a compatible bitcode archive.
AUTO prefers Metal, then tries Vulkan when compiled in, before the existing
OpenGL/Software fallbacks; `--renderer=vulkan` explicitly selects Vulkan.
The Cocoa bridge owns/restores its
CAMetalLayer, updates backing-pixel dimensions and defers swapchain changes during
native fullscreen transitions. Device selection enables advertised portability
extensions and checks vertex-stride alignment. Static integration has not been
compiled or run; programmer builds and runtime/visual verification remain pending.
Explicit renderer requests never switch families. Cocoa, Win32 and X11
have native gameplay input; Wayland gameplay capture remains unsupported. X11's
warp-relative fallback retains native pointer acceleration.

## Phase 2 — World Generation and Chunk Streaming

Implemented in the existing portable `MaaGE::World` library:

- **Columns and sections:** 16×16 horizontal columns, up to sixteen sparse
  16×16×16 sections, Y=0–255. Each allocated section holds contiguous numeric
  states and packed skylight/block-light bytes. Columns hold numeric biomes,
  heightmaps, lifecycle, dirty flags and mesh revisions. No per-block heap objects
  or GPU resources live in World data.
- **Coordinates:** world X/Z map by mathematical floor division and positive
  modulo, including negative coordinates. Column lookup uses a collision-checked
  integer hash table with expected O(1) access and cluster repair on eviction.
  Numeric block/state/biome definitions use deterministic direct O(1) arrays.
- **IDs:** block types, states, items and biomes have separate finalized numeric
  registries with direct deterministic O(1) lookup. State definitions reference
  numeric block types and metadata, including biome grass variants. Built-in
  numbers are current registry choices, not an old save-mapping contract. Texture
  references are precomputed; name resolution stays at registration, presentation
  and save/import boundaries, never in generation, collision, streaming, lighting,
  meshing or draw loops.
- **Seeded generation:** signed 64-bit seeds are two masked 32-bit words in C90.
  One active algorithm hashes seed bits, coordinates and numeric stages into
  gradient/fractal noise. Climate sampling selects numeric ocean/plains/desert/
  extreme-hills/forest/taiga biomes; blended heights produce stone, surface/filler
  layers, bedrock and ocean water. The same pipeline always includes Phase 4's
  caves, ores, decoration, vegetation and structures. Persisted configuration
  contains only sea level and spawn-search radius/step, defaulting to 63 and
  4096/16. `maage_generation_policy_default` takes no algorithm selector. Client,
  integrated Server and dedicated Server share the algorithm and defaults.
  Heightmaps and lighting precede meshing. No mutable global PRNG or historical
  branch depends on request order, worker scheduling or render radius.
- **Streaming:** bounded asynchronous jobs handle loading/generation, lighting,
  CPU meshing and saving; the owner commits results and evicts columns. The
  bounded desired grid prioritizes nearby and
  view-facing columns. Render radius defaults to **8** without a fixed 8-chunk
  ceiling; the World cache, coordinate hash and accepted-job pool grow according
  to runtime capacities. A halo protects neighbor lighting and meshing.
  Fully constructed immutable columns are published by the owner. Missing/loading
  columns block collision, and simulation waits for the player's column. Dirty
  columns are saved before eviction; shutdown joins the pool and flushes saves.
- **Lifecycle and meshes:** requested → loading/generating → generated → lighting
  → mesh pending → ready, with saving/unloading and explicit failure states.
  The owner alone controls membership; jobs borrow stable column/neighbor snapshots.
  CPU mesh updates transfer through the completion queue and owner revision checks;
  the Client owns copied GPU resources.
  Neighbor arrival/removal invalidates affected boundaries and lighting. Existing
  meshes survive unrelated movement. Missing neighbors expose exterior faces
  until an updated culled mesh arrives. Water interiors are culled numerically.
- **Presentation:** one shared nearest-filtered atlas serves all chunk meshes.
  The graphics thread drains multiple changed meshes within a configurable or
  adaptive millisecond budget and batches draws into one clear/depth/presentation
  pass. GPU mesh storage grows with the actual resident demand. Actual reconstructed 1.7.10 grass,
  dirt, stone, bedrock, water, sand, gravel and sandstone PNGs are loaded once.
  Real grass/foliage colormaps populate biome definitions; grass atlas variants
  and sandstone top/bottom/side references are resolved before gameplay.
- **Lighting:** packed sky-high/block-low 0–15 levels. Direct sky, opaque occlusion,
  water attenuation and bounded six-neighbor propagation run on private worker snapshots.
  Both channels import neighboring column light and converge after arrivals,
  removals or changed boundary light. Numeric block emission properties seed the
  emitted channel, including lava and torch emission. Meshing combines neighboring
  light levels with temporary directional face shading. Lighting-only sections
  are published through controlled owner commits with stale-neighbor rejection.
- **Diagnostics:** copied Server snapshots expose world identity, position,
  generation and streaming statistics. VERBOSE diagnostics remain available at
  handling boundaries; compact F3 uses the copied observation/performance data.

### Creation and Selection

Resources and a physical instance must already be prepared by the Launcher.
Direct startup uses its selected/default instance; `--instance=<uuid>` selects one
explicitly. `--data-directory=<absolute root>` overrides the global resource and
registry root. Client never authenticates or downloads Minecraft resources.

```sh
maage-client                         # Title screen and instance world selection
maage-client --new-world              # Unique world ID and automatic seed
maage-client --seed=-123456789         # Unique world ID with the specified seed
maage-client --create-world=f4a2c9b1-8e36-4f1c-91ac-7d02ee39d82c --seed=42 --world-name="Example World"
maage-client --world=f4a2c9b1-8e36-4f1c-91ac-7d02ee39d82c --render-distance=4
```

`--world=<id>` loads an existing world. Creating an existing ID fails instead of
replacing it. Supplying a seed for an existing world is rejected. All world IDs
are canonical UUIDs; non-UUID development IDs are rejected. Generated
UUIDs and automatic seeds use OS cryptographic randomness. Instance-local
`config/last-world` remembers the selection. Separate
selection and lifetime world locks prevent concurrent writers.

### Save Format and Atomic Writes

Data lives beneath `instances/<instance-uuid>/worlds/<world-uuid>/`: `world.bin`
metadata and `chunks/<signed-x>_<signed-z>.chunk`. One current metadata encoding
stores UUID/name, signed seed, creation UTC, generation configuration, position/
orientation, inventory, typed item entities, vitals and clock/weather. Item stacks use stable
namespaced identities. One current chunk encoding stores namespaced block plus
state-metadata and biome palettes, lighting, generation-stage/feature counts,
relative scheduled-update delays and numeric block entities with checked payloads.
Palette indices are file-local, independent of runtime IDs.

Private format identifiers permit strict rejection. There are no historical
metadata/chunk readers, numeric identity conversion, generator/blockset version
branches or fallback writers. Explicit little-endian lengths/coordinates/values
and CRC32 detect malformed input; unresolved identities fail without replacing
saved data with unrelated blocks or air. A complete metadata save borrows the
store's finalized item registry and serializes one owned gameplay/environment
snapshot, without reloading the previous file or round-tripping through gameplay.

The Server saves final player state after successful World flush. An interrupted
first launch retains a pending-spawn flag; later interruptions retain the last
successful player checkpoint. Chunk snapshots save asynchronously with one writer
per file and dirty-revision protection. Writes sync complete temporary files before
atomic replacement; failed writes retain the previous current-format file.
Abandoned `.tmp` files are ignored on read and removed before another write.
Directory-entry power-loss durability is not claimed. Rejected earlier development
worlds must be recreated; no conversion, migration, backup or corruption-recovery
framework is provided.

### Generation and Remaining Limits

The active generator is an original approximation with coarse climate cells,
blended height fields, seeded gradient noise and regional feature replay. Java
Random, GenLayer, vanilla three-dimensional density, exact biome distribution and
seed-for-seed terrain are not goals. Historical generators and their selection
arguments/APIs are removed; future algorithm changes may invalidate worlds.
Phase 3 supplies inventory and interaction, while Phase 4 supplies generation
features, fluid updates and environmental presentation.
Water/lava now use animated still/flow textures and shared corner heights;
underwater fog/overlays remain incomplete. Lighting includes smooth corner
sampling and ambient occlusion, without claiming exact vanilla parity.
GPU positions remain floating-point world
coordinates, so precision decreases far from the origin. World coordinates are
bounded to the classic-scale ±30-million range.

Native threading, storage and randomness stay behind Core APIs. Generic storage
and headless World persistence require no Launcher UI, HTTP, JSON, OpenSSL,
archive, window or graphics dependencies. Optional asset hashing remains a
separate acquisition dependency. Both integrated and dedicated Server now own
these same portable data/simulation services; remote networking remains deferred.

Local build coverage is macOS arm64. Windows/Linux source paths and all runtime
world generation, streaming, lighting, save/reload and movement behavior require
programmer validation. No applications, tests, benchmarks, world-generation tools,
asset downloads or GitHub Actions runs form part of this implementation's checks.

## Phase 2.5 — Multithreaded Architecture

Implemented as an extension of the existing World and Client:

- **Portable scheduler:** `MaaGE::Jobs` uses a bounded native worker pool with
  prioritized FIFO work queues, completion polling/notification and cooperative
  cancellation. Capacity includes queued, running and unconsumed completed jobs.
  Zero workers detects logical processors and selects one fewer, with a minimum
  of **one**. Failed detection selects **eight workers** directly. Positive
  explicit counts have no arbitrary ceiling. Allocation
  and operating-system resources determine availability. Queue/snapshot bounds
  are independently configurable at runtime, with default cache sizing derived
  from the footprint and accepted-job sizing derived from pool/streaming demand.
  Threads and scheduler arrays are created
  once, not per chunk or frame. A brief scheduler mutex protects queue ownership and cancellation;
  it is never held while a task performs generation, meshing, lighting or I/O.
- **World ownership:** the caller thread owns membership, published immutable
  column data, request/revision tracking and result application. Workers borrow
  immutable numeric registries and input snapshots; each owns its outputs.
  `maage_world_stream` polls completions and schedules work without waiting.
  Collision reads stable owner-published data without a global World lock.
  There is no frame-wide barrier or arbitrary worker mutation of gameplay state.
- **Concurrent work:** independent columns can load, generate, light, mesh and
  save simultaneously. Near-player work takes priority over distant requests;
  duplicate work is coalesced, accepted jobs and retained memory are bounded, and
  obsolete CPU requests are cancelled as the desired grid changes. Lighting
  imports staged immutable neighbor boundaries rather than mutating adjacent
  columns. Generation derives random state solely from seed, coordinates and
  numeric stage; scheduling never selects a different algorithm or feature set.
- **Revisions and lifetime:** task results include column incarnation/revision
  identity and required neighbor revisions. Changed/unloaded inputs reject stale
  lighting and meshes; completion processing releases borrowed snapshots only
  after workers finish. GPU uploads validate the accepted mesh token again.
  Cancelled or failed work cannot replace newer published data.
- **Persistence:** workers serialize coherent immutable snapshots and perform
  atomic file replacement. Only one save may target a column file at once.
  Save completion clears dirty state only if the saved revision still matches;
  dirty columns remain resident until a successful save. Clean shutdown cancels
  obsolete CPU work, drains/joins the pool and flushes dirty revisions while the
  registry and store remain alive. Only the current metadata/chunk encoding is
  read or written; malformed authoritative data remains an explicit failure.
- **Execution phases:** Client owns input/native events, camera, UI, audio,
  rendering and GPU operations. The Server owner thread handles ordered 20 TPS
  movement/gameplay/environment and pumps World scheduling/completion, including
  while paused. Local commands and copied snapshots/events cross the boundary;
  owned CPU meshes transfer to the Client without exposing borrowed columns.
  GPU uploads use a configurable/adaptive millisecond budget, retaining unchanged
  resources. Server metadata startup happens while the Client viewport is hidden;
  shutdown flush, checkpoint and join follow the render loop.
- **Diagnostics:** `--world-workers=<count>` configures the pool (0 is automatic). Startup reports its
  actual count. Five-second VERBOSE logs report queued/running jobs, accepted
  generation, lighting, meshing and I/O work, lifetime completed/cancelled/stale
  counts, and approximate mean scheduling latency. No per-task production logging
  or runtime benchmark is required.

Synchronization remains at scheduler ownership handoffs and filesystem writer
locks, where shared queue state and exclusive save ownership require it. Worker
tasks have no graphics or native UI dependencies and serve both Server modes.
Owner-thread startup/shutdown metadata I/O is a lifecycle boundary; no chunk
generation, serialization, mesh construction or disk I/O runs inside the render
loop. Runtime concurrency, cancellation, determinism and recovery require programmer
testing; compilation alone does not establish their behavior.

## Phase 2.75 — Client UI Foundation

Implemented as a small Client-owned UI, separate from the Launcher's native
AppKit/Win32/GTK controls:

- **Foundation:** immediate text, textured rectangles, panels and buttons, with
  simple left/center/right alignment and viewport clipping. Registration,
  navigation, input routing, drawing, resources and debug collection stay in
  separate modules. One reusable CPU batch avoids per-frame heap allocation.
- **Numeric screens:** finalized definitions use direct O(1) numeric indexing.
  Gameplay/HUD **0** is the nonmodal state; Pause **1** and Options **2** are
  registered modals. Opening a submenu pushes its numeric ID; Back/Escape pops
  it. The root returns to gameplay. Stack storage follows the finalized screen
  count; unknown IDs and navigation cycles fail explicitly.
- **Screens and input:** the HUD contains a centered textured crosshair and an
  independently toggled F3 overlay. Escape opens Pause, whose actions resume,
  open Options or exit through normal Client shutdown. Options now persists
  supported video/window/audio settings and retains Back navigation. Mouse controls
  and keyboard selection use native input snapshots; menus release capture and
  pause fixed-step player updates. Rendering, native events and bounded World
  completion/scheduling continue while paused. Closing menus restores gameplay
  capture when focus permits; focus loss still releases capture and held keys.
  Visible UI and input update every rendered frame without artificial menu delays.
- **Resources and scaling:** existing reconstructed 1.7.10 `gui/icons.png`,
  `gui/widgets.png` and `font/ascii.png` provide the crosshair, buttons and pixel
  text. PNGs resolve once at initialization into a shared GUI atlas through the
  existing Image/Texture APIs. No downloads, embedded Minecraft resources or new
  texture manager. Integer GUI scaling uses logical coordinates independently of
  native window/framebuffer scale. Columns clip independently on narrow views.
- **Rendering:** backend-neutral indexed UI triangles carry positions, UVs and
  RGBA. UI draws after world geometry with alpha blending and depth disabled,
  followed by one presentation. Metal and Vulkan consume the same Client UI,
  scene and foreground contracts; projective panorama, Gaussian blur and additive
  batches remain renderer operations. OpenGL/Software retain their existing
  clear/presentation behavior and reject unsupported UI/gameplay commands.
- **F3:** Phase 4.5 refines this overlay into compact left/right columns with live
  gameplay observations and separately averaged performance measurements; see below.
- **Read-only telemetry:** Client reads copied Server observations; the Server
  takes one brief scheduler snapshot of bounded numeric states. No worker output reads,
  World lock, job wait, heap allocation or diagnostic disk I/O occurs per frame.

The ASCII font, compact layout, scaling, capture/navigation, pause behavior and
Metal presentation require programmer runtime/visual verification. Non-macOS
source paths remain uncompiled locally. Phase 3 extends this UI with inventory,
targeting and basic game-mode controls. Crafting, complete settings and multiplayer
menus remain future work.

## Phase 3 — Player Interaction and Core Gameplay

Implemented foundations extend the existing World, scheduler, input and Client UI:

- **Targeting:** allocation-free numeric 3D DDA returns block/state IDs, face,
  position, distance and selection AABB, including negative coordinates. Missing
  columns stop targeting. Default reach is 5 blocks in Creative and 4.5 in Survival;
  `--interaction-range=<positive blocks>` optionally overrides it, up to 64.
  Twelve depth-tested black edges outline the target. Selection and collision use
  separate registered shapes, including partial blocks and oriented torches.
- **Mutations:** owner-thread expected-state edits copy a column and publish an
  immutable replacement. Center/neighbor lighting and mesh revisions are
  invalidated; stale jobs/results cannot replace newer edits. Resident dirty columns
  save asynchronously, one writer per file; eviction retains unsaved data. GPU
  uploads remain deferred, with unchanged meshes retained while replacements run.
- **Breaking and placement:** Creative breaks instantly and places without
  consumption or drops. Survival accumulates hardness/tool-based progress at
  20 TPS, consumes placed stacks, applies tool durability where defined and
  harvests basic numeric drops. Placement validates replaceability, loaded
  destination, height and player AABB; numeric placement callbacks permit future
  state orientation. Ten raw reconstructed damage textures multiply the existing
  lit surface through the mesh pipeline, preserving biome tint and base textures.
- **Items and inventory:** a finalized direct-index item registry supplies built-in
  block items, from stone/grass/dirt/bedrock/sand/gravel/sandstone through Phase 4
  content, with stack limits, block/icon states, names and tool properties.
  Thirty-six fixed stack slots include the nine-slot hotbar, plus a cursor stack.
  Left click moves/merges/swaps; right click splits or places one. Numeric screen
  3 provides the classic storage layout and a paged Creative catalogue, reusing
  reconstructed GUI textures and the prepared block atlas.
- **Controls and HUD:** mouse buttons break/place; 1–9 and wheel select hotbar
  slots; E opens/closes inventory; Escape pops the screen stack. Left Control or
  grounded double-tap forward requests authoritative sprinting, with hunger, sneak
  and collision cancellation, sprint jumping and distinct exhaustion costs. Existing GUI
  scaling, capture/focus behavior, crosshair and F3 remain. The hotbar shows actual
  selection/icons/counts; Survival displays hearts and hunger. A shaded three-face
  held-block presentation with a basic swing uses the existing UI batch.
- **Game modes and vitals:** new worlds start Creative with registered block
  stacks. Options or `--gamemode=creative|survival` selects mode; new Survival
  worlds begin empty. Double-tap Space toggles collision-aware Creative flight;
  Space/Shift ascends/descends. Survival tracks health, hunger, saturation,
  exhaustion, fall damage, slow regeneration and starvation. Death opens Pause
  with Respawn, retaining inventory at the session-start position.
- **Drops and entities:** successful Survival harvests create World-owned item
  entities with distinct numeric instance IDs, velocity, swept collision, age,
  pickup delay, bounded spatial merge searches and partial inventory collection.
  Dormant entities remain owned across column eviction. The Client consumes
  separate complete, revisioned interest sets and caches block/sprite meshes.
  See [item entities](item-entities.md) for rules, limits and manual verification.
- **Persistence:** the sole CRC-protected metadata representation stores named item
  identities and all inventory/cursor/entity records. Chunk-local namespaced
  block/state/biome palettes resolve at import. Old schemas and numeric mappings
  are rejected. Server writes final player metadata after rendering stops and
  World flush succeeds.
- **F3:** target IDs/face/position, selected item/slot, mode/flight, health/hunger,
  reach/break progress, mutation totals and dirty columns join existing scheduler
  diagnostics through bounded owner snapshots.

This is an independent 1.7.10-style foundation, not gameplay or visual parity.
Fifteen tiered pickaxes/shovels/axes and shears now have numeric definitions,
correct harvest eligibility, mining speeds and durability. Drops include resource
items and metadata variants. GUI icons use shaded isometric blocks or sprites;
hand and world models reuse the same geometry generator and atlas. Food,
difficulty selection, armor and combat remain deferred; respawn retains
inventory. Sprint travel/jump costs follow the reference; hunger regeneration and the broader
food model remain provisional.

macOS ARM64 Debug/Release Client (Metal and Vulkan/MoltenVK) and portable headless Release
World/Server compilation passed. No application, test, benchmark, UI/runtime
validation or GitHub Actions run was performed. Windows/Linux input source
changes remain uncompiled locally; Wayland gameplay input remains unsupported.

## Phase 3.5 — Instance Storage and Shared Folder Mapping

Instances are physical UUID environments, extending the existing Installations
controls and Client-build registry. Names and selected Client labels remain the
visible UI; UUIDs determine storage identity. Minecraft resources and Client
executables are not copied into instances.

```text
MaaGE/
  minecraft/                      shared reconstructed Minecraft resources
  clients/<build-id>/              shared installed Client packages
  servers/                        reserved for future Server management
  shared/{screenshots,shaderpacks,resourcepacks}/
  instances/<instance-uuid>/
    instance.json
    worlds/<world-uuid>/{world.bin,chunks/}
    mods/  config/  resourcepacks/  shaderpacks/  screenshots/  logs/
  config/                         global Launcher/account/build preferences
```

The reusable, graphics-independent `MaaGE::Instance` library uses json-c >=0.15
and generic Storage. Manifest schema 1 contains `schema`, UUID `id`, display
`name`, `clientBuildId`, optional `clientArguments` (empty by default), optional
`createdUtc`, and `folders`. Unknown root fields,
including future modpack metadata, survive edits. Native installation editors save
Client CLI arguments per instance and preserve them when duplicating an installation
or replacing its Client build. Arguments accept up to 4095 UTF-8 bytes without control
characters. Separate arguments with spaces; single/double quotes preserve spaces
and empty values. Backslashes escape backslashes and double quotes, plus single
quotes/spaces outside quotes; single-quoted text is literal. For example,
`--renderer=metal --world-name="My World"` passes two arguments. No shell expansion
or command execution occurs. The Launcher supplies `--instance` and
`--data-directory`; custom arguments cannot override these flags. Invalid quoting
or reserved flags fail before saving or starting the Client.

A folder entry is one of:

```json
{"mode":"private"}
{"mode":"shared","library":"screenshots"}
{"mode":"custom","path":"/absolute/external/folder","userSelected":true}
```

The shared library must match its folder kind. Worlds, mods, config and logs
always remain private. Screenshots, shaderpacks and resourcepacks default private
and may explicitly map to their managed shared directory or a selected external
directory. Configuration sharing and `shared/configurations` remain deferred.
Missing custom targets, loops, root escapes, conflicting links and populated
private destinations fail clearly. No data is silently moved into shared folders.

Generic Storage implements directory symlinks on macOS/Linux and local-volume
directory junctions on Windows, without an elevation-dependent symlink requirement.
Administrative remaps stage the new association, retain the exact old link/empty
directory and atomically save the manifest; failures restore the old association.
Interrupted remaps retain `.<folder>.mapping-new` / `.mapping-old` entries for
explicit recovery while the instance is closed. Targets are never recursively
deleted. Instance deletion removes its Launcher reference and archives the physical
directory as `instances/.deleted-<uuid>` for recovery.

Launcher schema 3 retains the existing installation registry as UUID references:
`selected` and `default` UUIDs, and `installations: [{"uuid":"..."}]`. Name,
Client association and mappings are read from manifests. Only this current schema
is accepted; old preference schemas are not converted. Invalid manifests fail
without resetting configuration. Installed Client packages remain independent
and can serve several instances. Play validates the manifest/build pair on its
existing worker and passes `--instance=<uuid>` plus the shared data root. The
Client holds its own instance lease after Launcher exit.
Client metadata/descriptors require numeric `instance_storage: 1` and the current
standalone-executable package layout. Missing/unsupported descriptors and package
registry schemas are rejected, with no legacy bundle/default conversion. Import
and verification never execute selected code. No network protocol is implied.

Direct CLI startup accepts the same `--instance=<uuid>` or uses the selected/default
Launcher UUID. It never creates a random instance or falls back to global worlds.
Client Core logs and native stdout/stderr share one instance-local `logs/client.log`,
flushed live and retained after shutdown alongside a dated `.log.gz` copy, matching
Launcher logging. An explicit startup
mapping operation is available without adding a settings UI:

```sh
maage-client --instance=<uuid> --map-folder=screenshots:shared
maage-client --instance=<uuid> --map-folder=resourcepacks:custom:/absolute/external/folder
maage-client --instance=<uuid> --map-folder=shaderpacks:private
```

Only one mapping may change per startup; it requires an explicit instance UUID
and refuses busy instances or populated private folders. The Client then starts
normally. Numeric folder kinds resolve and cache paths during initialization;
world simulation/meshing performs no name lookup or instance-path probing.

Global worlds, migration journals/receipts, migration startup flags and pending-
migration UI warnings are removed. World loading and creation require canonical
UUIDs inside the selected physical instance. Earlier data is not moved, copied or
converted automatically; recreate rejected development worlds as needed.

Minecraft resources use only the shared `minecraft/` layout: indexed objects,
metadata and `assets/reconst/` logical files. No historical MaaGE directory reuse,
migration or cleanup is attempted. Current setup/repair retains authorized
Minecraft 1.7.10 indexed/JAR acquisition, hash verification, reconstruction,
cancellation and retry; verified current-layout local sources remain reusable.

An exclusive UUID-scoped instance lease currently permits one Client per instance
and blocks rename, deletion and remapping during use. Each world retains its own
lifetime writer lock. There is no lock across ordinary operations in different
instances. Asynchronous chunk saves/revisions and post-render shutdown remain.
The authoritative Server uses the same storage library without UI/graphics;
Launcher Server management, mod loading and complete shared-folder settings remain
deferred.

macOS ARM64 Debug/Release Launcher/Client and headless Release Instance/World/Server
builds passed. No applications, tests, benchmarks, runtime migrations, UI validation
or GitHub Actions were run. Windows/Linux implementations remain uncompiled here;
filesystem/link/remapping behavior requires programmer runtime testing. These are
historical compilation notes, not validation of the current refactor.

## Phase 4 — World Simulation and Environmental Systems

Implemented foundation in Client **0.1.0-alpha.6** and portable `MaaGE::World`:

- **Generation:** one current pipeline is used by every entry point. There is no
  generator-selection argument or historical execution branch. Eight numeric
  stages cover biome selection, terrain shaping,
  caves, underground resources, surface decoration, vegetation, structures and
  final heightmaps. Fluid initialization and existing asynchronous lighting follow;
  the final generation stage does not claim completed lighting.
- **Underground:** branching tunnels, air pockets, ravines, dirt/gravel deposits,
  underground water/lava pools and bedrock. Numeric feature tables use classic
  height bands for coal, iron, gold, redstone, lapis, diamond and hills-only emeralds.
- **Surface:** grass/flowers, oak/birch/spruce trees, cacti/dead bushes, forest
  mushrooms, snow and biome surfaces. Blocks use the existing reconstructed atlas;
  plants use crossed geometry, and snow/cactus/torch content has explicit shapes.
  Spawn planning shares tree/cactus geometry and accounts for snow thickness.
- **Structures:** numeric regional placement generates underground
  cobblestone/mossy dungeon shells with terrain constraints and an entrance.
  Chests, spawners, loot, encounters, pyramids, temples and villages are deferred.
- **Determinism/concurrency:** workers replay bounded regional owners in fixed
  coordinate order, with private seed-derived plans and writes clipped to their
  own column. Caves, veins, trees and rooms cross chunk boundaries without neighbor
  waits, a global PRNG or a new pool. Existing snapshot/revision checks still reject
  obsolete results. Coarse stage progress shares the existing brief Jobs lock.
- **Scheduled updates:** `maage_world_tick` runs once per active 20 TPS player step,
  processing at most 128 updates. Per-column heaps/hash indices deduplicate numeric
  `(kind, cell)` entries; an active-column cursor bounds inspection. Unavailable
  boundaries retain delayed entries without generating neighbor jobs. Each touched
  column publishes once per phase. Auxiliary-only changes preserve geometry epochs;
  lighting commits merge current queues/entities rather than overwriting newer state.
- **Fluids:** numeric water/lava sources, horizontal levels and falling states;
  downward-first spreading, recession, water source renewal, barriers and
  obsidian/cobblestone/stone interactions. Water updates every five ticks, lava
  thirty. Placement/removal wakes nearby fluids through the same mutation path.
  Private lighting jobs handle emission, caves, edits and cross-column propagation.
- **Time/weather/rendering:** a persisted two-word clock advances a 24,000-tick day.
  Modal/focus pauses stop simulation while streaming continues. Seeded clear/rain/
  thunder timers and intensity transitions advance on simulation ticks. Cold/wet
  observations select snow and deserts suppress precipitation. Primary GPU shaders combine static
  sky/block light channels with a frame sky factor, preserving emitted light at night
  without relighting. Existing draw submission accepts optional background/overlay
  batches for sky color, sun/moon movement/phases, bounded rain/snow and thunder flashes.
- **Persistence:** current-only metadata retains generation configuration, named
  item identities and clock/weather in the final checkpoint after successful
  World flush. Current-only chunks retain identity palettes, stage/feature counts,
  updates and block entities. Pending work serializes remaining two-word
  delays and rebases on load, independent of an interrupted session's older clock.
  Instance paths, asynchronous single-writer saves, CRC/identity/length checks,
  atomic promotion and dirty-eviction protection remain intact.
- **Block entities:** chunk-owned numeric type/version/cell records with copied,
  bounded payloads and owner set/get/remove APIs. Block-type replacement removes
  the record. Container/furnace/sign identities are reserved; gameplay is deferred.
- **Diagnostics:** clock/weather and generation/update counters remain available
  through owner observations and logs; Phase 4.5 keeps them outside normal F3.


This remains original approximate generation without Java Random/GenLayer,
vanilla three-dimensional density or seed parity. Built-in feature tables are
actively maintained; sea level and spawn search remain per-world controls.
No jungle biome or surface lakes are implemented. Ore blocks drop themselves;
tree variants use base-log drops. Fluids omit vanilla slope search/exact update
order, swimming, buoyancy and lava damage. Plant support/leaf decay, snow
accumulation/melting remain deferred; Phase 6 adds source-integrated weather audio.
Fluid textures are opaque
first frames. Precipitation is a screen-space overlay gated by the player's sky/
climate observation, without world-depth particles or distant roof occlusion.
Clock/player checkpoints occur on clean shutdown; chunks save independently.

macOS ARM64 Debug/Release Client and headless World/Server builds passed. No applications, tests,
benchmarks, runtime/visual validation or GitHub Actions ran. Windows/Linux and
runtime shader compilation remain unverified. Metal/Vulkan consolidation and Vulkan
gameplay drawing were implemented subsequently and have not been compiled here.

## Phase 4.5 — Client Resilience and Operation Status

- **Operation Status:** independent C90 `MaaGE::Status` provides an explicitly
  verified unsigned 32-bit `0xSSCCOODD`, field extraction, numeric severity,
  immutable definitions, bounded allocation-free diagnostics, explicit SS/CC/OO/DD
  serialization and failure propagation. `include/maage/status.h` is the sole
  authoritative numeric registry for status, context, operation and detail codes.
  Contexts cover resource, rendering, World I/O, GUI, scheduling and World work.
- **Integration:** detailed image/asset lookup, renderer compatibility, chunk
  load/save and GUI creation results retain source failures independently of
  successful fallback. Jobs publish callback results through the existing queue
  handoff, retain the original operation result when cancelled and distinguish
  cancellation from stale snapshot rejection. Failed optional CPU/GPU meshes keep
  existing visuals or omit the affected mesh; retry follows revision invalidation.
  Authoritative load/registry/invariant failures stop safely and preserve saves.
- **Logging:** inclusive VERBOSE/INFO/WARNING/ERROR/FATAL thresholds; actual Debug
  builds default VERBOSE, other builds INFO. Existing `--log-level=`, INI overrides,
  logger inheritance, synchronized output, stream capture and shutdown archives
  remain. Only canonical VERBOSE/INFO/WARNING/ERROR/FATAL APIs and spellings are
  accepted; TRACE/DEBUG/WARN aliases and redundant wrappers are removed. Structured
  records include the hex outcome and separate path/column/job context; failure
  caches and owner handling boundaries prevent repeated reports every frame.
- **Visual recovery:** an original generated magenta/black checkerboard occupies a
  canonical shared nearest-sampled atlas tile before source registration. Failed
  block/item sources resolve numeric UVs to it; unrelated tiles remain available.
  Optional colormaps use documented green tints; absent GUI decorations use original
  simple controls/checkers and an original minimal ASCII font. Missing sun/moon/
  precipitation images omit only that visual. Sources resolve once per immutable
  resource generation; explicit destroy/recreate retries, without Client downloads.
- **F3:** one version/hash, integer FPS and measured TPS/frame time, compact chunk
  counts/distance, live XYZ/block/chunk/facing/yaw/pitch, biome/light and targeted
  block. The right column keeps available memory, OS/architecture, renderer/GPU,
  logical cores and one worker/queued-chunk line. Removed duplicate version/build,
  seed/world UUID, resident-section/vertex totals, gameplay counters, weather/time,
  generator stages and detailed scheduler/upload counters. Columns clip separately.
  Position/target/biome/light update each frame; FPS/TPS/frame time and worker use a
  0.75-second exponential history displayed every 250 ms. Native memory is sampled
  on that interval, and static identity is formatted only when the renderer changes.
  Unchanged right-column geometry is cached with text/viewport/font/texture keys;
  an optional cache allocation failure preserves immediate drawing. Adaptive upload
  budgeting continues to use the latest frame interval rather than F3 averages.
  The Window API has no reliable refresh-rate observation, so Hz is omitted.
- **Error visibility:** a nonmodal six-second notice shows the exact Operation
  Status hex and `logs/client.log`; recurring identical results are deduplicated.
  F3 does not acquire an error table. Required allocation, registry, device and
  authoritative-data failures remain controlled shutdown conditions.

macOS ARM64 Debug/Release Client, Launcher and Server builds and the separate
headless Release World/Server build passed. Runtime fallback, font/layout,
concurrency and shader behavior require programmer verification. Windows/Linux
and other gameplay-renderer capabilities remain unverified. No applications,
tests, benchmarks, graphical validation or GitHub Actions are run for this phase.

## Phase 5 — Universal Operation Status & Execution Tracing

- **Universal outcomes:** Core, platform services, Window/Input/Render, resource
  loading, World/simulation/registries/persistence, GUI, instances, Launcher/auth
  and the implemented Server lifecycle use canonical `maage_status` results.
  Factories return owned outputs explicitly; SUCCESS and NOOP remain distinct,
  warnings retain usable output, and failures describe the originating operation.
  Superseded renderer/window/registry result enums and resource/World compatibility
  wrappers are removed. Native callback actions, direct observations/predicates,
  arithmetic and invariant-only lock/wake primitives remain intentional exceptions.
  Immediate drawing reports through the enclosing GUI batch's canonical result.
- **Causal diagnostics:** `include/maage/status.h` remains the sole authoritative
  registry and tracing reference. The stable unsigned `0xSSCCOODD` result is usable
  alone. An optional caller/task-owned trace stores twelve fixed-capacity frames,
  original outcome, final disposition, recovery and an overflow count. Frames use
  numeric operations, static source locations and separate native error numbers;
  there are no dynamic diagnostic strings or global last-error state. Forwarding
  preserves the original result rather than repeatedly replacing it with generic
  dependency errors. Handlers log one causal diagnostic and reset before unrelated
  operations. A bounded trace is a logical operation history, not native unwinding.
- **Async ownership:** Jobs copy submission context and worker-owned trace records
  through existing queue/completion handoffs. World generation stages, lighting,
  meshing, load/save and Launcher worker services retain originating failures.
  Cancellation preserves callback outcome; stale rejection is a separate disposition.
  No caller stack pointers cross jobs, and tracing adds no global lock, barrier or
  render-loop wait. Shutdown keeps borrowed World storage/registries alive if worker
  termination fails; unsaved data cannot be reported as successfully flushed.
- **Cost and recovery:** ordinary success propagation is a numeric check, with no
  allocation, formatting or trace append. Trace reset touches metadata only;
  bounded records copy at existing async handoffs. Log filtering precedes diagnostic
  formatting. Phase 4.5 fallback textures/fonts/omitted decorations, deduplicated
  notices, compact realtime F3 and immutable revision rejection remain. Deferred
  gameplay actions report PARTIAL while preserving the original failure and state.
- **Limits:** this is authored logical tracing, not exhaustive native stack capture
  or a profiler. Traces may truncate after twelve frames and report the dropped
  count. Native API translations cover the implemented boundaries; no networking,
  protocol, remote distribution or additional renderer features are introduced.
  Runtime behavior and Windows/Linux compilation remain programmer/platform work.

macOS ARM64 Debug/Release Client, Launcher and Server builds and the separate
headless Release World/Server build passed. No applications, tests, benchmarks,
runtime/visual validation or GitHub Actions were executed.

## Phase 6 — Audio Engine & Soundscape

**Source-integrated; macOS compilation passed, observable completion unverified.**
The native audio foundation and Client integration compile in macOS ARM64 Debug
and Release. Other-platform compilation and runtime/audio behavior remain
unverified. The following requirements remain the acceptance criteria:

- Abstract audio backends and load/play sound resources, including the Minecraft
  1.7.10 sound assets and their mappings.
- Register sounds as numeric events, resolving resource names at setup/registration
  boundaries rather than inside gameplay loops. Server now emits numeric semantic
  sound notifications without an audio device or Client implementation dependency.
- Movement-distance footsteps, four-update mining damage, distinct
  successful destruction/placement sounds and supported damage, fall, splash,
  swimming, pickup and tool-break events. Ordinary jumps, landings and inventory
  slot changes are silent. UI buttons click on press; volume sliders preview on
  release. Protocol 5 retains explicit input edges, coherent sound identities,
  local emission timestamps, pause and owner-sampled soundscape observations.
- Cave sounds require authoritative dark-air samples and bounded cooldowns. Rain
  uses actual precipitation surfaces; water/lava use conditional display trials,
  with bounded copied source positions. Thunder remains silent until genuine
  lightning events exist. Music uses context-specific pools/delays and pause rules.
- Add background music with appropriate playback scheduling, volume categories
  and per-instance persistent settings, and positional audio where applicable.
- Handle missing or invalid audio assets without crashing; report failures through
  the existing Operation Status contract and retain usable playback.

**Observable completion:** Ordinary gameplay audibly responds with the correct
events, ambience, weather and music; users can control volumes, and unavailable
sounds degrade gracefully. Merely linking an audio library is insufficient.

## Phase 7 — Authoritative Simulation & Integrated Server

**Source-integrated; macOS compilation passed, observable completion unverified.**
Debug/Release application builds and a separate headless Release build compile
on macOS ARM64. Runtime/lifecycle behavior and Windows/Linux remain unverified.
One reusable `MaaGE::Server` owns content, World, player, gameplay, environment and
persistence on its authoritative thread in both entry points:

- Integrated single-player runs that Server inside the Client process; the
  independently buildable dedicated executable uses the same runtime headlessly.
  Neither duplicates gameplay, and no TCP transport is required for single-player.
- The Server owns ordered fixed **20 TPS** simulation, startup/restore, generation,
  streaming/completion commits and shutdown persistence. The safe World pump
  continues while integrated gameplay is paused. Borrowed registries, stores and
  local transport outlive every worker that uses them.
- A bounded local session carries numeric sequenced commands, copied snapshots,
  events and Operation Status traces. Owned CPU meshes transfer to Client GPU
  presentation; neither side exposes worker-private columns or borrowed stack data.
- Client handles native input, UI, camera/interpolation, rendering, audio and
  optional presentation prediction. Inventory, movement and block outcomes remain
  Server decisions. Server sound notifications use the same numeric event contract.
- After rendering stops, Server shutdown drains/flushes World work and saves the
  complete current-format player/environment checkpoint. Clean join precedes
  releasing borrowed content/transport; lifecycle failures retain needed resources.
- Remote multiplayer, a network protocol, administration GUI and Launcher Server
  management are deferred. Transport-independent local contracts establish the
  authority boundary without implementing these features.

**Observable completion:** Single-player actions pass through the integrated
authoritative Server, while the dedicated Server hosts the same simulation
headlessly, without Client windowing, rendering or GUI dependencies. Launching a
Server executable without moving actual gameplay authority does not qualify.

## Phase 8 — Client GUI & Menu Completion

The current Client has original MaaGE title/world-selection screens implemented
within its numeric GUI registry, with reconstructed widgets/font/dirt textures.
Default startup shows Title without starting simulation; Singleplayer supports
scrollable saved-world selection, double-click/keyboard play, creation with a
name, optional signed seed and Creative/Survival mode, rename, refresh and confirmed
permanent deletion. Corrupt/unsupported saves remain visible and cannot be played.
Catalog and administration use a bounded worker and reusable current-format World
storage APIs. Loading starts the existing integrated Server; Save and Quit flushes
and joins it before returning to Title. Explicit world CLI arguments retain direct
startup. Native text fields currently accept ASCII, matching the existing font.
The same frame limiter serves menus and gameplay. Multiplayer is disabled until
its transport is implemented. Runtime/UI verification remains programmer-owned.

Build upon Phase 2.75's existing numeric screen registration, GUI primitives and
screen stack; do not introduce a parallel UI/navigation system.

- Build upon the implemented title, world selection, creation, management and
  loading screens; extend progress reporting as startup stages expose it.
- Complete Options, including sound, video and controls, and finish the pause menu.
- Improve inventory and Creative interfaces, logical GUI scaling, focus and input
  routing, and correct Back/Escape navigation.
- Give screens a coherent Minecraft 1.7.10-inspired appearance and predictable
  navigation without disrupting loading, gameplay or existing settings.

**Observable completion:** From Client startup, a player can choose or create a
world, enter gameplay, adjust options, pause, and return through the menus
without command-line world-selection arguments. Rendered buttons alone are
not sufficient.

## Phase 9 — Visual Fidelity & Game Feel

The current source includes neutral biome-tinted grass/foliage, oriented torch
and log models, smooth light/AO, animated fluid surfaces, explicit material
passes, additive celestial sprites and perspective first-person presentation.
See [Visual Fidelity Implementation](visual-fidelity.md) for reference evidence,
build results, remaining limits and programmer comparison steps. These changes
are build-verified; runtime visual completion is not established.

Dedicate a milestone to consistent, performant gameplay presentation, using
Minecraft Java Edition 1.7.10 as the visual and mechanical reference while
preserving MaaGE's replaceable rendering architecture.

- Improve lighting fidelity, ambient occlusion, block rendering, foliage
  geometry/opacity, and water/lava appearance.
- Refine the sky, clouds, sun, moon, weather, particles and animations.
- Correct block breaking/placement feedback, held-item presentation, UI visual
  consistency, movement/camera feel and other presentation artifacts.
- Fix visible rendering bugs and inconsistencies with performance-conscious
  changes; preserve existing input and simulation semantics.

**Observable completion:** Typical movement and gameplay scenes look and feel
substantially closer to the intended 1.7.10 experience without major inconsistent
effects or visual regressions. Correct texture sampling alone is insufficient.

## Phase 10 — Core Gameplay Completion

The source now includes [Server-authoritative crafting](crafting.md): finalized
numeric shaped/shapeless recipes, player 2x2 and table 3x3 containers, transactional
output/remainders, bounded shift crafting, and return/drop closure. The initial
catalog supports the wooden/stone tool chain and recipes for registered higher
tiers. Smelting and full Survival progression remain deferred. Runtime/visual
completion requires programmer verification; the current `MGWORLD4` metadata
rejects previous development worlds.

Build a coherent Survival gameplay loop through Phase 7's **authoritative Server
runtime**, not Client-owned special cases.

- Add entity foundations, passive and hostile mobs, spawning/despawning, AI
  behavior and navigation.
- Implement combat, damage, drops/pickups, crafting and recipes, furnaces and
  smelting.
- Complete survival rules, equipment, durability, experience and progression
  foundations with the intended 1.7.10 behaviors.

**Observable completion:** Players can engage with mobs, gather and process
resources, craft equipment and experience meaningful survival progression through
the same integrated/dedicated simulation.

## Phase 11 — Advanced Gameplay Systems

Expand the shared authoritative simulation to cover advanced Minecraft 1.7.10
mechanics, using stable numeric identities and deterministic updates where required.

- Add redstone propagation and update rules, pistons, repeaters, comparators and
  related mechanisms; support advanced block entities.
- Add enchanting, advanced equipment, Nether and End dimensions, portals and
  dimension travel.
- Implement advanced structures, progression, bosses/endgame systems and
  remaining significant 1.7.10 mechanics and behavioral quirks.
- Parallelize independent workloads only where safe for simulation ordering
  and reproducibility.

**Observable completion:** Advanced mechanisms, dimension travel and endgame
progression work as integrated, persistent gameplay rather than isolated
infrastructure or Client-only demonstrations.

## Phase 12 — Networking & Multiplayer

Extend Phase 7's existing integrated/dedicated Server implementation to support
remote players; do not create a second gameplay engine.

- Introduce a versioned Client/Server network protocol with compatibility
  negotiation **independent of application SemVer**, plus remote transports.
- Support multiple connected players; replicate chunks, entities and shared
  world/player state, with input sequencing, prediction and reconciliation.
- Handle connection lifecycle, recovery, synchronization, multiplayer performance
  and safe concurrent Server scheduling.
- Complete dedicated Server configuration/administration and Launcher Server
  management/local hosting workflows while preserving a fully headless runtime.

**Observable completion:** Multiple remote Clients can play together with
consistent world and player state using the exact simulation already employed
for single-player. Compatibility and remote transport are additions to Phase 7,
not substitutes for its integrated authority boundary.
