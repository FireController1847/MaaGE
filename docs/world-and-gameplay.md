# World and Gameplay

World owns numeric chunks, generation, simulation utilities, CPU meshing and storage. The same authoritative Server runtime owns gameplay, time and persistence for integrated/dedicated sessions; Client replicas neither generate nor save. Public interfaces: `include/maage/{world,generation,world_store,entities,random_ticks,crafting,containers,block_entity}.h`.

## Generation and Streaming

Base registers terrain, ores, features, spawn and biome policies through public APIs. Ocean, hills, taiga, desert, forest and plains have river/frozen-river and sand/cold/stone-beach overlays. Seeded jittered regional sites, climate/relief fields and coordinate warps determine land biomes; independent local bends vary land boundaries without moving ocean coverage. Compact radial blending of regional sites defines continuous shore coordinates, rounded inlets, terrain/ecology blending and a coastal ramp aligned to sea level. Intermittent narrow beaches depend on independent fields, not arbitrary low elevation; ordinary low inland terrain remains land.

Independent warped drainage blends overlapping seeded cell classes into smooth channel contours. Coherent width/depth variation and occasional pools shape gradual banks; valleys widen with relief, carve into the sea and use depth-based flow/frozen margins. Submerged inland channels become river biomes; pre-carve ocean owns mouths. Job-local caches preserve deterministic samples without shared locks. Shallow beds retain soil; deeper beds use gravel over stone. Water-anchored sand/clay/gravel patches preserve underlying foundations, with additional ocean sand attempts and no continuous rim. Current constants/profiles: `src/base/generation*.c`.

Biome-specific flowers, tall plants, trees, mushrooms, ferns, snow and desert vegetation follow ordered features. Reeds generate beside unfrozen water on eligible soil/sand and grow to three blocks; rare pumpkins occupy grass. Tall plants require paired space/support and drop once from the lower half. Generation changes affect new terrain only; use a new development world for continuous terrain.

Ordered stages own detached columns, immutable definitions and coordinate-derived RNG, independent of job completion order. Live saplings reuse tree emitters. Near/forward square-ring streaming does not wait on previous rings; separate I/O workers handle load/save. Owner handoffs and revisions reject obsolete outputs; unavailable terrain obstructs.

16³ sections use adaptive uniform/palette-packed numeric snapshots with copy-on-write sections and separately retained lighting. Representations do not cap registry IDs; snapshots/registries outlive borrowing Jobs. See `src/world/` and [AGENTS.md](../AGENTS.md).

## Persistence and Identity

Worlds live at `instances/<uuid>/worlds/<uuid>/`. Current formats: `MGWRLD16` metadata, one checksummed `MGIDMAP3` dictionary, `MGRGN2` regions, `MGCHNK11` chunks. Saved block/state/item/item-variant/biome/entity-type IDs translate once at open. States/variants use canonical typed assignments, constants once per identity; property/value names resolve at open only. Regions bind dictionary UUID and use numeric records, compression/checksums and codecs selected by owning saved block identity. Publish map extensions before saves; reject missing, malformed or duplicate identities.

Async region writes use immutable snapshots; dirty clears only for the saved revision. After successful flush, metadata checkpoints player/inventory, weather, separate RNG streams, rotating simulation cursor and active/dormant entities (including health/orientation, variants, damage/death, goals, retained effects and flat versioned module state). No pointers or open temporary GUI results. Old development formats fail without migration/deletion. See `world_store.h`, `src/world/{store,region_store}.c`.

## Random Ticks and Growth

At 20 TPS the unpaused owner retries neighbor work, services scheduled updates, random ticks and block entities. Available cascades drain before phase publication. Random tick speed defaults to 3 (zero disables); simulation radius defaults to 8, independent of rendering. Eligible numeric-state counts avoid section scans; coordinate, behavior and weather RNG persist separately. Missing terrain/light defers work without synchronous loading.

Registered behavior includes grass, saplings, leaves, farmland/wheat, cactus, mushrooms, snow and ice. Typed species/stages persist; shared Minecraft contracts permit compatible content. Tree plans preflight required columns before mutation. Natural leaves start unchecked; nearby log/leaf removal marks resident leaves, which check connected logs within four face-adjacent leaf steps. Missing neighbors defer; saved flags persist, generation/loading do not arm checks, and placed leaves persist. Built-in farming currently covers wheat, not other crops or bone meal.

## Block Changes and Directional Support

Reserve exact before/after transitions before expected-state mutation; no-op replacements create none. Owner dispatches CHANGED, destination support, then adjacent receivers in -X,+X,-Y,+Y,-Z,+Z order. NEIGHBOR_CHANGED carries changed position, receiving face, previous/replacement state and origin; the receiving subject is its current state. Detached generation/replica imports never dispatch gameplay.

Callbacks append rather than recurse. Columns rotate fairly while preserving per-column order/delivery bits. `neighbor_update_budget` defaults to 8192 deliveries per drain, not queue capacity. Unavailable inputs leave deliveries pending without loading; retries preserve completed provider cursors if receiver state is unchanged. Deferral must precede effects; unavailable-only retries do not dirty snapshots.

SURVIVES supplies state-aware six-face ALL/ANY predicates; a decisive known failure/success need not await unknown alternatives. SUPPORT_LOST owns effects; CAN_PLACE is separate. Minecraft implements plants, torches, cactus, farmland, crops/mushrooms and falling-block support. Unsupported falling placement is allowed; placement or removal of lower support triggers falling in the ordinary owner cascade. Random sampling, loading and generation do not activate untouched floating sand/gravel. Support reactions are not delayed scheduled ticks.

Deferred transition/origin/face/provider records persist in source column auxiliary snapshots and `MGCHNK11`; light replacement retains current auxiliary state. Resume when relevant terrain is resident. Record size is bounded by encoding/address space, not fixed event count. `maage_world_begin_mutations` / `maage_world_end_mutations` postpone dispatch until mutable entity borrows end; tree plans publish all cells first. Finish scopes before job polling/persistence.

## Entities and Item Drops

One growable World store owns item, falling, living and projectile instances, with monotonic instance IDs distinct from immutable content types. Owner handles swept physics, pickup, age/delay; unavailable terrain freezes entities. Active/dormant entities checkpoint independently of column files.

Reserve all drops before removal, tool wear or RNG changes. Mining, Q/Control-Q and cursor drops create item entities, never instant inventory grants. Pickup retains residual quantities and sends copied cues; the 0.15s Client interpolation is cosmetic. Falling entities retain state and place or convert to items without duplication.

Genuine removals use `maage_world_break` to emit one copied removed-state occurrence on successful replacement. Mining, support loss, decay, displacement and explosions retain their loot/RNG policies while sharing sound/fragments. Falling source transfers and fluid level edits remain ordinary mutations.

## Living Entities and Natural Spawning

Content registers pig, cow, sheep, chicken, zombie, skeleton, creeper, spider and arrow with dimensions, attributes, loot, sounds, models and spawn rules. Owner callbacks compose wandering/panic (initially away from damage), local obstacle steering/jumps, pursuit/LOS, melee, ranged shots and retained explosions. Airborne steering does not require ground under look-ahead. Swept bodies handle gravity, fluids and environmental damage. Sheep own fleece/shearing/grazing, chickens eggs/slow falling, cows bucket milk. Eight spawn eggs use authoritative reach/stack validation.

`maage_entity_policy` configures rotating ticks, spawn attempts/intervals, class populations and distances. Weighted biome/class rules inspect loaded terrain, light, time, surface/fluid and collision. Base passive rules use grass in plains/forest/taiga/hills, light 9–15; hostile rules light 0–7. Death retains transformed loot and cursors across capacity failures/checkpoints; attributed XP credits the player directly (no orbs).

Accepted hits publish sequences; rejected hits do not restart hurt/knockback, stronger resistant hits apply only additional damage. Death stops AI/interactions, retains body for 20 active ticks and removes only after loot/XP finish. Numeric ray/AABB targeting has 0.1-block border and terrain occlusion; entity targeting clears mining. Prediction acknowledgment requires a newer actual owner mutation tied to input sequence.

## Crafting and Furnaces

One finalized recipe registry handles temporary 2×2/3×3 grids. Shaped patterns offset/mirror (not rotate); shapeless ingredients use distinct cells. Exact/wildcard variants, item tags and typed constraints match; property projections compile unique results. Shaped wins, then occupied count/registration order. Matching allocates nothing. Owner validates container/access/range/revision and preflights outputs/remainders; shift crafting is bounded.

Temporary closure returns cursor/inputs to inventory, then reserved overflow entities; failure retains unreturned inputs for retry/checkpoint, not open table/results. Furnaces instead own persistent World block entities, codecs/containers and separate numeric fuel/smelting recipes. Owner cooking is 200 ticks, XP fractional and extraction-only; progress is copied, not a Client timer.

Regular/trapped chests own independent 27-slot halves. Two adjacent same-type chests compose 54 slots; triples or joining an existing pair fail. Different types stay independent. Owner reconciles facing from pair axis/opaque neighbors without moving inventories. Access validates loaded halves, identities, eight-block range and clear tops. Transactions preflight both; closing returns cursor only. Breaking drops that half and closes combined view; survivor keeps inventory/facing. Viewers/lid phase are transient; stacks/revisions persist. Trapped output awaits redstone.

## Creative Inventory Transactions

Use input identifies the predicted published pose. Server retains eight poses, accepts anchors within four ticks/same life, raycasts that pose against current terrain, and validates overlap/inventory against current authority. Expired anchors defer rather than silently retarget.

Session protocol 28 carries catalog/player-slot, clone, hotbar, drop, destroy and clear actions. Owner FIFO validates mode, health, container identity/exact revision, tab/entry scope, stack and action/slot bounds; generic slots cannot bypass Creative policy. `maage_inventory_operate` preflights copied player inventory before commit. Catalog owns no storage; deletion spawns nothing, drops reserve entity capacity first. Creative closure drops carried stack or retains/retries on failure. Survival/persistent containers keep existing ownership.

## Current Limits

Richer navigation/AI, breeding/riding, status effects, complete Survival/combat, XP entities, redstone, dimensions and remote entity encoding/multiplayer remain future work. No exact Minecraft seed or visual parity is promised. See [roadmap](development-roadmap.md) and [AGENTS.md](../AGENTS.md); implementation is not runtime validation.
