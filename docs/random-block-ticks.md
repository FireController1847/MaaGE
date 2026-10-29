# Random Block Ticks

`maage_world_tick` runs random ticks on the authoritative Server owner at 20 TPS,
after the bounded scheduled-update phase and inside the same column edit
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
blocks consume an attempt. Section eligibility is checked at section entry, at that point. Neither scheduled-update records nor individual Client messages represent
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
| Leaves | Marked natural leaves propagate log support through up to four face-adjacent leaf cells; supported leaves clear the check flag, unsupported leaves decay with ordinary drops. Player-placed leaves persist. See Leaf Support below. |
| Snow layer | Melts to air when saved block light exceeds 11. Full snow blocks do not melt. |
| Ice | Melts to source water when saved block light exceeds `11 - light_opacity` (8 for built-in ice); normal fluid scheduling follows. |

Grass uses daylight/weather-subtracted sky light combined with block light,
including five-neighbor brightness sampling for applicable cover.
Mushrooms use full saved light, and melting uses block light only. Queries defer
when lighting is unavailable or an earlier opacity/emission edit invalidates it;
missing light is never treated as darkness. Cross-column destinations and all
required neighborhoods must be loaded. No sample triggers synchronous relighting.

Immediate plant/cactus support reactions now use the persistent neighbor-update
queue described in [Client UI and Visual World](gameplay-parity.md). Crops/sapling growth, lava
fire ignition, weather-driven snow accumulation/freezing and Nether-specific ice
behavior remain unsupported. Mycelium and podzol are absent from the catalog, so their mushroom
light exceptions do not apply. Dirt and full snow blocks have no random callback.
Lava, flowing water and oriented torches retain registered sampling
eligibility even where the callback is absent. Lava lacks fire, fluid flow
remains exclusively scheduled, and torches already have initialized orientation
metadata. Unsupported callbacks are not fabricated.

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
and must be recreated; no migration or automatic deletion is performed. Chunk files independently persist scheduled fluid/support work.
Resume restores stream positions; it does not guarantee
identical gameplay traces when different streaming/lighting availability changes
which samples can execute. Coordinate traversal itself does not depend on hash,
cache insertion or worker-completion order.

## Leaf Support

Leaf metadata uses species bits 0..1 (oak 0, spruce 1, birch 2), persistence bit
4, and pending-check bit 8. The immutable registry contains all twelve combinations:

| Metadata flags | Oak / spruce / birch state IDs | Meaning |
| --- | --- | --- |
| 0 | 18 / 274 / 275 | Natural, already checked |
| 4 | 576 / 577 / 578 | Persistent player placement |
| 8 | 579 / 580 / 581 | Natural, awaiting support evaluation |
| 12 | 582 / 583 / 584 | Persistent with check bit retained; never decays |

`maage_block_leaves_state` maps metadata directly to numeric states. Player item
placement sets bit 4. Check-bit mutations
preserve species, textures and biome tint. All variants remain eligible for the
same section sampler; persistent and already-checked leaves return NOOP.

### Propagation

`maage_leaves_changed` participates in the existing owner mutation batch. Replacing
a log with a non-log marks natural leaves in its radius-4 cube. Removing a leaf
marks natural leaves in its radius-1 cube, including diagonals. Already-pending
and persistent leaves are skipped. Flag-only mutations cannot recurse into removal
propagation; repeated removals reuse one private edit per touched column.

On a random opportunity, marked natural leaves first require the radius-5 column
neighborhood to be loaded. The callback samples the 9x9x9 radius-4 cube, seeds all
logs as support distance zero, and propagates through six face-adjacent leaf cells
for at most four edges. An isolated nearby log cannot support disconnected leaves.
All supported log orientations/species qualify, and persistent leaves can carry
support through a canopy. Vertical coordinates outside 0..255 are known air.

A connected center clears bit 8. An unsupported center reserves ordinary item
drops and changes to air through expected-state `maage_world_set`. Actual removal
uses normal lighting/mesh invalidation and triggers adjacent leaf rechecks.
The temporary search uses 729 signed bytes and 729 short queue entries on the
owner's stack; it scans no whole chunk and allocates nothing for the search.

If any required column is unavailable, the callback retains its check bit and
returns NOOP without consuming behavior RNG or loading terrain. Removal marks only
currently loaded destinations. Detached load/generation output marks natural leaves
for rechecking during the existing section-count initialization pass. This bounded
import step catches support changes while a destination was absent, without a
persistent removal-event queue, synchronous loads, or distant generation. Persistent
states are preserved. Rechecked imported columns become dirty for normal saving.

Flag-only owner edits save new authoritative states while preserving Server
geometry/light epochs. An accepted asynchronous lighting result merges current
authoritative state arrays and auxiliary data into its private output before
publication, retaining current leaf flags with the worker's computed light.

### Decay Drops

Natural decay reuses `maage_item_generate_drops` with no tool: each supported
species has a 1/20 chance of one corresponding sapling; oak independently has a
1/200 chance of one apple. Both may drop. No quantities go directly to inventory.
Leaf-decay drop draws derive from the random-tick behavior stream; the existing
player/item-entity RNG remains separate. Capacity is reserved before removal, and
spawns use the existing item-entity store. Ordered owner mutations and expected
state checks prevent a mined leaf from also decaying into a second drop.

Player breaking retains existing shears behavior (one species-matching leaf item)
and ordinary drop probabilities. Persistence flags affect natural decay, not the
ordinary breaking drop table. Fortune and Silk Touch remain unsupported; there is no enchantment system. No jungle leaves/species are registered.

## Persistence and Review

The seed domains are `0x41c64e6d` and `0x85ebca6b`; a new zero behavior seed
normalizes to one. Resume never reseeds either saved stream. `MGWORLD6` stores
both words before its CRC, atomically with player, entities and environment.
Leaf states use the namespaced block/metadata chunk palette.

Programmer review should cover speed zero, pause/resume, growth/light thresholds,
connected versus disconnected leaf paths, placed leaves, unloaded boundaries,
ordinary/shears/decay drops and save/reopen continuation. Sampling order and
persistence contracts do not imply identical gameplay under different terrain
availability. Runtime verification remains pending.
