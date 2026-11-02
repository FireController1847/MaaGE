# Client Presentation

The Client owns UI, controls, camera, sound and visuals; the Server alone owns gameplay. The Client consumes copied state rather than reading the live Server World. Relevant public interfaces: `include/maage/{render,input,presentation,session_protocol,skin}.h`. Persistent requirements live in [AGENTS.md](../AGENTS.md).

## Screens and Controls

`src/client/ui/` owns one numeric screen registry, stack, atlas/font, layout and hit-testing pipeline. `src/client/frontend.c` owns lifecycle/catalog Jobs and options persistence, not a competing UI.

| ID | Screen | Key behavior |
| --- | --- | --- |
| 0 | Gameplay | Hotbar, HUD/vitals and F3; nonmodal |
| 1 | Pause | Resume, Options, Save/Quit |
| 2 | Options | FOV, Video, Sound, Controls |
| 3 | Inventory | Survival 2x2 crafting or Creative catalog |
| 4 | Title | Panorama, Singleplayer, Options, Quit |
| 5 | Worlds | Asynchronous catalog and world actions |
| 6 | Create World | Name, seed, game mode |
| 7 | Delete confirmation | Explicit confirmation |
| 8 | Rename | Editable name |
| 9 | Loading | Truthful preparation status, no fabricated progress |
| 10 | Video | Distance and supported video settings |
| 11 | Sound | Master/category levels |
| 12 | Crafting Table | 3x3 Server-owned container |
| 13 | Controls | Remapping and mouse preferences |

Drawing and hit testing share computed bounds within the native safe viewport. Text entry currently supports ASCII; unimplemented settings/actions remain disabled or absent. Pause/Options stop owner gameplay and music but retain safe streaming; inventory/table release capture without pausing. Save/Quit flushes asynchronously; Quit hides the window while pending teardown completes.

Actions live in a growable Client registry. Bindings use USB HID keyboard usages or one-based mouse buttons, translated by each OS adapter. Remapping waits for a new edge; Escape unassigns, focus loss cancels, duplicate bindings remain independent, and per-action/whole-list Reset restore defaults. System Escape/F3/F11 and GUI transfer modifiers are not gameplay bindings. `config/options.ini` stores `[controls]` as `key:<usage>`, `mouse:<button>` or `none`, and `[mouse]` sensitivity 0..100 (default 50) with invert; malformed entries retain defaults. `[video] render_distance` defaults to 8, usually offered as 1..32 but explicitly larger valid choices remain representable. CLI overrides startup preferences. Changing distance does not regenerate terrain.

## Replica and Asynchronous Presentation

Session first publishes numeric block/biome columns, then FIFO changed-cell deltas against acknowledged revisions and ordered removals. It sends no light arrays, meshes or live pointers. The Client replica solves its own numerical lighting with immutable snapshots and boundary inputs; stale job results are rejected/rebased. Geometry can precede final lighting and be refined later. Missing terrain is not air, and predicted block edits remain local overlays until the Server confirms/rejects them. Inventory changes remain authoritative.

Near/forward-facing missing terrain and collision cells get priority, without blocking independent work. First display advances eight directional sectors through independent two-ring groups; four groups can be active at once. A bounded Client audit requeues fully loaded columns lacking both GPU geometry and pending mesh work, including valid empty meshes. Old drawable meshes stay alive until replacement is complete. Distance reduction retires out-of-view Client meshes independently of transport unloads; unsaved Server columns and pending acknowledgment bases remain owned until safe retirement. The render loop must not wait for filesystem work.

## Rendering and Mesh Lifetimes

Common C owns CPU scene/material plans, UI batching, surface tables, geometry preparation and translucency ordering; Metal/Vulkan/modern OpenGL adapters own GPU memory, uploads, synchronization and display. Legacy OpenGL/Software retain smaller capabilities. Use `include/maage/render.h` and `src/core/render/` for actual supported operations. Per-section face merging requires compatible material/UV/animation and corner lighting/tint/AO; irregular, transparent, fluid and varying-shade geometry retains separate quads. Packed native vertices index deduplicated immutable surface records. Incremental uploads and deferred retirement bound work but cannot guarantee driver/OS latency. Intersecting translucent meshes sort approximately.

Texture catalogs, including GUI images, are registered and resolved before frame work; missing optional images use generated fallbacks. Shared shading/material rules cover foliage, AO, biome tint, fluid and distance fog, animated textures and foreground items. Night/day appearance scales sky contribution without rewriting authoritative light. No backend may silently claim support it lacks.

## Player Appearance and Cameras

F5 cycles first-person, back and front views during captured gameplay. Perspective is transient. Third person uses a four-block boom shortened by local 11x11x11 replica collision samples (including predicted changes); unavailable terrain obstructs. Player-eye targeting, movement/reach and audio never follow the displaced camera. One interpolated player model/pose source feeds first-person arm, inventory preview and world body; parts/held items reuse cached meshes. Repeated snapshots must not restart a swing or camera damage response.

The Launcher chooses only the selected verified account's ACTIVE official skin, validates its texture route, and atomically caches a PNG in `minecraft/skins/<uuid>/`. Failure preserves that account's known-good skin or default. Client startup receives a versioned nonsecret appearance reference, never credentials, arbitrary paths or URLs. A classic/slim modern 64x64 or legacy classic 64x32 PNG normalizes once to the common atlas with correct independent limb regions and translucent outer layers. There is no per-frame decoding, downloading or per-world texture replacement. Authentication success is independent of skin availability.

The Server owns sprint eligibility; Client FOV smoothing changes the world view independently of saved base FOV and fixed foreground projection. Creative sprint flight adds its own horizontal acceleration multiplier. Pickup and equipment effects are visual only.

## Current Limits

Creative has one catalog without categories/search. Armor/capes, remote players, complete Survival UI, effects/particles, exact reference lightmaps and portions of secondary rendering are incomplete. Wayland does not yet provide Client relative mouse capture. Source-level contracts are not runtime/platform verification.
