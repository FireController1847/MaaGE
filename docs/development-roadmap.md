# MaaGE Gameplay Roadmap

Minecraft: Java Edition **1.7.10** is the reference. Client **0.1.0-alpha.4**
implements the Phase 1 foundation, Phase 2 world milestone and Phase 2.5
multithreaded architecture, Phase 2.75 UI and Phase 3 gameplay foundations below.
Compilation does not establish runtime correctness or vanilla fidelity. Phases
4–6 remain future work; Phase 3 limitations are explicit below.

## Phase 1 — Rendering and Movement

Implemented foundation: standalone `maage-client`, version queries, native Window
and Input, reconstructed PNG resources, frozen numeric block/state registration,
indexed visible-face cube meshing, native Metal textured rendering, first-person
camera, fixed 20 TPS movement, jumping/gravity and swept AABB collision. Phase 2
extends these same modules; the nine-section static demonstration is superseded
by the normal generated world.

WASD walks and Space jumps. Phase 2.75 adds Escape pause-menu navigation and F3.
Focus loss releases capture and clears held keys. Camera position is interpolated;
long frames cap simulation catch-up. Inactive/hidden windows use event waits.
The player bounds, eye height, acceleration/drag/jump values and camera tuning
remain provisional pending programmer fidelity verification.

**Metal remains the only textured-mesh renderer.** Vulkan, OpenGL and Software
preserve their existing clear/presentation paths and reject unsupported gameplay
commands. Explicit renderer requests never switch families. Cocoa, Win32 and X11
have native gameplay input; Wayland gameplay capture remains unsupported. X11's
warp-relative fallback retains native pointer acceleration.

## Phase 2 — World Architecture, Generation, Streaming, Lighting and Persistence

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
- **IDs:** terrain types retain legacy IDs air 0, stone 1, grass 2, dirt 3,
  bedrock 7, water 9, sand 12, gravel 13 and sandstone 24. State IDs are a separate
  domain; biome grass states 256–261 reference grass with numeric metadata 1–6.
  All texture references are precomputed. Name parsing occurs only at argument,
  registration or save/import boundaries, never in generation, collision,
  streaming, lighting, meshing or draw loops.
- **Seeded generation:** signed 64-bit seeds are represented as two masked 32-bit
  words in C90. Generator **1** hashes seed bits, stable coordinates and numeric
  stages into gradient Perlin/fractal noise. Climate sampling selects numeric
  ocean/plains/desert/extreme-hills/forest/taiga biomes; blended heights produce
  stone, surface/filler layers, bedrock and static ocean water at Y=63. Heightmaps
  and initial lighting precede meshing. A bounded deterministic surface search
  selects the initial spawn. Generation has no mutable global PRNG and does not
  depend on request order, worker scheduling or render radius.
- **Streaming:** bounded asynchronous jobs handle loading/generation, lighting,
  CPU meshing and saving; the owner commits results and evicts columns. The
  bounded desired grid prioritizes nearby and
  view-facing columns. Render radius defaults to **3**, configurable from 1–8;
  one extra column provides a lighting/meshing halo. The cache is capped at 512
  columns. Fully constructed immutable columns are published by the owner. Missing/loading
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
  The graphics thread uploads at most one changed column per frame and batches
  draws into one clear/depth/presentation pass. Actual reconstructed 1.7.10 grass,
  dirt, stone, bedrock, water, sand, gravel and sandstone PNGs are loaded once.
  Real grass/foliage colormaps populate biome definitions; grass atlas variants
  and sandstone top/bottom/side references are resolved before gameplay.
- **Lighting:** packed sky-high/block-low 0–15 levels. Direct sky, opaque occlusion,
  water attenuation and bounded six-neighbor propagation run on private worker snapshots.
  Both channels import neighboring column light and converge after arrivals,
  removals or changed boundary light. Numeric block emission properties seed the
  emitted channel; current terrain blocks emit zero. Meshing combines neighboring
  light levels with temporary directional face shading. Lighting-only sections
  are published through controlled owner commits with stale-neighbor rejection.
- **Diagnostics:** startup logs world ID, signed seed, generator and radius.
  Periodic DEBUG diagnostics report loaded/pending/ready columns, generated versus
  disk-loaded counts, mesh rebuilds and approximate frame time.

### Creation and Selection

Resources must already be prepared by the Launcher. Direct startup defaults to
MaaGE's platform application-data directory; `--data-directory=<absolute root>`
overrides it. Client never authenticates or downloads Minecraft resources.

```sh
maage-client                         # Resume last world; create one if none exists
maage-client --new-world              # Unique world ID and automatic seed
maage-client --seed=-123456789         # Unique world ID with the specified seed
maage-client --create-world=example --seed=42 --world-name="Example World"
maage-client --world=example --render-distance=4
```

`--world=<id>` loads an existing world. Creating an existing ID fails instead of
replacing it. Supplying a seed for an existing world is rejected. IDs are bounded,
path-safe lowercase ASCII identifiers; generated IDs and automatic seeds use OS
cryptographic randomness. `config/last-world` remembers the selection. Separate
selection and lifetime world locks prevent concurrent writers.

### Save Format and Recovery

Data lives beneath `worlds/<id>/`: `world.bin` metadata and
`chunks/<signed-x>_<signed-z>.chunk`. Metadata records ID/name, signed seed,
creation UTC, format/generator/blockset versions and player position/orientation.
Player state saves after the pool's clean-shutdown flush. An interrupted first
launch retains a pending-spawn flag; later interrupted launches resume the last
successfully saved player state.

Format **1**, generator **1**, blockset **1** use explicit little-endian encoding
and CRC32. Sparse chunk files store a section mask, numeric 16-bit state IDs,
packed light, numeric biomes and heightmaps. Runtime IDs remain independent of
this storage width. The exact state/type/metadata mapping above is frozen by the
blockset version; changing IDs requires an explicit migration. Unsupported versions,
coordinates, lengths, mappings, heightmaps or checksums fail clearly. Corrupt
existing files are preserved, never silently regenerated or overwritten.

Writes sync complete temporary files before atomic promotion. Abandoned `.tmp`
files are ignored on read and removed before another write. Failed writes retain
the previous save. This provides interrupted-write recovery; directory-entry
power-loss durability and automatic recovery from existing file corruption are
not claimed. Restore damaged files explicitly from a backup.

### Compatibility and Remaining Limits

Generator 1 is an original **approximation**, with coarse climate cells, blended
height fields and seeded gradient noise. It does not reproduce Java Random,
GenLayer, vanilla three-dimensional density, exact biome distribution or
seed-for-seed 1.7.10 terrain. The generator version is persisted and unsupported
versions are rejected rather than silently changing terrain.

Trees, caves, ores, structures, decorations, fluids, weather, inventories, block
interaction and multiplayer are deferred. Forest/taiga currently describe terrain
and color parameters without trees or snow. Water uses an opaque tinted first
texture frame; there is no animation, fluid simulation, swimming or underwater
presentation. Lighting is a basic model, without vanilla lighting parity,
ambient occlusion or day/night. GPU positions remain floating-point world
coordinates, so precision decreases far from the origin. World coordinates are
bounded to the classic-scale ±30-million range.

Native threading, storage and randomness stay behind Core APIs. Generic storage
and headless World persistence require no Launcher UI, HTTP, JSON, OpenSSL,
archive, window or graphics dependencies. Optional asset hashing remains a
separate acquisition dependency. The existing Server can consume these same
portable data/simulation modules later; networking and authoritative server
integration are not implemented in this phase.

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
  are independent of worker count: World task capacity scales with the pool
  within its finite column-cache bound. Threads and scheduler arrays are created
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
  columns. Generation retains generator 1's seed/coordinate/stage-derived random
  state and terrain algorithms, so scheduling never selects different terrain.
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
  registry and store remain alive. Existing save versions and corruption recovery
  rules remain unchanged.
- **Client phases:** input/native events, completion commits, ordered fixed
  20 TPS player movement, rendering orchestration and GPU operations stay on the
  owning thread. No other meaningful independent gameplay system exists yet;
  entity frameworks or mechanics have not been added to demonstrate parallelism.
  GPU uploads remain capped at one changed column per frame and retain unchanged
  resources. Resource/world metadata opens during hidden startup; blocking flush,
  join and final player metadata happen after the render loop stops.
- **Diagnostics:** `--world-workers=<count>` configures the pool (0 is automatic). Startup reports its
  actual count. Five-second DEBUG logs report queued/running jobs, accepted
  generation, lighting, meshing and I/O work, lifetime completed/cancelled/stale
  counts, and approximate mean scheduling latency. No per-task production logging
  or runtime benchmark is required.

Synchronization remains at scheduler ownership handoffs and filesystem writer
locks, where shared queue state and exclusive save ownership require it. Worker
tasks have no graphics or native UI dependencies, allowing future Server reuse.
Main-thread startup/shutdown metadata I/O is a lifecycle boundary; no chunk
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
  open an Options placeholder or exit through normal Client shutdown. Options
  demonstrates Back navigation without adding a settings system. Mouse controls
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
  followed by one presentation. **Metal currently implements UI and textured
  gameplay.** Vulkan/OpenGL/Software retain existing clear/presentation behavior
  and explicitly reject unsupported UI/gameplay commands; parity is deferred.
- **F3 gameplay data:** the header uses the generated Client version and Git
  metadata. Measured FPS/frame time and simulation TPS/tick cost accompany world
  ID/seed, player XYZ, block/chunk coordinates, facing/yaw/pitch, numeric biome
  and diagnostic name, current available sky/block light, render distance,
  loaded/pending columns, resident sections and rendered mesh/vertex/triangle
  counts. Resident sections are not a frustum-visibility estimate. Targeted-block
  information is N/A because raycasting has not been implemented.
- **F3 system data:** OS/architecture, renderer/backend and available GPU name,
  Client build type, available process resident/peak memory, logical processors,
  worker pool size, genuinely running generation workers, queued/running
  generation, meshing, lighting and I/O, and completed/cancelled/stale counters.
  Unavailable values are omitted or marked N/A rather than inferred.
- **Read-only telemetry:** visible F3 World/job, text and memory snapshots refresh
  every rendered frame. Rolling frame/tick timing statistics may smooth
  measurements without throttling live state. A single brief scheduler
  lock copies bounded context identities and numeric states, then World groups
  immutable task kinds outside that lock. No worker output fields are read.
  World observation reads owner-published biome/light/section data, with light
  unavailable until current lighting completes. No World lock, job wait, GUI
  dependency or diagnostic file I/O is introduced into the render loop.

The ASCII font, compact layout, scaling, capture/navigation, pause behavior and
Metal presentation require programmer runtime/visual verification. Non-macOS
source paths remain uncompiled locally. Phase 3 extends this UI with inventory,
targeting and basic game-mode controls. Crafting, a full settings system and
multiplayer menus remain future phases.

## Phase 3 — Player Interaction, Inventory and Core Gameplay

Implemented foundations extend the existing World, scheduler, input and Client UI:

- **Targeting:** allocation-free numeric 3D DDA returns block/state IDs, face,
  position, distance and selection AABB, including negative coordinates. Missing
  columns stop targeting. Default reach is 5 blocks in Creative and 4.5 in Survival;
  `--interaction-range=<positive blocks>` optionally overrides it, up to 64.
  Twelve depth-tested black edges outline the target. Selection and collision use
  separate registered shapes; current content remains full cubes.
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
  state orientation. Ten reconstructed damage textures are precomposited into
  the atlas at initialization and drawn through the existing mesh pipeline.
- **Items and inventory:** a finalized direct-index item registry supplies seven
  usable block items (stone, grass, dirt, bedrock, sand, gravel and sandstone),
  stack limits, block/icon states, display names and extensible tool properties.
  Thirty-six fixed stack slots include the nine-slot hotbar, plus a cursor stack.
  Left click moves/merges/swaps; right click splits or places one. Numeric screen
  3 provides the classic storage layout and a paged Creative catalogue, reusing
  reconstructed GUI textures and the prepared block atlas.
- **Controls and HUD:** mouse buttons break/place; 1–9 and wheel select hotbar
  slots; E opens/closes inventory; Escape pops the screen stack. Existing GUI
  scaling, capture/focus behavior, crosshair and F3 remain. The hotbar shows actual
  selection/icons/counts; Survival displays hearts and hunger. A shaded three-face
  held-block presentation with a basic swing uses the existing UI batch.
- **Game modes and vitals:** new worlds start Creative with registered block
  stacks. Options or `--gamemode=creative|survival` selects mode; new Survival
  worlds begin empty. Double-tap Space toggles collision-aware Creative flight;
  Space/Shift ascends/descends. Survival tracks health, hunger, saturation,
  exhaustion, fall damage, slow regeneration and starvation. Death opens Pause
  with Respawn, retaining inventory at the session-start position.
- **Drops:** ordinary successful harvests enter inventory immediately. Overflow
  uses at most 64 numeric position/stack records and is collected within two
  blocks; a full pickup pool prevents another overflowing break. These records
  persist but have no rendered item entities, movement or despawn yet.
- **Persistence:** CRC-protected atomic player metadata schema 2 stores inventory,
  cursor, selected slot, mode, vitals, flight, fall distance, pickup records and
  position/rotation. Schema-1 worlds load with empty Creative inventory and full
  vitals, retaining seed/chunks. Numeric item mapping 1 fixes the seven current
  IDs with metadata/damage zero; unknown mappings fail explicitly. Chunk format
  and block/biome mappings remain 1. Final player metadata writes after rendering
  stops and World flush succeeds; no player-file I/O runs in the frame loop.
- **F3:** target IDs/face/position, selected item/slot, mode/flight, health/hunger,
  reach/break progress, mutation totals and dirty columns join existing scheduler
  diagnostics through bounded owner snapshots.

This is an independent 1.7.10-style foundation, not gameplay or visual parity.
Tools are defined by the API but none are registered yet, so bare-hand mining
cannot harvest stone/sandstone. Drops are simplified (no cobblestone item, gravel
flint chance or item-entity simulation). Hunger uses provisional walking/jump/
mining costs, 80-tick regeneration/starvation and a one-health starvation floor;
food, difficulty selection, armor, combat and crafting remain deferred. Respawn
has no persistent bed/spawn point or death drops. Held blocks are perspective UI
quads with fixed idle positioning, without an arm, equip animation or models.
Cracks use opaque precompositing rather than vanilla's dedicated blend pass.
Full-cube placement is the only current content behavior; fluids do not simulate.

macOS ARM64 Debug/Release Client (native Metal) and portable headless Release
World/Server compilation passed. No application, test, benchmark, UI/runtime
validation or GitHub Actions run was performed. Windows/Linux input source
changes remain uncompiled locally; Wayland gameplay input remains unsupported.

## Phase 4 — World Systems

Planned: caves/structures, ores/vegetation, fluids, day/night, weather, block
entities, block updates/tick scheduling, more complete block behavior and world
simulation fidelity.

## Phase 5 — Full Classic Gameplay

Planned: entities/mobs, AI, combat, crafting/furnaces, redstone, enchanting,
Nether/End, progression and faithful Minecraft 1.7.10 mechanics.

## Phase 6 — Client/Server Architecture

Planned: authoritative dedicated Server simulation, a versioned network protocol,
compatibility rules, multiplayer, player/world synchronization and Launcher server
hosting integration. Portable world/simulation stays independent of graphical
Client ownership. Optional Server administration UI will be a frontend over the
same simulation, rather than a second implementation.
