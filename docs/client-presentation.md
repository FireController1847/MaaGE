# Client Presentation

The Client owns UI, controls, camera, sound and visuals; the Server alone owns gameplay. The Client consumes copied state rather than reading the live Server World. Relevant public interfaces: `include/maage/{render,input,presentation,session_protocol,skin}.h`. Persistent requirements live in [AGENTS.md](../AGENTS.md).

## Screens and Controls

`src/client/ui/` owns one numeric screen registry, stack, atlas/font, layout and hit-testing pipeline. `src/client/frontend.c` owns lifecycle/catalog Jobs and options persistence, not a competing UI.

| ID | Screen | Key behavior |
| --- | --- | --- |
| 0 | Gameplay | Hotbar, HUD/vitals and F3; nonmodal |
| 1 | Pause | Resume, Options, Save/Quit |
| 2 | Options | FOV, Video, Sound, Controls |
| 3 | Inventory | Survival 2x2 crafting or registered Creative tabs/search/player inventory |
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

## Creative Inventory

The personal container's Creative presentation uses a 195x136 logical panel, 9x5 catalog, authoritative nine-slot hotbar and the original registered `creative_inventory/{tabs,tab_items,tab_item_search,tab_inventory}.png` resources. Selected tabs draw after the panel; unselected tabs draw before it. Extra registered categories page ten at a time, retaining Search and Survival Inventory on the right. Layout, slot hover bounds, scrollbar travel and safe-area scaling share the input/drawing geometry. Selecting a tab or reopening resets scrolling; reopening retains the selected tab and clears its query.

Search stays focused in its tab, accepts up to fifteen ASCII characters and filters a reusable ordered entry-ID view against metadata-specific display names and registered tooltip text. Matching is case-insensitive; empty results have no active scrollbar. Backspace, Delete, selection and cursor movement edit locally. The default chat key T selects Search from another tab; while Search is focused, the inventory binding types instead of closing. Escape closes. Hotbar bindings retain their empty-cursor shortcut precedence.

Catalog primary/secondary clicks take one item, increase/decrease matching carried quantities, or clear a different carried stack. Shift-primary fills the stack; middle-click clones into an empty cursor. Number bindings replace a real hotbar slot with a full catalog stack; drop bindings create one item or, with Control, a full stack. Player slots use shared stack clicks, splitting, swaps, transfers, drag distribution and double-click collection. Shift-clicking the catalog hotbar deletes that stack; the Survival Inventory tab transfers between main inventory and hotbar instead. Shift-double-click transfers matching stacks from the source region.

Survival Inventory uses the Creative inventory background, shared player preview, real main inventory/hotbar and a destroy slot. Unsupported equipment positions are decorative, with no fabricated storage or crafting state. Destroy consumes the cursor without entities; shift-destroy atomically clears player inventory, cursor and temporary player inputs. Outside clicks and normal Creative cursor closure use the reserved owner drop path. Changing tabs retains the cursor.

The UI queues typed requests without changing authoritative stacks. Its growable FIFO retains accepted clicks across transport backpressure, allowing one container request at a time; Creative actions acquire the next acknowledged revision, while registered output slots retain their observed result revision. Tab scope is captured with each request, while search/tab selection itself remains local. Inventory, crafting-table and furnace layouts continue through the registered container screen and atlas. Compilation, input feel and visual fidelity still require programmer verification.

## Replica and Asynchronous Presentation

Session first publishes numeric block/biome columns, then FIFO changed-cell deltas against acknowledged revisions and ordered removals. It sends no light arrays, meshes or live pointers. The Client replica solves its own numerical lighting with immutable snapshots and boundary inputs; stale job results are rejected/rebased. Geometry can precede final lighting and be refined later. Missing terrain is not air, and predicted block edits remain local overlays until the Server confirms/rejects them. Inventory changes remain authoritative.

Near/forward-facing missing terrain and collision cells get priority, without blocking independent work. First display advances eight directional sectors through independent two-ring groups; four groups can be active at once. A bounded Client audit requeues fully loaded columns lacking both GPU geometry and pending mesh work, including valid empty meshes. Old drawable meshes stay alive until replacement is complete. Distance reduction retires out-of-view Client meshes independently of transport unloads; unsaved Server columns and pending acknowledgment bases remain owned until safe retirement. The render loop must not wait for filesystem work.

## Rendering and Mesh Lifetimes

Common C owns CPU scene/material plans, UI batching, surface tables, geometry preparation and translucency ordering; Metal/Vulkan/modern OpenGL adapters own GPU memory, uploads, synchronization and display. Legacy OpenGL/Software retain smaller capabilities. Use `include/maage/render.h` and `src/core/render/` for actual supported operations. Per-section face merging requires compatible material/UV/animation and corner lighting/tint/AO; irregular, transparent, fluid and varying-shade geometry retains separate quads. Packed native vertices index deduplicated immutable surface records. Incremental uploads and deferred retirement bound work but cannot guarantee driver/OS latency. Intersecting translucent meshes sort approximately.

Registered models share one part/cuboid/provider API across living entities and item icons, held items and dropped items. Model-backed items bind a numeric model and presentation transform before finalization; chest and trapped-chest items register separate full 64×64 sheets with body, lid and latch geometry. Ordinary placement and container behavior stay in the existing item/block paths. Model parts borrow numeric textures from the current atlas and cached GPU meshes; animations transform instances rather than rebuilding part geometry.

Logical texture sources and identities are registered once before loading. Client LOAD preserves registered rectangular model sheets with padded regions and builds a replaceable atlas, resolved appearance snapshots and sampled biome colors without changing logical definitions. Missing optional startup images use generated fallbacks. Shared shading/material rules cover foliage, AO, biome tint, fluid and distance fog, animated textures and foreground items. Night/day appearance scales sky contribution without rewriting authoritative light. No backend may silently claim support it lacks.

F3+T in an active world invokes the render-owner resource refresh entry point. It stages atlas upload, GUI/font/HUD textures, environment textures and item caches before publication. Any failed/partial preparation keeps the prior complete presentation. Publication updates the replica's appearance view, cancels/invalidates obsolete meshes without waiting, clears borrowed render arrays and rebuilds local item/player/edit geometry. Numeric IDs, pending gameplay actions, Server state and active World identity remain intact. Backends retire GPU resources through their existing lifetime rules. Old CPU appearance snapshots survive until no outstanding replica job borrows them; further reloads apply backpressure while one retired generation remains. A replica handed to background shutdown retains its views until that worker finishes. INITIALIZE never runs again. Audio retains its independent lazy catalog/selected-track pipeline and is not reopened by texture refresh. Resource-pack selection/UI, alternate resource roots, shader reload and audio cache refresh are not implemented.

Central Log supports VERBOSE, DEBUG, INFO, WARNING, ERROR, FATAL and OFF. Debug defaults to DEBUG; other builds default to INFO. DEBUG is ordinary development/resource detail. High-volume tick, streaming, interaction and per-frame renderer diagnostics use explicit VERBOSE. First-party C90 event gates avoid evaluating disabled arguments; outside Debug, VERBOSE expressions/format strings are removed while the public function ABI remains available. Release `--log-level=VERBOSE` is accepted and enables DEBUG/higher only. Logger-specific INI/CLI overrides retain dot-boundary inheritance and precedence. An atomic conservative minimum rejects disabled severities without locking; an allocation-free native per-thread last-category cache avoids repeated override resolution. Updates invalidate caches under the existing log lock; enabled events recheck filtering and serialize complete records/traces through that lock. Other native compilers retain a synchronized fallback. Renderers share `maage.client.render` with backend details in messages; World selects Client/Server categories from its owning configuration. Core/Launcher utility categories remain independent.

## Player Appearance and Cameras

F5 cycles first-person, back and front views during captured gameplay. Perspective is transient. Third person uses a four-block boom shortened by local 11x11x11 replica collision samples (including predicted changes); unavailable terrain obstructs. Player-eye targeting, movement/reach and audio never follow the displaced camera. One interpolated player model/pose source feeds first-person arm, inventory preview and world body; parts/held items reuse cached meshes. Repeated snapshots must not restart a swing or camera damage response.

The Launcher chooses only the selected verified account's ACTIVE official skin, validates its texture route, and atomically caches a PNG in `minecraft/skins/<uuid>/`. Failure preserves that account's known-good skin or default. Client startup receives a versioned nonsecret appearance reference, never credentials, arbitrary paths or URLs. A classic/slim modern 64x64 or legacy classic 64x32 PNG normalizes once to the common atlas with correct independent limb regions and translucent outer layers. There is no per-frame decoding, downloading or per-world texture replacement. Authentication success is independent of skin availability.

The Server owns sprint eligibility; Client FOV smoothing changes the world view independently of saved base FOV and fixed foreground projection. Creative sprint flight adds its own horizontal acceleration multiplier. Pickup and equipment effects are visual only.

## Current Limits

Armor/equipment storage, enchantments, potion behavior, capes, remote players, complete combat/Survival, effects/particles, exact reference lightmaps and portions of secondary rendering are incomplete. Unsupported items are absent from Creative categories; some tabs are empty. Wayland does not yet provide Client relative mouse capture. Source-level contracts are not runtime/platform verification.

On macOS, Client startup optionally requests its product name through dynamically resolved private LaunchServices symbols after AppKit launches. Missing framework/symbols/application identity or a rejected update retain the system Dock label and produce only a VERBOSE diagnostic. `MAAGE_CLIENT_MACOS_DOCK_NAME=OFF` omits this adapter. Private ABI compatibility and actual Dock behavior remain OS-dependent; the Client remains a standalone executable.
