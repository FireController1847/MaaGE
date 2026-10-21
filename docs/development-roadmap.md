# MaaGE Gameplay Roadmap

Minecraft: Java Edition **1.7.10** is the reference. Client **0.1.0-alpha.6**
implements the Phase 1 foundation, Phase 2 world milestone and Phase 2.5
multithreaded architecture, Phase 2.75 UI, Phase 3 gameplay, Phase 3.5 instance storage
and the Phase 4 environment foundation below. Compilation does not establish
runtime correctness or vanilla fidelity. Phases 5–6 remain future work.

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
  stone, surface/filler layers, bedrock and static ocean water at Y=63. Generator
  **2** uses the same terrain mathematics with persisted per-world sea-level and
  spawn-search policies; generator 1's rules never change. Heightmaps and initial
  lighting precede meshing. Generation has no mutable global PRNG and does not
  depend on request order, worker scheduling or render radius.
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
  emitted channel; current terrain blocks emit zero. Meshing combines neighboring
  light levels with temporary directional face shading. Lighting-only sections
  are published through controlled owner commits with stale-neighbor rejection.
- **Diagnostics:** startup logs world ID, signed seed, generator and radius.
  Periodic VERBOSE diagnostics report loaded/pending/ready columns, generated versus
  disk-loaded counts, mesh rebuilds and approximate frame time.

### Creation and Selection

Resources and a physical instance must already be prepared by the Launcher.
Direct startup uses its selected/default instance; `--instance=<uuid>` selects one
explicitly. `--data-directory=<absolute root>` overrides the global resource and
registry root. Client never authenticates or downloads Minecraft resources.

```sh
maage-client                         # Resume last world; create one if none exists
maage-client --new-world              # Unique world ID and automatic seed
maage-client --seed=-123456789         # Unique world ID with the specified seed
maage-client --create-world=f4a2c9b1-8e36-4f1c-91ac-7d02ee39d82c --seed=42 --world-name="Example World"
maage-client --world=f4a2c9b1-8e36-4f1c-91ac-7d02ee39d82c --render-distance=4
```

`--world=<id>` loads an existing world. Creating an existing ID fails instead of
replacing it. Supplying a seed for an existing world is rejected. IDs are bounded,
canonical UUIDs for new worlds; migrated legacy IDs remain loadable. Generated
UUIDs and automatic seeds use OS cryptographic randomness. Instance-local
`config/last-world` remembers the selection. Separate
selection and lifetime world locks prevent concurrent writers.

### Save Format and Recovery

Data lives beneath `instances/<instance-uuid>/worlds/<world-uuid>/`: `world.bin` metadata and
`chunks/<signed-x>_<signed-z>.chunk`. Metadata records ID/name, signed seed,
creation UTC, format/generator/blockset versions and player position/orientation.
Player state saves after the pool's clean-shutdown flush. An interrupted first
launch retains a pending-spawn flag; later interrupted launches resume the last
successfully saved player state.

Legacy chunk format **1**, generator **1**, blockset **1** remain readable with
frozen numeric identities. Chunk format **2** introduced namespaced block/state and
biome palettes local to each chunk, preserving expanded runtime ID registries;
Phase 4 format **3** retains those palettes and adds column simulation state.
Metadata schemas 1–4 retain generator-1 behavior; configured generator 2 uses
metadata schema **5** with sea-level and spawn-search parameters. Phase 4 metadata
schema **6** additionally persists clock/weather and supports generator 3. Headers carry
matching generator/blockset versions. All files use explicit little-endian encoding
and CRC32. Unsupported versions, lengths, coordinates, identities or checksums
fail without replacing corrupt originals. Explicit migrations remain necessary
for incompatible mappings or generation changes.

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

Generators 1/2 retain their original terrain without Phase 4 features; generator 3
adds the generation features described below. Phase 3 supplies inventory and
interaction, and Phase 4 supplies fluid updates and environmental presentation.
Water still uses an opaque tinted first texture frame without animation, swimming
or underwater presentation. Lighting lacks vanilla parity and ambient occlusion.
GPU positions remain floating-point world
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
  GPU uploads are budgeted by elapsed milliseconds per frame (adaptive by default
  or explicitly configured), and unchanged resources remain resident.
  Resource/world metadata opens during hidden startup; blocking flush,
  join and final player metadata happen after the render loop stops.
- **Diagnostics:** `--world-workers=<count>` configures the pool (0 is automatic). Startup reports its
  actual count. Five-second VERBOSE logs report queued/running jobs, accepted
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
- **F3:** Phase 4.5 refines this overlay into compact left/right columns with live
  gameplay observations and separately averaged performance measurements; see below.
- **Read-only telemetry:** World observations use owner-published snapshots. One
  brief scheduler snapshot copies bounded numeric states; no worker output reads,
  World lock, job wait, heap allocation or diagnostic disk I/O occurs per frame.

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
- **Persistence:** CRC-protected player metadata schemas 1–3 remain readable.
  Schema 4 writes namespaced stable item identities and expandable pickup
  records; configured generator 2 uses schema 5. Existing numeric item mapping
  1 retains its legacy seven-item interpretation. Chunk format 2 persists
  namespaced block/state/biome palettes while reading old format-1 chunks.
  Final player metadata writes only after rendering stops and World flush succeeds.
- **F3:** target IDs/face/position, selected item/slot, mode/flight, health/hunger,
  reach/break progress, mutation totals and dirty columns join existing scheduler
  diagnostics through bounded owner snapshots.

This is an independent 1.7.10-style foundation, not gameplay or visual parity.
Tools are defined by the API but none are registered yet, so bare-hand mining
cannot harvest stone/sandstone. Drops are simplified (Phase 4 adds cobblestone, but no gravel
flint chance or item-entity simulation). Hunger uses provisional walking/jump/
mining costs, 80-tick regeneration/starvation and a one-health starvation floor;
food, difficulty selection, armor, combat and crafting remain deferred. Respawn
has no persistent bed/spawn point or death drops. Held blocks are perspective UI
quads with fixed idle positioning, without an arm, equip animation or models.
Cracks use opaque precompositing rather than vanilla's dedicated blend pass.
Phase 4 extends content with shaped decoration and scheduled fluids below.

macOS ARM64 Debug/Release Client (native Metal) and portable headless Release
World/Server compilation passed. No application, test, benchmark, UI/runtime
validation or GitHub Actions run was performed. Windows/Linux input source
changes remain uncompiled locally; Wayland gameplay input remains unsupported.

## Phase 3.5 — Instance Storage & Shared Folder Mapping

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
`name`, `clientBuildId`, optional `createdUtc`, and `folders`. Unknown root fields,
including future modpack metadata, survive edits. A folder entry is one of:

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
Client association and mappings are read from manifests. Legacy Launcher schemas
1/2 create manifests before replacing the registry atomically; invalid manifests
fail without resetting configuration. Installed Client packages remain independent
and can serve several instances. Play validates the manifest/build pair on its
existing worker and passes `--instance=<uuid>` plus the shared data root. The
Client holds its own instance lease after Launcher exit.
Client metadata/descriptors declare numeric `instance_storage: 1`. Older packages
remain installed and referenced but are unavailable for Play until an instance-aware
Client is imported; the Launcher never starts an old Client that would ignore its
instance argument and write global worlds. No network protocol is implied.

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

Legacy global `worlds/` migrates automatically only for the sole configured
instance. With multiple instances, the Launcher reports migration pending and
preserves legacy data. Choose a destination explicitly while Clients are closed:

```sh
maage-client --instance=<destination-uuid> --migrate-legacy-worlds
```

Migration holds the legacy selection lock and every world writer lock, validates
metadata and refuses occupied destinations or conflicting last-world selections.
A CRC journal at `config/worlds-migration` records the destination and selection
before an exclusive atomic directory move. If relocation is unavailable, a bounded
copy into `.worlds-migration-<uuid>` verifies synced files byte for byte and checks
the whole tree before atomic promotion; original legacy files remain as backup.
The completion receipt is `config/legacy-worlds-migrated`. Journal recovery resumes
an interrupted atomic move or accepts an already promoted copy only if its source
and destination trees still match. Interrupted partial copies remain available for
manual cleanup/recovery; retries preserve originals and never overwrite worlds.
Existing IDs, seeds, generator versions, chunks and player data are retained;
new worlds use canonical UUIDs, while old non-UUID IDs remain a compatibility case.

An exclusive UUID-scoped instance lease currently permits one Client per instance
and blocks rename, deletion and remapping during use. Each world retains its own
lifetime writer lock. There is no lock across ordinary operations in different
instances; legacy migration alone serializes access to the old global collection.
The existing asynchronous chunk saves/revisions and post-render shutdown remain.
Server management can opt into the same storage library without UI/graphics;
Server hosting, mod loading and complete shared-folder settings remain deferred.

macOS ARM64 Debug/Release Launcher/Client and headless Release Instance/World/Server
builds passed. No applications, tests, benchmarks, runtime migrations, UI validation
or GitHub Actions were run. Windows/Linux implementations remain uncompiled here;
filesystem/link/remapping/migration behavior requires programmer runtime testing.

## Phase 4 — World Systems

Implemented foundation in Client **0.1.0-alpha.6** and portable `MaaGE::World`:

- **Generation:** new worlds default to generator **3**. `--generator-version=1|2|3`
  selects a creation policy; loads retain the recorded policy. Generators 1/2
  remain frozen. Eight numeric stages cover biome selection, terrain shaping,
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
- **Structures:** numeric, versioned regional placement generates underground
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
  observations select snow and deserts suppress precipitation. Metal combines static
  sky/block light channels with a frame sky factor, preserving emitted light at night
  without relighting. Existing draw submission accepts optional background/overlay
  batches for sky color, sun/moon movement/phases, bounded rain/snow and thunder flashes.
- **Persistence:** metadata **6** reads schemas 1–5 and retains policies/stable item
  identities, adding clock/weather to the final player checkpoint after successful
  World flush. Chunk **3** reads formats 1/2 and retains stable palettes, stage/feature
  counts, updates and block entities. Pending work serializes remaining two-word
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
frozen by generator 3; sea level and spawn search remain its per-world controls.
No jungle biome or surface lakes are implemented. Ore blocks drop themselves;
tree variants use base-log drops. Fluids omit vanilla slope search/exact update
order, swimming, buoyancy and lava damage. Plant support/leaf decay, snow
accumulation/melting and weather audio remain deferred. Fluid textures are opaque
first frames. Precipitation is a screen-space overlay gated by the player's sky/
climate observation, without world-depth particles or distant roof occlusion.
Clock/player checkpoints occur on clean shutdown; chunks save independently.

macOS ARM64 Debug/Release Client and headless World/Server builds passed. No applications, tests,
benchmarks, runtime/visual validation or GitHub Actions ran. Windows/Linux and
runtime shader compilation remain unverified; only Metal implements gameplay drawing.

## Phase 4.5 — Client Resilience, Operation Status and Debug Refinement

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
  remain. TRACE/DEBUG/WARN spellings remain compatibility aliases. Structured
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

## Phase 5 — Full Classic Gameplay

Planned: entities/mobs, AI, combat, crafting/furnaces, redstone, enchanting,
Nether/End, progression and faithful Minecraft 1.7.10 mechanics.

## Phase 6 — Client/Server Architecture

Planned: authoritative dedicated Server simulation, a versioned network protocol,
compatibility rules, multiplayer, player/world synchronization and Launcher server
hosting integration. Portable world/simulation stays independent of graphical
Client ownership. Optional Server administration UI will be a frontend over the
same simulation, rather than a second implementation.
