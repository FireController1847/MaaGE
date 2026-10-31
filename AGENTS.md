> **PRIMARY PROJECT CONTEXT** — Read this file before work. It is the exclusive
> home for persistent agent instructions and project requirements. Keep it concise,
> current and actionable; do not create alternative instruction or memory files.

# MaaGE Project Contract

MaaGE (Minecraft as a Game Engine) is an independently developed modular native
voxel platform in C, using Minecraft Java Edition 1.7.10 as loose gameplay/content
inspiration. Performance, extensibility and traditional modpacks guide the design;
Java architecture, seed parity, binary/save interoperability, fixed numeric IDs,
Java/Forge execution and compatibility with existing Java mods are not requirements.
Roadmaps describe direction, not authorization or evidence of runtime correctness.

## Working Rules and Authorization

- Inspect current source, applicable instructions and Git state first. Preserve
  unrelated changes. Make focused changes, reuse existing interfaces and avoid
  speculative abstractions, duplicate systems, scaffolding and dependencies.
- **Do not configure CMake, compile or link without explicit authorization for the
  current task.** Static source/diff/dependency inspection and formatting are allowed.
  Prior build authorization does not carry into unrelated tasks.
- Do not create or run authored tests, frameworks, fixtures/mocks, harnesses,
  test-only targets/presets/scripts, benchmarks or manual test checklists without
  specific authorization. Do not launch applications, perform runtime/visual/audio
  validation, authenticate/download assets or trigger Actions without authorization.
  The programmer owns runtime verification. Production validation/invariants remain required.
- When builds are authorized, build directly affected targets/shared components and
  primary backends. Build secondary backends only when affected; report actual
  configurations, failures and unverified platforms. Compilation proves no runtime outcome.
  CI remains manually triggered (`workflow_dispatch`) with read-only permissions.
- Preserve the existing Git author/committer identity. Every created or amended
  commit must include `Co-authored-by: Codex <noreply@openai.com>`.
- Report changes, checks actually performed and material limits concisely. Do not
  create per-task implementation-history reports; final responses and Git preserve that history.

## Independent Implementation, Formatting and Documentation

- Independently implement MaaGE algorithms and architecture. External implementations
  may inform observable behavior/expected outcomes; never copy or mechanically
  translate proprietary code. Comments/docs describe MaaGE algorithms, invariants
  and design rather than unnecessary external class/method/source references.
  Preserve accurate provenance, required attribution, legal notices and necessary
  product/API/asset references. Flag substantial suspected copied material separately.
- `.editorconfig` and `.clang-format` are authoritative for new/modified first-party
  native source: four spaces, no indentation tabs, attached opening braces, spaces
  around binary/assignment/ternary operators, after commas and C casts; no arbitrary
  line limit. Prefer one physical line for each straightforward statement, declaration,
  function signature, or function call, without any arbitrary length limit. Existing
  unnecessary line breaks must be actively removed rather than merely preserved by
  clang-format. Multiline formatting is reserved for genuine logical structure or
  improved readability. Short guards/inline returns are encouraged; simple conditionals need no braces.
  Separate unrelated statements and logical phases, not every guard/assignment.
- clang-format prints a cast followed by unary minus as `(size_t) -1`. The
  programmer's explicit sentinel style is `(size_t) - 1`; retain that whitespace
  exception manually. Do not build a custom formatter. Formatter use must not change
  tokens, macros, literals, language standards or semantics. Do not sort includes
  or reflow comments mechanically. Generated files, the CMake placeholder template and vendored `stb_vorbis.c`
  are excluded.
- Retain UTF-8, final newlines and existing LF endings for portable tools/scripts;
  do not apply repository-wide CRLF churn. Follow native resource/shader conventions
  where clang-format does not support the language.
- Maintain a lean current-state repository. Git preserves historical changes; docs
  explain active architecture, usage, requirements and future work. Remove expired
  handoffs, consolidate duplicate explanations and update existing module docs when
  contracts change. Add a document only for a genuinely distinct technical subject.
- `README.md` is human-facing and **must not be edited without explicit authorization
  naming that file**. Preserve its opening AI-routing comment, development disclosure
  and legal notices. No visible agent instructions, design-goals section or feature cards;
  use MLA heading capitalization when authorized to edit it.
- Source comments explain local invariants; public headers explain API ownership/use;
  module docs explain current systems; [the roadmap](docs/development-roadmap.md)
  distinguishes implemented, partial and future capabilities. Keep persistent agent
  policy here. Consolidate contradictions rather than appending historical guidance.

## Predevelopment and Portability

- Backward compatibility is not required. Prior worlds, generators, schemas,
  settings, assets, instances, commands and APIs are disposable. Keep one current
  implementation/encoding; reject incompatible bytes. Do not add old readers,
  migration/conversion, aliases, deprecated wrappers, fallback writers or backup
  machinery. Never silently reset/delete authoritative corrupt or incompatible data.
  Explain when development data needs recreation; remove superseded code/instructions.
- Independent Launcher/Client/Server SemVer alpha sequences and Git hashes describe
  development, not compatibility. Centralize versions in `CMakeVersions.cmake`;
  bump for meaningful application changes, not merely commits. Shared generated C90
  metadata refreshes from HEAD, including SemVer-safe abbreviated hashes.
- Portable code/public C interfaces use explicit ISO C90 without extensions.
  Isolate OS/compiler/graphics exceptions in target-local adapters; enable Objective-C
  only for macOS targets that need it. Avoid unnecessary C99/C11/C++; do not sacrifice
  required modern functionality. Preserve alternative toolchains where feasible.
  Never assume pointer/integer widths or use build-host assumptions for target checks.

## Modules and Authority

- `include/` contains public contracts; `src/` implementations. Core supplies reusable
  platform foundations; World owns numeric content/chunks, generation, collision,
  simulation utilities and CPU meshing; Session transports numeric copied/owned data;
  Server owns the authoritative runtime; applications own their frontends/lifecycle.
- Keep contracts minimal, backend independent and explicit about ownership. Use opaque
  handles/versioned interfaces where appropriate; no circular dependencies or generic
  infrastructure depending on applications. Evaluate `lib-core-c` before reuse, but
  do not make it a required dependency without programmer approval.
- Launcher never depends on Client/Server implementation internals. It starts the
  exact independently versioned Client as a separate process whose lifetime survives
  Launcher exit. Client and Server share definitions/portable services, never each
  other's executable or application-private implementation.
- One reusable Server runtime owns World, player, gameplay, environment and persistence
  on its owner thread for integrated and dedicated sessions. Gameplay is Server-owned
  from the outset. Remote networking is separate future work, not a second simulation.
  Headless Server requires no Client, GUI, display, windowing, audio device or graphics.
  A future administration GUI is a frontend inside that same executable; defer it.
- Client owns input, UI, camera, audio and visual presentation. It consumes copied
  snapshots/events, an entity mailbox and revisioned block packets.
  Its presentation-only World replica reuses shared chunks/light/meshing/Jobs and cannot
  generate, simulate or save. It never reads or mutates the live Server World.

## Status, Tracing and Logging

- `maage/status.h` defines universal verified unsigned 32-bit `0xSSCCOODD` outcomes
  (Status, Context, Operation, Detail). Serialize SS/CC/OO/DD explicitly; severity
  comes from the top two status bits, success context is zero, unassigned bytes unknown.
  Do not add competing error enums, boolean wrappers or logging-only outcomes.
- Meaningful operations participate in success/NOOP, warning/partial, error and fatal
  reporting. Intentional exceptions include pure math/predicates, direct getters,
  native ABI callbacks/action outputs and invariant synchronization. Immediate GUI
  drawing accumulates one canonical batch result; native exits convert results explicitly.
- Preserve originating results and task-owned bounded causal traces. Jobs copy traces
  by value through existing handoffs; never borrow stack data, use global last-error,
  add trace locks/unbounded histories, allocate per call or format successful hot paths.
  Callback outcomes survive cancellation; stale and cancelled dispositions differ.
  Recovery has its own result; reset after handling an occurrence, before unrelated work.
- Central Log trace events put the original/root status, actual code/context and useful
  message first, then consistently indented recorded propagation frames in causal order.
  Include available source/operation/native causes and explicit omitted/recovery details;
  never invent function names or native stack frames. Hold the log lock for the entire
  multiline event; filter before formatting. Preserve severity, sinks and ordinary messages.
- Log once at the handling boundary through named `maage.*` loggers. Severity and
  verbosity are independent. Levels are exactly VERBOSE/INFO/WARNING/ERROR/FATAL;
  defaults are Debug VERBOSE, otherwise INFO. Overrides inherit at dot boundaries;
  precedence is defaults → optional INI → explicit `--log-*`. Pad after closing brackets.
- Logging stays synchronous, thread-safe, bounded, C90 and headless-safe. FATAL does
  not exit itself. Flush each record; keep captured stdout/stderr unbuffered. Files
  permit external read/write/rename (Windows share read/write/delete); no exclusive file lock.
  Keep scheduler/storage/use locks independent of log sharing.
- Launcher logs use platform data-root `logs/`; Client combines all levels and native
  stdout/stderr in its instance `logs/client.log`, including Launcher startup capture,
  without duplicate console/capture files. Client adopts it after the instance lease;
  Server retains console. Overrides remain supported, with early init/clean shutdown.
- Keep the latest plain log. Close/reinit GZip-copies nonempty sessions under local
  close timestamps, suffixing collisions without overwriting. Before a new session,
  preserve unarchived output and reuse identical archives. Stage/sync publication;
  failures retain plain files. Core GZip privately uses zlib >=1.2.3, including headless.
- Optional visual failures retain usable presentation via generated nearest-sampled
  magenta/black tiles, minimal font/default tint or omitted decoration. Cache failures
  per immutable resource generation; explicit destroy/reload retries. Brief deduplicated
  notices link important results to the instance log. FATAL is for unsafe continuation
  (critical allocation/invariants, required registries, corrupt authority or no renderer);
  preserve files and clean up normally, never conceal programming corruption.

## Numeric Identity and Content

- Block types, states, items and biomes are distinct numeric domains. Finalize immutable
  registries before gameplay; ID-to-definition access is deterministic direct O(1).
  Unknown/unregistered IDs fail explicitly rather than substituting unrelated blocks.
- Resolve external namespaced names at registration/import/I/O boundaries only. Name
  hashing is collision-checked expected O(1), not worst-case O(1). No identifier strings,
  name comparisons/lookups, paths, decoding or per-block/face allocation in gameplay,
  collision, generation, lighting, meshing or rendering loops. Resolve atlas UVs first.
- States reference numeric types/properties; items have their own registry and portable
  fixed-size stacks carrying count, metadata and damage. Display names are presentation.
  Content/recipes are growable without arbitrary registry-count ceilings.

## World, Jobs and Persistence

- Columns are 16x16 with sixteen sparse 16x16x16 sections, Y=0..255. Negative X/Z use
  floor division/positive modulo. Coordinate hashing is expected O(1). Dimensions may
  change deliberately; current seeds use C90-compatible signed 64-bit word pairs.
- Keep one deterministic current generator, independent of job order. Ordered terrain,
  cave, ore, surface, vegetation and structure stages own detached columns without
  neighbor waits. Generation policy contains only sea level and spawn radius/step
  (defaults 63 and 4096/16), shared by Client/Server; no historical generator selectors.
- Parallelize independent heavy CPU work through reusable bounded Jobs pools, never
  threads per object. Automatic workers are logical CPUs minus one, minimum one;
  failed detection uses eight directly. Positive explicit counts have no arbitrary
  ceiling; allocation/OS failures are explicit. Bound accepted contexts, snapshots
  and queued/running work independently; cache/job capacities are configurable and
  defaults derive from footprint/pool size, not fixed hard ceilings.
- Only the owner changes membership, published columns, gameplay or result commits.
  Workers own outputs and borrow immutable registries/snapshots until completion.
  No global World lock, frame barrier, render-loop job wait or lock around generation,
  filesystem I/O or GPU work. Brief scheduler/token/completion handoffs are allowed.
  Poll commits/schedule bounded near-player work; revisions reject obsolete jobs/uploads.
  Unload invalidates requests; cancellation is cooperative. Preserve fixed-step ordering.
- Server gameplay light and Client visual light use one numerical solver with separate
  state/schedules. Transfer initial blocks/biomes, then FIFO deltas against acknowledged
  block revisions, never light arrays or Server meshes. Dirty-cell transfer masks and
  backpressure retain unsent states/bases. Ordered interest removals release replica
  baselines only after all earlier packets; account for bounded in-flight residency. Expected-state prediction uses immediate bounded cell geometry; rejection
  rebuilds effective state. Independent light jobs own one output column and immutable
  facing boundary inputs, with dirty-cell removal/addition and cached direct-sky shafts.
  Rebase light onto newer blocks, retain pending work and requeue changed boundaries;
  separate geometry validity from light refinement and invalidate diagonal AO meshes.
  Keep previous GPU presentation until replacements arrive; local patches cover edits.
- Saves use coherent immutable snapshots asynchronously, one writer per column file.
  Clear dirty only if the saved revision remains current; never evict unsaved dirty data.
  Shutdown cancels obsolete CPU work, drains/joins Jobs and flushes outside rendering;
  borrowed stores/registries/snapshots outlive all tasks. Save final player metadata
  only after successful World flush. Failed joins retain resources still borrowed.
- Missing/loading terrain blocks collision/targeting; never treat it as free air or
  teleport the player. Owner 20 TPS advances environment and numeric scheduled updates,
  respecting pause. Deduplicate column queues, persist remaining delays/block entities,
  preserve geometry epochs for auxiliary-only edits and merge current auxiliary state
  into light completions. Static sky/block levels remain 0..15; daylight scales sky only.
- Random ticks belong to active owner phases, separate from scheduled fluids/support.
  `maage_world_config_init` defaults speed 3, explicit zero disables; setters validate
  aggregate work. Simulation square defaults to radius 8, independent of rendering/cache.
  Eligibility counts and immutable callbacks avoid sampling scans. Coordinate, behavior
  and weather RNG streams/checkpoints remain separate and never reseed on resume.
  Saplings retain species/stage metadata and grow through shared tree features with
  complete detached placement preflight; absent terrain defers growth.
  Preserve numeric leaf species/persistent/check flags, bounded connected log support,
  unavailable-neighborhood deferral and capacity-preflighted drops; no immediate leaf
  deletion on log removal. Details: [random ticks](docs/random-block-ticks.md).
- Support callbacks serve placement and persistent deduplicated neighbor checks. Mutations
  preflight work; owner processing is iterative/bounded and never discards queued cascades.
  Missing support/light defers; import catches unloaded edits. Ordinary grass blocks
  never acquire plant support requirements. Preserve each block's distinct rules/drops.
- Worlds live only under `instances/<uuid>/worlds/<uuid>/`, never shared resources/global
  worlds. Canonical UUIDs identify them. `MGWORLD8` metadata and `MGCHNK5` chunks are the
  sole encodings; private identifiers reject prior data. Use named item identities and
  chunk-local named biome/block palettes plus metadata, explicit lengths/coordinates/
  identities/CRC validation, single writers and atomic replacement. Store open borrows
  finalized items; save writes one complete player/entity/environment/RNG checkpoint.

## Gameplay and Client Contracts

- Owner expected-state mutations publish immutable columns, invalidate light/neighbor
  meshes and reject stale work. Mining/placement/vitals/entities run at 20 TPS. Shared
  shape-aware numeric DDA stops at unavailable terrain. Definitions resolve hardness,
  tool/harvest/drop/selection/collision/placement metadata before gameplay.
- Item entities have distinct monotonic instance IDs, typed records, swept collision,
  velocity, age and pickup delay. Missing terrain freezes retained entities. One World
  checkpoint owns active/dormant entities independently of column saves. Reserve drops
  before removal; never give mined drops directly to inventory or discard quantities.
  Client caches item geometry and presents confirmed pickup batches for 0.15s, without
  a magnetic gameplay force. See [items](docs/item-entities.md).
- One finalized numeric recipe registry serves temporary 2x2/3x3 containers and one
  inventory cursor. Matching allocates nothing. Server validates identity/result revision,
  health, loaded table and range. Preflight complete outputs/remainders; bound shift
  crafting. Closure returns inventory then reserved entity overflow; checkpoint only
  unreturned inputs on allocation failure, never an open table/derived result. No table
  storage, smelting or recipe book is implied. See [crafting](docs/crafting.md).
- Sprint eligibility/tap timers are Server-owned; Player owns movement multiplier,
  jump impulse and collision cancellation. Dynamic sprint/world FOV stays independent
  of saved base and fixed hand projection; duplicate snapshots do not restart smoothing.
  Food use is owner-timed with numeric item metadata and copied progress for Client poses;
  current respawn retains inventory at session spawn.
- Client screens/layout/input live under `src/client/ui/`, not controller `frontend.c`.
  Controller owns catalog Jobs, lifecycle and Options persistence. Reuse one numeric
  registry, stack, primitive/atlas/input system; no native widget or second game UI.
  [UI contracts](docs/gameplay-parity.md) list current IDs/layouts and modal behavior.
- Default startup shows Title; catalog/admin work uses bounded Jobs and current storage.
  Save/Quit joins/flushes before returning. Explicit world/creation CLI bypasses Title.
  Startup-only Mojang splash uses the verified original 256x256 PNG on white before
  reveal; pump events during initialization, no minimum delay. Missing art logs/uses white.
  No Client asset acquisition or packaged Mojang artwork.
- Per-instance atomic `config/options.ini` stores numeric FPS, VSync, GUI scale, Leaves, Biome Blend,
  window mode and volume settings. Game mode is world data. New FPS defaults to 240;
  zero is Unlimited with resource/display backpressure. Carry monotonic pacing deadlines,
  rebase stalls/cap changes, use native whole-ms waits and bounded sub-ms final waits.
  Preserve explicit saved preferences; never round each remainder up to 1 ms.
- Save options on exit, completed mode transitions/F11 and shutdown, never slider frames
  or pending transitions. Exactly Windowed, Borderless and native Fullscreen exist.
  F11 toggles Windowed/last non-windowed (default Borderless). macOS Borderless stays in
  its desktop Space; Fullscreen enters an AppKit Space, never exclusive display capture.
  Honor notch-safe UI bounds; world may use drawable pixels. Client's standalone Mach-O
  Info.plist disables notch compatibility mode. Reconcile observed mode, geometry/capture
  and saved preference; unsupported operations report explicit Status.
- Pause/Options stop gameplay and music but keep safe streaming/events alive; inventory/
  crafting release capture while simulation/music continue. HUD/F3 are nonmodal. Flush
  local activation audio before blocking world teardown. Ordinary jumps, landings and
  slot changes are silent; sound events distinguish steps/mining/destruction/placement.
  Thunder requires lightning; preserve bounded expiry and epoch/sequence rejection.
  Ambient display sampling belongs to the Client replica on a bounded local cadence;
  cave effects require calculated darkness, while gameplay sounds stay Server-owned.
- Register all music IDs; decode/upload only the selected track when starting and release
  after stop. Decoded-memory bounds must not limit registration or masquerade as NOT_FOUND.
- F5 transiently cycles First/Back/Front on captured press edges. Share interpolated feet,
  skin cuboids/UVs/poses and a four-block obstruction-limited boom. Targeting/audio remain
  at the authoritative eye. Client replica 11x11x11 camera samples update on cell/geometry-epoch
  changes, including predictions; unknown terrain obstructs. Borrow cached part/
  item meshes, clear instances before destruction and never rebuild on F5. One Client
  six-tick swing timeline, restart eligible at three, serves both perspectives without
  attack-echo restarts. See [perspectives](docs/third-person.md).
- UI/F3 live state updates each frame; keep one version/hash, FPS/TPS/chunks/position/
  facing/target/biome/light/memory/OS/GPU/workers. Performance uses 0.75s exponential
  history displayed at 250ms; memory shares observation cadence. Cache static/right text
  geometry until identity/viewport/font/texture changes. Never smooth/delay coordinates,
  targeting, biome or light. Telemetry adds no per-frame allocation, World lock, disk I/O
  or job wait; a brief bounded scheduler snapshot lock may copy numeric job states only.

## Instances, Launcher and Security

- Instances are UUID physical environments with authoritative `instance.json`; current
  Launcher schema 3 stores UUID references/selection, not duplicate name/build records.
  Names never determine paths. `MaaGE::Instance` is reusable C90 without UI/graphics.
  Worlds/mods/config/logs are private; only screenshots/shaderpacks/resourcepacks support
  explicit private, managed-shared or external mappings. No virtual/global-world fallback
  or implicit data moves. Resolve numeric folder kinds/paths only at startup/I/O boundaries.
- Manifest-governed POSIX symlinks/Windows local-volume junctions validate UUID/root
  containment, targets/conflicts; never replace populated directories. Serialize admin
  changes through UUID use leases (one Client per instance), preserving world locks.
  Resolve `--instance` before Client logging/world startup. Accept current Launcher,
  manifest and descriptor schemas only; no journals, legacy IDs or migration scaffolding.
- Native Launcher uses AppKit, Win32 or GTK 3, separate from Client UI. Ordinary UI starts
  no renderer; only explicit `--renderer-demo` does. Preserve native focus/accessibility/
  high contrast and DPI behavior; Windows uses shared private 96-DPI/font/painting helpers,
  PerMonitorV2 and Common Controls v6, updating only changed timer state. No webview/toolkit
  framework. Keep Play/account selection, News empty until real, Installations and Settings.
- Launcher preferences are nonsecret `config/launcher.json`; instance manifests own Client
  association, optional CLI and folder mappings. Parse shell-free CLI directly; reserve
  Launcher-managed instance/data-directory/appearance arguments. Play rechecks selected
  verified account/access/resources and launches only the exact assigned available build.
- Authentication is Launcher-worker-owned Microsoft consumer OAuth code + S256 PKCE with
  device fallback, approved public client `ff5d912a-45ca-409e-a364-9d1bf9512f61`, registered
  `http://localhost`. Worker loopback binds only 127.0.0.1 at an ephemeral port; exchange
  the same pathless localhost URI, validate GET path/Host/unpredictable state, bound reads,
  timeouts and cancellation. No substitute IDs/secrets, embedded browser, custom protocol
  handler or access to other launchers' credentials/OAuth clients/protected files.
- Require matching Java UUID/name and explicit Java entitlement after Xbox/XSTS/Minecraft
  exchange. Empty/Bedrock-only responses or Xbox login never grant access; do not promise
  permanent purchase versus subscription. Verified encrypted leases last at most one hour
  and token expiry; public registry metadata grants none. Signout/expiry revokes active
  work without deleting resources. Account management remains reachable at startup.
- Never log tokens, auth/callback URLs/query strings, bodies, OAuth state/PKCE/device codes,
  identities, credential paths or untrusted descriptions. Use allowlisted static routes,
  public endpoint/client/redirect configuration, numeric service/native codes and timings.
  Wipe transient secrets. Persist rotated Microsoft refresh before downstream exchanges;
  Xbox/Microsoft access tokens remain transient. Do not use Keychain/Credential Manager/
  DPAPI/Secret Service: MaaGE owns UUID-authenticated AES-256-GCM account records,
  random local key.bin, owner-only 0600/protected DACL, atomic writes and store locking.
  The local user can access key/ciphertext. Use OS crypto randomness and verified TLS.
- Skins use only the selected verified profile, validated texture routes/cache and atomic
  publication on the access worker. Failure never changes authorization; known-good/default
  fallback stays profile-scoped. Client receives a versioned nonsecret reference, never
  downloads/tokens. Normalize once into the common atlas; no per-frame I/O. Preserve
  classic/slim/legacy conversion and independent limbs/layers. See [skins](docs/player-skins.md).
- Asset setup follows verified access. Launcher acquires official 1.7.10 indexed resources
  and verified client archive solely for assets; never executes it or extracts Java code.
  Asset worker snapshots/cancellation/Retry/Repair share one verification/reconstruction
  implementation. Cache client JAR at minecraft/cache/client-1.7.10.jar; keep acquired files
  outside source/packages/releases. Client consumes reconstructed paths only, never JARs,
  network/authentication or repair. Image/MinecraftAssets remain independent of Launcher.
- Data roots: %APPDATA%/MaaGE, ~/Library/Application Support/MaaGE, or $XDG_DATA_HOME/maage
  (fallback ~/.local/share/maage). Indexed objects use minecraft/assets/objects/<prefix>/<hash>,
  reconstructed logical files assets/reconst, index assets/indexes/1.7.10.json, version
  metadata minecraft/metadata/1.7.10.json. Preserve every namespace/resource byte; JAR paths
  strip only assets/. No old-layout reuse/migration/cleanup or invented resource categories.
- Downloads verify SHA-1/size, stage atomic promotion and serialize writers. Concurrency/
  retries/timeouts/low-speed policies are configurable via MAAGE_DOWNLOAD_*; defaults are
  not hard ceilings. Deduplicate every indexed hash and reconstruct every logical path.
  Prefer verified hard links, then verified copies staged outside logical trees. Reject
  traversal, unsafe/aliased paths, links/reparse directories and file/directory conflicts.
  Bound ZIP entries/size/expansion, exclude Java/executables, verify CRC/size/derived hashes.
  Receipts cannot replace metadata/file fingerprint checks; Full Repair hashes objects and
  independent copies, and cached JAR/selected resources always require verification.
  Equal-content/spelling collisions only; log actual acquisition work without auth secrets.

## Native Rendering and Builds

- Native Window is independent of Render: Win32, X11/Wayland, AppKit, and no window for
  headless. No SDL/GLFW foundation. macOS UI calls stay on the main thread; SDK types stay
  private. Wayland alone may require target-local C99; requested resize/OpenGL/relative
  gameplay capture remain unsupported. Prefer Wayland availability with X11 fallback.
- Metal (macOS) and Vulkan (Windows/Linux and optional macOS selection) are primary;
  preserve shared semantics and shader/layout contracts. OpenGL modern/legacy and Software
  are lower priority; do not expand them without request or a required shared-interface fix.
  Renderers borrow windows; windows outlive them. Keep native interop/resource ownership,
  frame synchronization and command encoding in adapters, CPU plans shared.
- Renderer families are auto/metal/vulkan/opengl/software only. Explicit choices never
  switch families; AUTO uses Metal→Vulkan→modern GL→legacy GL→Software on macOS, Vulkan
  first elsewhere. Advertise compiled capabilities/real limits, zero when unknown;
  unsupported commands fail instead of successful NOOP/emulation. OpenGL uses native
  Cocoa/WGL/GLX (modern 3.3+, macOS 4.1; legacy 1.1); Software stays C90 RGBA presentation.
- Bound mutable frame resources and GPU work; preserve backpressure rather than software
  FPS ceilings. No per-frame GPU-wide waits; defer destruction until users retire. Vulkan
  maintenance1 is optional; Metal's first-frame priming/event responsiveness and VSync
  presenter/direct path must remain coherent. See [rendering](docs/visual-fidelity.md).
- Keep root authored CMake configuration/presets and generated cmake/<name>/ plus compiled
  build/<name>/ separate; never commit generated output. Cross-compile using target checks.
  Headless configuration must not discover graphics/GUI dependencies. Do not change
  toolchain/language standards or acquire disabled backend dependencies implicitly.
- Enabled Vulkan requires headers and host GLSL tools; Windows/Linux use their native
  runtime/loader (Windows loads dynamically). macOS enables Vulkan unless explicitly off,
  requires static target-compatible MoltenVK (MAAGE_MOLTENVK_STATIC_LIBRARY override), and
  links necessary Apple frameworks/system C++ runtime without enabling MaaGE C++. Link
  used entry points directly, retain dead stripping, no force-load/dylib/loader distribution.
  Native Metal alone requires only Apple SDK graphics; isolate Objective-C/bridges.
- Windows discovers vcpkg before project(), respecting explicit toolchains/MAAGE_USE_VCPKG.
  Keep minimal pinned platform dependencies, host tools and read-only manual CI. Static
  CRT/dependency triplets derive from target architecture, replace stock dynamic/static-md
  selections and invalidate stale paths. Keep system DLLs/GPU drivers dynamic. Generate the
  manifest/triplet under cmake/, retain explicit custom manifest/toolchain overrides and
  never auto-enable Windows manifests elsewhere. Non-Windows/custom cross retains project()
  before module options; clear caches when changing toolchains/manifests.
- Required boundaries: PNG >=1.6 in Image; zlib >=1.2.3 in GZip; Launcher curl >=7.85,
  json-c >=0.15, libzip >=1.7 (Linux pkg-config), GTK >=3.22 only in Linux UI. Crypto uses
  Windows CNG, otherwise OpenSSL >=1.1.0. Keep acquisition/GUI dependencies out of Server.
  Vendored Vorbis has its own C99 target and license; consumers use declaration-only mode.
- MAAGE_OPTIMIZE_SIZE may enable supported LTO/dead stripping/size optimization without
  losing functionality/protections/metadata/diagnostics; Debug retains debugging. Report
  executable bytes separately from bundles/disk allocation, never fabricate measurements.
- Launcher is the only desktop application/bundle. Client is standalone maage-client(.exe),
  with generated versioned package/descriptor and --version/--version-json bypass paths.
  Server is one CLI executable. CMakeApplications.cmake owns packaging. Launcher artwork
  in src/launcher/icons is Launcher-only; Client loads original reconstructed runtime icons
  through Image/Window once, without packaging/modifying them or Server branding changes.
- Client descriptors/registry SHA-256 bind exact local packages and require current instance/
  appearance contracts. SemVer precedence ignores metadata; build identity includes hash,
  platform/type/executable digest. Replacing imports reassigns safely before removing old
  packages; never overwrite/remove running builds. Debug developer imports copy only the validated executable and adjacent manifest,
  without executing or traversing other source-directory contents. Reject links,
  reparse/special files and retain developer originals. Release omits import UI; Release Client
  remains importable. Shell-free Core Process starts exact executables with explicit cwd,
  environment, nonblocking monitoring and combined append capture; no shells/Launch Services.

## Licensing and Future Work

Apache-2.0 covers original MaaGE code/docs only. Preserve LICENSE/NOTICE and third-party
licenses/disclosures. Minecraft/Mojang/Microsoft assets/trademarks stay separately owned;
never distribute proprietary assets or imply affiliation/endorsement. A legitimate
installation alone is not blanket redistribution permission.

Prefer data-driven modpacks and reusable C-compatible extension contracts with explicit
ownership. Native modules are trusted unless truly isolated. Add systems incrementally
when requested, not empty future directories/APIs. [The roadmap](docs/development-roadmap.md)
owns priorities and unimplemented features. Keep this contract focused on durable rules;
update the relevant existing section when requirements evolve.
