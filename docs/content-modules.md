# Content Modules

MaaGE supplies mechanisms; statically linked modules register content and policy. `MaaGE::Base` is an ordinary `maage_content_module` selected by `src/application/content_modules.c` for Client and Server. The authoritative signatures and lifetime comments are in `include/maage/content.h`.

## Composition and Lifetime

`maage_content_config.modules` submits namespaced module descriptors and optional named dependencies. Creation rejects duplicates, missing dependencies and cycles, then chooses a stable topological order for deterministic numeric assignment. All modules **define**, all **resolve** forward references, registries **finalize**, then all **initialize** against immutable definitions. `maage_content_failure` reports the module/dependency, phase and original Status.

`maage_content_context.module_data` survives a module's phases. `maage_content_own` transfers owned state on success; `maage_content_module_data` resolves another module's published state at composition boundaries. Config/descriptors are borrowed through creation, while callback code, registered contexts and module-owned state outlive Worlds, jobs and audio/visual readers. Stop all consumers before destroying content. Client atlas/resource users retain their composition descriptors until teardown.

Registration copies definitions and checks references across content domains. Namespaced names resolve at registration/import boundaries; runtime hot paths use numeric IDs. Optional presentation/appearance resolvers update copied UV/tint information without changing logical texture identities. Headless composition does not require image resolution.

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

`maage_content_context.creative` owns the presentation catalog alongside the existing item and container registries; it owns no inventory. Register tabs with `maage_creative_tab_register` in define, retain returned IDs (or resolve names at module boundaries), and add individual variants with `maage_creative_entry_register` in define/resolve. `maage_content_creative` exposes the finalized catalog to frontends and the Server. `src/base/creative.c` registers the twelve built-in tabs and their explicit assignments using these same APIs.

Tab `order` sorts independently of numeric registration identity. Category entries have separate `order` and `search_order`, an explicit count-one/pristine stack, optional copied tooltip lines and a `searchable` flag. Item definitions may also supply immutable metadata-specific tooltip lines through their `tooltip` callback; search and ordinary item hover share them. Search and inventory are optional singleton presentation roles. Finalization validates referenced items, metadata and registered textures, creates ordered per-tab views, and deduplicates searchable item/metadata pairs. There is no implicit enumeration through `max_metadata`. Registration may move definition arrays; retain numeric IDs until finalization.

Tabs can use an existing item stack as their icon or copied textured quads in a 16x16 cell. Presentation-only icons do not register obtainable items. GUI backgrounds and tab sprites use the ordinary presentation atlas, including its resource failure handling. A personal container with `creative_catalog` uses the shared 195x136 layout, 256-pixel tab sprite sheet and real player inventory. Category pages show ten tabs while Search and Survival Inventory stay available. Neither catalog entries nor presentation tab IDs are persisted as gameplay stacks.
