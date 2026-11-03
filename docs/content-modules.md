# Content Modules

MaaGE supplies mechanisms; statically linked modules register content and policy. `MaaGE::Base` is an ordinary `maage_content_module` selected by `src/application/content_modules.c` for Client and Server. The authoritative signatures and lifetime comments are in `include/maage/content.h`.

## Composition and Lifetime

`maage_content_config.modules` submits root modules (`minecraft`, `ie2`) or submodules (`ie2:expanded`). Identities use lowercase letters, digits, `_`, `-` and `.` in each nonempty segment, with zero or one colon. A submodule's root must exist and appear explicitly in its dependency array. No dependency is inserted automatically. The existing stable topological sorter rejects duplicate modules/dependencies, missing dependencies and cycles, choosing the earliest submitted ready module for deterministic numeric assignment.

Every applicable module completes the current stage before the next begins: **DEFINE → BIND, INTERNAL → BIND, EXTERNAL → BIND, COMPATIBILITY → FINALIZE → LOAD → INITIALIZE**. The full public phase constants and callbacks use `INTERNAL`, `EXTERNAL` and `COMPATIBILITY` without compatibility aliases. INTERNAL resolves a module's own relationships; EXTERNAL resolves declared dependencies and requires at least one; a non-NULL COMPATIBILITY callback declares explicit integration work. Missing callbacks do nothing and produce no skip logs. Finalization validates references and freezes logical definitions; Server omits LOAD and initializes after peer validation. Client initialization waits for required physical resources.

`minecraft` is the ordinary built-in root module. It registers `minecraft:*`, binds internal references in INTERNAL, and declares no EXTERNAL, COMPATIBILITY or initializer. Recipes, smelting, fluids, containers, sounds, Creative and generation retain public registrations.

A root owns its namespace; submodules share that namespace without owning existing parent/sibling definitions. Composition-local registry scopes enforce ownership across named blocks, items, biomes, fluids, geometry, containers, Creative tabs, sounds, block-entity codecs and generation registrations. Duplicate identities, foreign updates and conflicting singleton bindings fail. Unregistration and intentional replacement remain unsupported. Failed registration must abort its callback; ignored ownership violations still fail composition.

Name resolution and `maage_content_module_data` require an explicit direct dependency for another module's content/state; INTERNAL permits only the current module. Numeric getters remain direct O(1) services for trusted native code. No name resolution is added to gameplay loops.

`module_data` survives a module's phases. `maage_content_own` transfers data on success and destroys it after the registries. Config borrows through creation; `context.trace` borrows only through its current lifecycle call; descriptor arrays, names and callbacks outlive the content and any independently composed Server using them. Callback code and registered state outlive Worlds, jobs and audio/visual readers. Stop all consumers before destroying content. Independent compositions must use per-context owned state rather than mutate static module data.

## Execution Context and Bootstrap

`execution.h` supplies a copyable side/capability value: Client, integrated Server or dedicated Server. Client has presentation configuration; both Server sides have authority and are headless. Presentation capability does not promise a live rendering/audio device. There is no mutable global side. Lifecycle callbacks receive `context.execution`; runtime code can retain a copy, query `maage_content_execution`, or query `maage_world_execution` when World is available. Capabilities describe facilities rather than executable names or operating systems.

Shared DEFINE/bind callbacks are pure composition work. Optional `side_bind_internal`, `side_bind_external` and `side_bind_compatibility` execute at their corresponding stage only for the descriptor's `sides` mask. `initialize` uses the same mask. Zero selects no side hooks/initializer. A selected callback fails if `required_capabilities` are unavailable. EXTERNAL still requires dependencies; a declared COMPATIBILITY callback is the explicit compatibility condition. No optional-dependency discovery or undeclared compatibility targets are implemented.

Client creates one finalized logical composition. `maage_client_resources_load` consumes its numeric presentation sources directly; there is no preliminary composition. LOAD decodes sources, constructs the atlas and samples colormaps. `maage_content_view_create` copies resolved state/geometry appearances and biome colors while borrowing immutable logical names, types, items and behavior. It performs no registration or finalization. These immutable presentation snapshots are independently destroyable and can be created repeatedly. `maage_content_load_complete` publishes Client readiness; `maage_content_initialize` rejects repeated or premature initialization. Resource refreshes never call initialization.

Integrated Server obtains the same submitted descriptors through the finalized Client identity peer, independently composes headless content on its owner thread, validates numeric identities, then initializes before opening World/storage or publishing packets. Dedicated Server does the same with its dedicated context and application composition. Neither requires Client resources or devices. World scheduling, authority and persistence formats remain unchanged.

Peer validation compares named domain/owner/ID maps, state `(block, metadata)` maps, logical texture descriptors and Creative entry identities. Behavior callbacks and runtime-owned state may differ with coherent IDs. Side-only registrations remain supported for independent compositions; mismatching session peers are rejected. Remote negotiation, hidden Server-only IDs crossing a session and optional-dependency discovery remain unimplemented.

## Diagnostics

Central Log category `maage.content` retains shared lifecycle summaries with explicit sides. DEBUG produces at most one completion per module per phase that executed callbacks, including both shared and side work. Dependency validation/order remain available at DEBUG. Empty callbacks/phases produce no ordinary lines. Failures retain original Status/trace, exact module/phase, dependency/registry, offending identity where known and reason. INFO summarizes composition, finalization, LOAD and initialization. See the Client presentation document for reload publication and the logging policy.

## Extension Interfaces

| Interface | Contract |
| --- | --- |
| `blocks.h`, `items.h`, `biome.h`, `presentation.h` | Immutable finalized numeric definitions and behavior/presentation metadata. |
| `creative.h` | Explicit Creative tab identities, titles, icons/backgrounds, order and obtainable metadata entries. Finalized tab/search views borrow numeric entry IDs; item validity never implies visibility. |
| `containers.h` | Copied layouts, slot/progress descriptors and Server-owner open/access/policy/action/sample/retryable-close callbacks. Independently paged transport windows do not cap registrations. Transaction failure retains item ownership; custom backing stores stay module-owned. |
| `fluids.h` | Fluid identities and copied state maps for schedules, propagation, contact, physics, material and immersion; finalized dispatch is numeric. |
| `block_geometry.h` | Optimized primitives or immutable worker-safe quad providers. Allocation-free count/write passes agree; collision/selection bounds are separate from visual geometry. |
| `sounds.h` | Registered resources/events/music/ambience independent of playback; Client owns decode, voice and device capacity. |
| `crafting.h`, `smelting.h` | Numeric recipes, smelting and fuel policy used by the authoritative owner. |

No API registry has an artificial limit derived from transport page size, GPU working set or simultaneous sound voices. Saved block/state/item/biome IDs use the World dictionary; other runtime IDs follow the deterministic current composition.

Entity and block-entity registration/codecs remain distinct facilities. Dynamic module loading/discovery, scripting and remote content negotiation are not implemented.

## Creative Catalogs

`maage_content_context.creative` owns the presentation catalog alongside the existing item and container registries; it owns no inventory. Register tabs with `maage_creative_tab_register` in DEFINE, retain returned IDs (or resolve names at module boundaries), and add individual variants with `maage_creative_entry_register` in DEFINE or the appropriate binding stage. `maage_content_creative` exposes the finalized catalog to frontends and the Server. `src/base/creative.c` registers the twelve built-in tabs and their explicit assignments using these same APIs.

Tab `order` sorts independently of numeric registration identity. Category entries have separate `order` and `search_order`, an explicit count-one/pristine stack, optional copied tooltip lines and a `searchable` flag. Item definitions may also supply immutable metadata-specific tooltip lines through their `tooltip` callback; search and ordinary item hover share them. Search and inventory are optional singleton presentation roles. Finalization validates referenced items, metadata and registered textures, creates ordered per-tab views, and deduplicates searchable item/metadata pairs. There is no implicit enumeration through `max_metadata`. Registration may move definition arrays; retain numeric IDs until finalization.

Tabs can use an existing item stack as their icon or copied textured quads in a 16x16 cell. Presentation-only icons do not register obtainable items. GUI backgrounds and tab sprites use the ordinary presentation atlas, including its resource failure handling. A personal container with `creative_catalog` uses the shared 195x136 layout, 256-pixel tab sprite sheet and real player inventory. Category pages show ten tabs while Search and Survival Inventory stay available. Neither catalog entries nor presentation tab IDs are persisted as gameplay stacks.
