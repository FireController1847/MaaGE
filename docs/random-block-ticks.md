# Random Block Ticks

`maage_world_tick` runs random ticks on the authoritative Server owner at 20 TPS,
after the existing bounded scheduled-fluid phase and inside the same column edit
batch. Pause stops both simulation phases; the independent streaming pump continues.

Initialize `maage_world_config` with `maage_world_config_init`. Its
`unsigned int random_tick_speed` defaults to **3**; assigning **0** disables random
ticks without a default-value fallback. **1** reduces opportunities; larger values
increase attempts, not guaranteed growth. The owner-thread
`maage_world_set_random_tick_speed` changes that same setting at runtime. There is
no menu, CLI argument or console rule. The setting is Server configuration, rather
than saved world data. Creation/the setter reject a worst-case aggregate attempt
count above `UINT_MAX` with `BUFFER_OVERFLOW`, instead of wrapping or truncating.
Accepted rates still consume CPU time proportional to the requested attempts.

## Selection and Ownership

The active region is the square around the authoritative player's column selected
by `simulation_distance` (default 8), independent of Client render distance. The
existing asynchronous streamer covers the larger simulation/presentation footprint;
only presentation columns need meshes. Random sampling never loads a column and
never includes distant retained cache entries. Unavailable columns are skipped.

Columns are visited in stable Z/X order, sections from bottom to top. A section
participates only when it exists and contains at least one numeric state marked
`random_ticks`. A registered `maage_block_definition.random_tick` callback supplies
the behavior. Counts are derived on the load/
generation worker before publication, copied with snapshots, and adjusted in O(1)
by owner mutations. There is no per-tick block scan or sampling allocation.

For each attempt, the persisted 32-bit coordinate state advances by multiplication
by 3 and addition of 1013904223, modulo 2^32. After shifting right by 2, bits 0..3
select X, 8..11 select Z and 16..19 select section-local Y. Untickable sampled
blocks consume an attempt. Section eligibility is checked at section entry, as in
1.7.10. Neither scheduled-update records nor individual Client messages represent
these attempts. The last active tick's attempt/callback counts are copied in World
statistics.

Callbacks borrow immutable numeric definitions and a World-owned random context.
They mutate only through expected-state `maage_world_set`; each touched column
gets one private edit and normal revision/lighting/mesh publication. Unchanged
samples publish nothing. Actual changes retain the existing snapshot allocations;
sampling itself allocates nothing. No global lock, GPU work, worker mutation,
string lookup or job wait is introduced.

## Supported Behaviors

| Block | Random-tick behavior |
| --- | --- |
| Grass | Decays to dirt when light above is below 4 and cover opacity exceeds 2. With source light at least 9, makes four spread attempts: X/Z offsets -1..1, Y offsets -3..1. Destination must be metadata-zero dirt, with light above at least 4 and opacity at most 2. No tall grass is created. |
| Cactus | An exposed top shorter than three blocks increments age 0..15; a subsequent tick at age 15 grows one block and resets the old top. New-growth side obstruction produces the normal item drop after capacity preflight. Ages are numeric registered states and persist in chunk palettes. |
| Brown/red mushrooms | One-in-25 spread chance; stops with five of the same mushroom in the 9x3x9 density neighborhood. Four candidate-walk steps precede one final placement attempt. Current supported substrates require an opaque top and full saved light below 13. |
| Flowers, tall grass, dead bush | Random substrate check; plants without valid support disappear with their ordinary drops (including the 1/8 tall-grass seed chance and no ordinary dead-bush drop). This does not spawn vegetation. |
| Leaves | Marked natural leaves propagate log support through up to four face-adjacent leaf cells; supported leaves clear the check flag, unsupported leaves decay with ordinary drops. Player-placed leaves persist. See [Leaf Decay](leaf-decay.md). |
| Snow layer | Melts to air when saved block light exceeds 11. Full snow blocks do not melt. |
| Ice | Melts to source water when saved block light exceeds `11 - light_opacity` (8 for built-in ice); normal fluid scheduling follows. |

Grass uses daylight/weather-subtracted sky light combined with block light,
including the reference's five-neighbor brightness rule for applicable cover.
Mushrooms use full saved light, and melting uses block light only. Queries defer
when lighting is unavailable or an earlier opacity/emission edit invalidates it;
missing light is never treated as darkness. Cross-column destinations and all
required neighborhoods must be loaded. No sample triggers synchronous relighting.

Immediate plant/cactus support reactions now use the persistent neighbor-update
queue described in [Client Parity](gameplay-parity.md). Crops/sapling growth, lava
fire ignition, weather-driven snow accumulation/freezing and Nether-specific ice
behavior remain unsupported. Mycelium and podzol are absent from the catalog, so their mushroom
light exceptions do not apply. Dirt and full snow blocks have no random callback.
Lava, flowing water and oriented torches retain reference-derived sampling
eligibility even where the callback is absent. Lava lacks fire, fluid flow
remains exclusively scheduled, and the
registered torches already have orientation metadata (the reference's random
callback only initializes metadata zero). No unsupported behavior is fabricated.

## Randomness and Persistence

World owns separate seeded coordinate and behavior streams. Behavior draws use
nonzero 32-bit xorshift with rejection sampling for uniform bounded choices;
Java RNG/seed parity is not a MaaGE contract. Weather keeps its original RNG and
draw sequence. No process-global random source, render clock, worker count or
visual option supplies randomness.

`maage_random_tick_state` belongs to World, independently of
`maage_environment_state`. Distinct seed-mixing domains initialize selection and
behavior; supplied saved states are copied unchanged. Weather initialization and
its draw sequence are unchanged. Both random-tick words are explicitly encoded
as four little-endian bytes after the environment payload in the sole `MGWORLD6`
metadata encoding, before the CRC. Zero is valid for the coordinate LCG; a saved
zero behavior/weather xorshift state or a word above 32 bits is rejected.
The same atomic world checkpoint contains player, entities, weather and random
block-tick progression. Previous development worlds are rejected
and must be recreated; no migration or automatic deletion is performed. Chunk
encoding is unchanged. The copied local Session snapshot contract is version 10.
Resume restores stream positions; it does not guarantee
identical gameplay traces when different streaming/lighting availability changes
which samples can execute. Coordinate traversal itself does not depend on hash,
cache insertion or worker-completion order.

## Reference and Validation

The supplied local Forge `unpacked` tree contains Forge/FML support sources. The
adjacent cached `forgeSrc-1.7.10-10.13.4.1614-1.7.10-sources.jar` supplies the inspected
Minecraft classes: `WorldServer.func_147456_g`/`func_152379_p`,
`World.setActivePlayerChunksAndCheckLight`, `World.getBlockLightValue_do`,
`World.getFullBlockLightValue`, `World.calculateSkylightSubtracted`,
`World.getSunBrightnessFactor`, `Chunk.getBlockLightValue`,
`ExtendedBlockStorage.func_150818_a`/`getNeedsRandomTick`/`removeInvalidBlocks`,
`Block.setTickRandomly`/`getTickRandomly`/`registerBlocks`, the relevant material
properties, and `updateTick` on `BlockGrass`, `BlockCactus`, `BlockMushroom`,
`BlockSnow`, `BlockIce`, `BlockLeaves` and `BlockBush`; the Forge patches restore
the vanilla `BlockBush`/`BlockTallGrass` substrate checks. `BlockDeadBush`,
`BlockTorch`, `BlockRedstoneOre`, `BlockDirt` and `GameRules`
were also inspected. 1.7.10 hardcodes three section attempts and has no
`randomTickSpeed` gamerule; MaaGE makes that rate configurable.

macOS ARM64 Debug/Release World, Server and Client compiled/linked, including the
Metal/Vulkan Client configuration. A separate Release headless World/Server build
also passed. Client linking reported duplicate-library warnings. Windows/Linux
were not compiled. No applications, tests, benchmarks or Actions ran; runtime and
visual validation remain with the programmer.
