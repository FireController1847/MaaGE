# Content Modules

MaaGE supplies mechanisms; statically linked modules register content and policy. `MaaGE::Base` is an ordinary `maage_content_module` selected by `src/application/content_modules.c` for Client and Server. The authoritative signatures and lifetime comments are in `include/maage/content.h`.

## Composition and Lifetime

`maage_content_config.modules` submits root modules (`minecraft`, `ie2`) or submodules (`ie2:expanded`). Identities use lowercase letters, digits, `_`, `-` and `.` in each nonempty segment, with zero or one colon. A submodule's root must exist and appear explicitly in its dependency array. No dependency is inserted automatically. The existing stable topological sorter rejects duplicate modules/dependencies, missing dependencies and cycles, choosing the earliest submitted ready module for deterministic numeric assignment.

Every applicable module finishes the current stage before the next stage begins:

| Stage | Contract |
| --- | --- |
| DEFINE | Copy definitions and retain allocated numeric IDs. No runtime services or side effects; also runs during resource discovery. |
| BIND 1: INT | Resolve the module's own relationships after all definitions exist. |
| BIND 2: EXT | Resolve relationships with explicitly declared dependencies; skipped without dependencies. |
| BIND 3: FIX | Explicit compatibility work declared by a non-NULL callback; dependency validation remains mandatory. |
| FINALIZE | Validate references, resolve copied presentation, freeze registries and validate an optional identity peer. |
| INITIALIZE | Initialize the selected runtime side once, against immutable definitions; skipped for resource catalogs. |

`minecraft` is the ordinary built-in root module. It registers `minecraft:*`, binds its internal references in INT, and declares no EXT, FIX or initializer. Existing recipes, smelting, fluids, containers, sounds, Creative and generation still use public registration interfaces.

A root owns its namespace; submodules share that namespace without acquiring ownership of their parent's existing definitions. Registry entry points enforce ownership through a composition-local scope, including named blocks, items, biomes, fluids, geometry, containers, Creative tabs, sounds, block-entity codecs and generation registrations. Each identity retains its exact registering module. Duplicate identities and foreign definition updates fail, including between siblings. Singleton engine role bindings have one module owner. Intentional replacement, unregistration and foreign definition patching remain unsupported; integrations can register their own recipes and relationships. A failed registration must abort its callback. Ownership violations remain fatal to composition even if a callback ignores the returned error.

Name resolution and `maage_content_module_data` require an explicit direct dependency when accessing another module's content/state; INT permits only the current module. Numeric getters remain direct O(1) services, not authorization boundaries for trusted native code. Names resolve at composition/import/I/O boundaries, never in gameplay loops. Optional presentation/appearance resolvers update copied UV/tint information without changing logical texture identities.

`module_data` survives a module's phases. `maage_content_own` transfers data on success and destroys it after the registries. Config and task-owned `context.trace` borrow only through creation; descriptor arrays, names and callbacks outlive the content and any independently composed Server using them. Callback code and registered state outlive Worlds, jobs and audio/visual readers. Stop all consumers before destroying content. Independent compositions must use per-context owned state rather than mutate static module data.

## Execution Context and Bootstrap

`execution.h` supplies a copyable side/capability value: Client, integrated Server or dedicated Server. Client has presentation configuration; both Server sides have authority and are headless. Presentation capability does not promise a live rendering/audio device. There is no mutable global side. Lifecycle callbacks receive `context.execution`; runtime code can retain a copy, query `maage_content_execution`, or query `maage_world_execution` when World is available. Capabilities describe facilities rather than executable names or operating systems.

Shared DEFINE/bind callbacks are pure composition work. Optional `side_bind_int`, `side_bind_ext` and `side_bind_fix` execute at their corresponding stage only for the descriptor's `sides` mask. `initialize` uses the same mask. Zero selects no side hooks/initializer. A selected callback fails if `required_capabilities` are unavailable. EXT still requires dependencies; a declared FIX callback is the explicit compatibility condition. No optional-dependency discovery or undeclared compatibility targets are implemented.

Client resource discovery creates a `catalog_only` composition: shared DEFINE/binding/finalization only, with all side hooks and initialization explicitly skipped. Resource discovery/atlas assembly uses its registered sources and biomes. The presentation-resolved Client composition then runs its selected side hooks and initialization, validating its numeric catalog before initialization. Add atlas sources/textures in pure definition work so discovery sees them.

Integrated Server startup borrows the finalized Client as an **identity peer**, obtains the same submitted descriptors, and creates independent headless registries and module-owned state on the authoritative owner thread. Its own three binding stages, Server hooks and initialization run there. Dedicated Server independently uses the application composition with its dedicated context and no Client/device dependency. World, storage, scheduling and teardown authority remain unchanged.

`maage_content_validate_identity` compares named domain/owner/numeric-ID maps, block-state `(block, metadata)` maps, presentation sources/texture descriptors and Creative entry stack identities. Local hosting validates before module initialization, World/store opening or packet publication. Different behavior callbacks and runtime-only owned state may coexist with identical content identities. Side-only registrations are supported for independent compositions (including dedicated Server), but differing peer identity maps are rejected explicitly. Per-peer ID translation, hidden Server-only blocks crossing a session, and remote content negotiation are not implemented. Existing World dictionaries/save encodings are unchanged.

## Diagnostics

Central Log category `maage.content` uses INFO for loading/finalization/completion summaries and VERBOSE for submitted identities, root/parent relationships, dependencies, actual stable order, stage boundaries, shared/side callbacks and skipped work with reasons. Logs include execution side, catalog purpose, registry counts and elapsed monotonic time when available. A failure is logged once at the composition handling boundary with original Status/causal trace, module, named stage, dependency/registry, offending content identity where known, and reason. `maage_content_failure` preserves those details for callers; callbacks can append original causes through `context.trace`. No new severity or logging system is introduced.

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

`maage_content_context.creative` owns the presentation catalog alongside the existing item and container registries; it owns no inventory. Register tabs with `maage_creative_tab_register` in define, retain returned IDs (or resolve names at module boundaries), and add individual variants with `maage_creative_entry_register` in DEFINE or the appropriate binding stage. `maage_content_creative` exposes the finalized catalog to frontends and the Server. `src/base/creative.c` registers the twelve built-in tabs and their explicit assignments using these same APIs.

Tab `order` sorts independently of numeric registration identity. Category entries have separate `order` and `search_order`, an explicit count-one/pristine stack, optional copied tooltip lines and a `searchable` flag. Item definitions may also supply immutable metadata-specific tooltip lines through their `tooltip` callback; search and ordinary item hover share them. Search and inventory are optional singleton presentation roles. Finalization validates referenced items, metadata and registered textures, creates ordered per-tab views, and deduplicates searchable item/metadata pairs. There is no implicit enumeration through `max_metadata`. Registration may move definition arrays; retain numeric IDs until finalization.

Tabs can use an existing item stack as their icon or copied textured quads in a 16x16 cell. Presentation-only icons do not register obtainable items. GUI backgrounds and tab sprites use the ordinary presentation atlas, including its resource failure handling. A personal container with `creative_catalog` uses the shared 195x136 layout, 256-pixel tab sprite sheet and real player inventory. Category pages show ten tabs while Search and Survival Inventory stay available. Neither catalog entries nor presentation tab IDs are persisted as gameplay stacks.
