# World and Gameplay

World owns numeric content, chunks, generation, simulation utilities, CPU meshing and storage; the authoritative Server owns gameplay, time and persistence sequencing. Integrated and dedicated sessions use the same runtime. Client replicas neither generate nor save. Public interfaces: `include/maage/{world,generation,world_store,entities,random_ticks,crafting,containers,block_entity}.h`.

## Generation and Streaming

The Base module registers terrain, ore, feature, spawn and biome policies through public interfaces. Current biome categories are ocean, hills, taiga, desert, forest and plains. Regional sites use a seeded 128-block lattice with bounded jitter, climate/relief fields, local 3x3 classification support and distance-based edge blending. Materials remain categorical at the surface; relief can inherit neighboring climate for ecology. The generator is independent, without Java seed parity or a guarantee that every component has a minimum size.

Generation stages operate on detached columns, using immutable definitions and coordinate-derived randomness. Ore/feature callbacks execute at stable stage boundaries; worker completion order never changes feature ownership. Live saplings reuse the tree feature emitter. Square-ring World scheduling prioritizes near-player and forward missing terrain without waiting for previous rings. Disk loads/durable saves use a different worker pool from CPU generation, light and meshing. Short owner handoffs and revisions reject obsolete completions; unavailable collision columns remain obstructed.

A 16³ section holds numeric states. Published snapshots use adaptive uniform/palette-packed representations; private block mutations detach affected sections and lighting has separate reference-counted storage. These are memory representations, not content-ID ceilings. Borrowed immutable snapshots and registries outlive their jobs. The current algorithms are in `src/world/` and `src/base/generation*.c`.

## Persistence and Identity

Worlds reside under `instances/<uuid>/worlds/<uuid>/`. Current formats: `MGWRLD12` metadata with one checksummed `MGIDMAP1` identity dictionary, `MGRGN2` region files and `MGCHNK9` chunk records. The dictionary maps saved numeric block/state/item/biome IDs to current finalized definitions at open; saved states reference block identity plus metadata. Persisted chunk/region records contain numeric IDs only, compression/checksums and registered block-entity codecs selected by owning saved block identity. Missing, malformed or duplicate identities fail explicitly.

Owner edits create immutable save snapshots; async region writers clear dirty state only when the saved revision remains current. Metadata checkpoints player/inventory, weather/environment, RNG and retained active/dormant entities after a successful World flush. No open temporary GUI results or pointers are stored. Older development formats have no migration or automatic deletion. Details: `include/maage/world_store.h`, `src/world/{store,region_store}.c`.

## Random Ticks and Growth

At 20 TPS the unpaused owner services persistent bounded scheduled fluid/support updates and then random ticks. `maage_world_config_init` defaults `random_tick_speed` to 3; explicit zero disables it. Simulation distance defaults to radius 8 independently of Client render distance. Only loaded sections with eligible numeric states participate; per-section counts avoid repeated whole-block scans. Coordinate, behavior and weather RNG streams persist separately. Unavailable terrain/light defers mutations without synchronous loads or relighting.

Registered random behaviors include grass, saplings, leaves, farmland/wheat, cactus, mushrooms, ordinary vegetation support, snow layers and ice. Sapling stage/species and eight wheat stages persist as numeric metadata. Sapling growth preflights a complete detached tree plan across required columns before commit. Natural leaves propagate connected-log support for at most four face-adjacent leaf steps; their check flags survive missing neighbors. Player-placed leaves persist. Support changes queue deduplicated neighbor work, retaining overflow/unknown-neighbor tasks. Existing farming currently covers wheat, not other crops or bone meal.

## Entities and Item Drops

World owns growable typed entities with monotonic instance IDs distinct from content IDs. Current types are items and falling blocks, not mobs. The Server owns swept collision, pickup, active age and delay; entities freeze when required terrain is unavailable. Dormant and active entities belong to the World checkpoint rather than per-column files.

Mining reserves capacity for every drop before expected-state removal/tool wear/RNG changes. Drops become World item entities, never instant inventory grants. Q, Control-Q and cursor drops follow the same owner path. Pickup fills existing compatible stacks, keeps residual quantities in their entities and publishes copy-only cues. The Client's 0.15-second pickup interpolation is decorative; there is no gameplay magnet. Sand/gravel transfer into owned falling-block entities and either place saved state at landing or convert to item ownership without duplicating quantities. See `include/maage/entities.h` and `src/world/entities.c`.

## Crafting and Furnaces

One finalized growable recipe registry serves temporary 2x2 inventory and 3x3 table grids. Shaped patterns offset/mirror but do not rotate; shapeless ingredients occupy distinct cells. Metadata is exact unless wildcarded. Shaped wins over shapeless, then occupied count and registration order. Matching allocates nothing. The Server validates numeric container identity, access/range, slot policy and result revision before preflighting all inputs, outputs and remainders. Bounded shift-click repetition does not permit infinite crafting loops.

Crafting Table storage is temporary: close returns cursor/inputs to inventory and reserves item entities for overflow. Failed closure retains unreturned stacks for retry/checkpoint; the open table and derived result are not saved. Furnaces instead use persistent World-owned block entities, registered codecs and containers. Separate numeric smelting/fuel registries drive 200-step owner cooking, snapshot progress and extraction-only fractional XP. GUI progress draws integer steps from authoritative copies, not Client timers. See `include/maage/{crafting,smelting,furnace,containers,block_entity}.h`.

## Creative Inventory Transactions

Session protocol 23 replaces the full-cursor Creative pick with typed catalog/player-slot, clone, hotbar, drop, destroy and clear actions. The Server validates Creative mode, health, the accessible personal container's identity and exact revision, registered tab/entry scope, item/metadata/damage/count validity and slot/action bounds. Relative operations run in owner FIFO order; stale commands fail explicitly. Generic personal slot commands cannot bypass Creative policy.

Catalog selection owns no storage. Shared `maage_inventory_operate` transactions preflight player-slot changes in a copied inventory before commit, including distribution, collection, hotbar swaps and bounded transfers. Cursor and selected-slot ownership remain in the existing inventory/checkpoint. Deletion creates no entities; drops reserve World entity capacity before ownership changes. Creative closure drops its carried stack before normal container teardown, retaining it and retrying when reservation fails. Survival crafting/table/furnace closure and persistent block-entity ownership use their existing paths.

## Current Limits

MaaGE does not yet implement general mobs/AI, complete combat/Survival, redstone, dimensions or remote multiplayer. Neither generator nor presentation promises exact Minecraft 1.7.10 parity. See [the roadmap](development-roadmap.md) for plans and [AGENTS.md](../AGENTS.md) for durable rules.
