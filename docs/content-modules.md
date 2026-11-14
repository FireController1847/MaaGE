# Content Modules

MaaGE supplies mechanisms; modules define content and policy. The ordinary built-in `MaaGE::Base` (`minecraft`) is statically selected in `src/application/content_modules.c`. Public API/lifetimes: `include/maage/content.h`. Durable rules: [AGENTS.md](../AGENTS.md).

## Composition and Lifetime

`maage_content_config.modules` submits roots or submodules such as `ie2:expanded`. Each nonempty name segment allows lowercase letters, digits, `_`, `-`, `.`; zero or one colon. Submodules explicitly depend on their root. Stable topological ordering chooses the earliest submitted ready module and rejects duplicate modules/dependencies, missing dependencies and cycles.

All modules complete each stage before the next: **DEFINE → BIND, INTERNAL → BIND, EXTERNAL → BIND, COMPATIBILITY → FINALIZE → LOAD → INITIALIZE**. INTERNAL resolves own relationships; EXTERNAL requires declared dependencies; a non-NULL COMPATIBILITY callback declares explicit integration. Absent callbacks are silent. Finalization validates/freezes definitions; Server omits LOAD and initializes after peer validation. Client initializes after required resources. Minecraft uses DEFINE/INTERNAL through the same APIs, without external or compatibility hooks or initializer.

Roots own namespaces; submodules share them without ownership of parent/sibling identities. Registry scopes reject foreign updates, duplicate identities and conflicting singleton bindings; no replacement/unregistration. Ignored ownership violations still fail composition. Name resolution and `maage_content_module_data` require a direct declared dependency; INTERNAL permits only self. Numeric getters remain direct O(1).

`module_data` survives all phases; `maage_content_own` transfers data on success, destroying it after registries. Config borrows during creation; traces during the current call. Descriptors/names/callbacks and registered state outlive all compositions, Worlds, Jobs and presentation/audio consumers. Independently composed sides use per-context state, not mutable static state.

## Tags, Properties and Behaviors

The composition's `context.traits` registers typed properties (boolean, bounded unsigned, extensible namespaced identity), property values, domain-aware tags and behavior providers. Zero is valid; `MAAGE_CONTENT_ID_INVALID` is invalid. Property configuration zero is empty.

`maage_property_set_register` copies, validates, sorts and interns assignments; unordered resolution is supported, duplicates rejected. States explicitly register sparse configurations. `maage_block_state_value` resolves constants/state values; `maage_block_state_change` resolves existing configurations/states without allocation. No automatic Cartesian product or runtime semantic interpretation in rendering. The face definition's `texture_rotation` defaults to fixed; ordinary opaque full-cube faces can opt into `MAAGE_BLOCK_TEXTURE_ROTATION_RANDOM_90`, a world-cell shader hash with no new states/storage. Layered, inset, oriented, fluid and custom geometry and inventory/entity models retain authored UVs.

`maage_tag_register` declares domains, requirements, included earlier tags and providers. Inclusions flatten per block/item domain; only applicable paths contribute. `maage_traits_assign` assigns owned subjects tags and constants. Every block has a tag; items may have none. Every state/variant satisfies effective required properties; constants cannot also be assigned in configurations. Invalid domains/types, duplicates and missing values fail. The engine assigns no tag meanings.

Providers specify domains, event, immutable context, callback, explicit order and optional earlier dependencies. Reserved block lifecycle events require block-only domains and dispatch via `maage_block_behavior_call`. Composition deduplicates providers and rejects missing dependencies, incompatible domains, exclusive conflicts or unordered equal-order handlers. Failures stop dispatch with original Status; warnings survive. Placement predicates combine with AND. Exclusive SURVIVES returns a six-face ALL/ANY rule and requires SUPPORT_LOST; predicate NOOP denotes unavailable input. CAN_PLACE is independent. NEIGHBOR_CHANGED receives the actual adjacent transition and can defer before effects, retaining its continuation cursor. Register these on tags; finalization installs typed block callbacks, rejecting uncomposed callbacks. See [directional support](world-and-gameplay.md#block-changes-and-directional-support).

Items use contiguous local variants (zero default), immutable configurations and copied appearance/placement rows. Stack count/damage are separate. Placeable variants and placement callbacks reference registered states of the declared block. Target-transforming use has its own contract. Recipes match identity or item-domain tag with optional property constraints; output projections compile unique numeric property-tuple mappings at finalization. Missing/ambiguous outputs fail, enabling external variants without changing recipes.

Authoritative item `use` receives a `consume_use` output initialized to one. On SUCCESS it can leave one to charge the ordinary item/durability use or set zero for a reusable interaction; NOOP and failures never charge the stack. The owner validates the zero/one contract, while Creative retains its normal no-cost behavior. Base Eyes consume a use when filling a frame, but can restore a complete filled ring's surface without inserting another Eye.

Traits finalize before blocks/items. Membership costs `S * ceil(T / 8)` bytes for S subjects and T tags, independent of cells/stacks; direct bit lookup, sparse provider chains and immutable configurations allocate nothing at runtime. Temporary domain closures/workspace are released, partial compiled tables cleared on failure. Peer validation compares logical contracts and dispatch identity, not callback addresses.

### Minecraft Contracts

All following classifications/behaviors belong to the Minecraft module. Other modules may define their own or explicitly depend on Minecraft.

| Tag | Domain | Contract |
| --- | --- | --- |
| wood, log, planks | Block/item | Extensible wood_type; log removal notifies leaves |
| axial | Block/item | axis (0 vertical, 1 X, 2 Z for Base logs) |
| leaves | Block | Wood, persistent/check flags; decay/import/neighbor behavior |
| soil, sand | Block/item | Plant support; sand_type for sand |
| crop, farmland | Block | crop_age/moisture 0–7; growth, hydration/support/landing |
| soil_plant | Block | Downward soil support, placement and loss drops |
| mushroom, cactus | Block | Shared survival/growth; cactus age 0–15 |
| wood_drops | Block | Typed wood variants of configured drop |
| pickaxe, shovel, axe, shears, hoe | Item | Tool eligibility, effectiveness and conditional wear |

External logs can support leaves and join wood recipes by registering namespaced wood values and exactly one tagged plank output per projected value. Compatible crops provide required age states, appearances and drops. Sparse absent transitions fail explicitly. Base uses typed assignments rather than packed metadata; specialized blocks use their own classifications/providers. Wood classification alone does not create a new tree generator.

## Execution Context and Bootstrap

`execution.h` provides copyable Client, integrated Server and dedicated Server side/capability values. Server is authoritative/headless; presentation capability does not guarantee a live device. No global side. Query via context, `maage_content_execution` or `maage_world_execution`.

Shared DEFINE/bind hooks are composition work. Optional side_bind_internal/external/compatibility and initialize hooks obey descriptor sides (zero selects none) and required capabilities. EXTERNAL still requires dependencies; no optional dependency discovery.

Client composes once. `maage_client_resources_load` loads numeric sources, atlas and colormaps; `maage_content_view_create` creates immutable resolved appearances borrowing logical definitions. `maage_content_load_complete` publishes readiness, then initialize runs exactly once. Reload creates only physical views; see [Client presentation](client-presentation.md#rendering-and-mesh-lifetimes).

Integrated Server independently composes the Client peer's descriptors on its owner thread; dedicated Server uses the application composition. Both validate identities before initialization/World/session use, without Client devices. Model/entity definitions compose on both sides, GPU geometry only on Client.

Peer validation covers named domain/owner/ID maps, property schemas/values/configurations, flattened tags/membership, behavior domains/order, state maps, item variants and logical placement/appearance, texture descriptors and Creative identities. Callback addresses/state may differ; mismatched side-only registrations fail paired sessions. Copied stacks validate before UI acceptance.

## Diagnostics

`maage.content` logs explicit sides. DEBUG logs dependency/order and one completion per module/phase with actual work; INFO summarizes composition/finalization/LOAD/initialize. Failures retain original Status/trace, phase/module and offending identity/reason. See [diagnostics](diagnostics.md).

## Extension Interfaces

| Header | Contract |
| --- | --- |
| traits.h | Typed properties, tags, deterministic provider dispatch |
| blocks.h, items.h, biome.h, presentation.h | Immutable numeric definitions and appearance |
| creative.h | Explicit tabs and obtainable variant entries |
| containers.h | Copied layouts/progress; owner open/access/action/sample/retryable close |
| fluids.h | Numeric state maps for propagation, contact, physics, immersion |
| block_geometry.h | Immutable worker-safe count/write geometry; separate collision/selection |
| dimensions.h | Named World definitions, generation/environment policy and owner portal callbacks |
| entity_types.h, entities.h | Registered types, loot/spawn rules, instance behavior and codecs |
| models.h | Hierarchies, cuboids, numeric materials, channels and animation callbacks |
| sounds.h | Registered sounds/music/ambience independent of device capacity |
| crafting.h, smelting.h | Numeric recipe, smelting and fuel policy |

Registries are growable, not capped by transport pages, GPU working sets or voice capacity. Types, live instances, block entities and models remain distinct. `maage_entity_type_register` assigns type ID; `maage_entities_spawn_type` assigns instance ID; `maage_entity_type_update` binds copied references before finalization. Instances/checkpoints own flat state; callbacks borrow module context. `maage_item_registry_bind_model` changes presentation only. Dynamic loading/discovery, scripting and remote negotiation are unimplemented.

## Creative Catalogs

`context.creative` owns presentation, not inventory. Register tabs with `maage_creative_tab_register` in DEFINE and explicit variants with `maage_creative_entry_register` in DEFINE/binding. `maage_content_creative` exposes finalized catalog. `src/base/creative.c` registers twelve built-in tabs through these APIs.

Tab order, entry order and search order are independent of IDs. Entries carry count-one pristine stacks, optional copied tooltips and searchable flag; item tooltip callbacks supply variant text. Search/Inventory are optional singleton roles. Finalization validates stacks/textures, creates ordered views and deduplicates searchable item/variant pairs. Retain IDs through growable registration; no implicit variant enumeration. Icons use item stacks or copied 16×16 textured quads (not necessarily obtainable items). Catalog/tab IDs are not persisted as stacks. See [Creative UI](client-presentation.md#creative-inventory).

### Dimension Providers

`context->dimensions` participates in module ownership, dependency reads, numeric peer identity validation and finalization. Register one namespaced definition with `primary=1` per composition; each other dimension has its own automatically assigned ID. Registration copies names, labels and callbacks, borrowing immutable `context` data until all Worlds and jobs have ended. Capture IDs during composition; do not resolve names in generation or owner ticks.

A definition supplies detached `generate`, numeric `spawn`, optional owner `contact` and optional owner `arrive` callbacks. `contact` returns a copied `maage_dimension_travel` destination/position/kind/delay, or NOOP when there is no contact. `at_spawn` requests the destination's spawn before streaming. `arrive` returns NOOP to defer on unavailable terrain, SUCCESS only after its placement/preflight succeeds, and an originating Status on failure. It must use ordinary World mutations or `maage_world_apply_plan`; it cannot borrow live player, source World or storage state. Provider-owned travel kinds are opaque to Server. `respawn=0` retains local respawn; another finalized numeric ID redirects death respawn.

Skylight, weather, celestial presentation, ambient floor, fog and evaporation policy are copied immutable definition values. Numeric light stays independent of the presentation floor. Fluids opt into evaporation and optional hot-dimension delay/level-step overrides. `maage_world_dimension` exposes the current immutable definition to content callbacks and Client presentation. See [dimension ownership and built-ins](world-and-gameplay.md#dimensions-and-portal-travel).
