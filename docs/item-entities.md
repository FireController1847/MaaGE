# Item entities, harvesting and presentation

## Ownership and lifetime

`maage_world` owns a growable `maage_entities` store. Entity instance IDs are
monotonic 32-bit identities, separate from entity type IDs and item/block IDs.
Records carry a numeric type, typed item-stack payload, feet position, previous
authoritative position, velocity in blocks/tick, grounded contact, active age,
pickup delay. Presentation light is sampled from the Client replica. The current entity type is an item; mobs and AI
are not implemented.

The Server advances entities once per unpaused 20 TPS step. The player and items
share swept AABB collision against registered block shapes. Items use a 0.25-block
body, gravity 0.04/tick, drag 0.98, ordinary ground friction 0.588 and ice friction
0.9604. Floor impacts settle without a physical rebound; removing support resumes falling. Water items
sink with the same gravity and drag as air. Their complete AABB receives the same
normalized 0.014 current impulse as players, including openings below adjacent
non-solid material and downward currents beside falling-water walls. There is no
upward buoyancy or modern floating-item behavior.
Presentation bobbing/spin never feeds back into physics.

Mined drops have a ten-tick pickup delay. Q drops one selected item and Control-Q
drops its stack with a forty-tick delay. Clicking outside the inventory drops the
cursor stack; right click drops one. All use the existing numeric command queue
and authoritative inventory. Collection uses expanded player bounds, fills
compatible stacks before empty slots, and leaves any remainder in the entity.
Collection intersects the body expanded by (1, 0.5, 1) with the item's AABB;
its horizontal center limit is player half-width + 1.125 blocks. There is no
pre-collection magnetic force. A successful step emits one aggregate pickup cue
with a bounded batch of up to 32 copied transfer records. Further transfers wait
for the next step; quantities stay authoritative. The Client interpolates confirmed
transfers toward player feet + 0.5 over 0.15 seconds using a squared phase, with
at most 32 simultaneous decorations. Partial pickups leave the residual entity.
No ownership mutation depends on successful animation or sound playback.

Compatible nearby stacks merge only if their complete combined quantity fits the
item's stack limit. Metadata and damage must match. The survivor keeps the younger
age and longer pickup delay. A reused spatial hash examines at most 128 candidates
per entity search, every five active ticks; rotating insertion priority prevents
a permanently privileged candidate window in dense piles. Simulation work is
linear in retained records plus a bounded candidate budget, rather than all pairs.
Items expire after 6000 active ticks. Allocation grows with content; 64 is only an
initial allocation size. Identity/address-space exhaustion reports backpressure.

An entity whose terrain is unavailable freezes in World ownership. Column eviction
never owns, saves or deletes that record, so crossing/reloading columns cannot
instantiate duplicate copies. Dormant records remain in memory and are included
in the World checkpoint. This intentionally simple design does not page dormant
entity records to separate files or parallelize entity simulation yet.

## Mining and drops

Every current block uses its registered hardness. Tool effectiveness and harvest
eligibility are separate. Stone-family blocks require a pickaxe for drops; wood,
dirt, grass, sand, gravel, cactus and ordinary vegetation do not acquire an
unintended pickaxe requirement. Snow requires a shovel. Negative hardness is
unbreakable in Survival; Creative destruction is immediate and produces no drops.

Progress per damage tick is `speed / (hardness * divisor)`, with divisor 30 for a
harvestable block and 100 otherwise. Airborne and submerged-eye penalties each
divide speed by five and combine. Target/tool changes reset progress. Initial
acquisition of a non-instant block is separate from damage ticks, and the existing
five-tick repeat cooldown follows completed sustained mining. Crack stages use
authoritative progress. There is no render-rate mining multiplier.

Tool damage is valid through the listed maximum, inclusive; the following wear
operation breaks the tool. Pending exhaustion may exceed the consumption
threshold of four after mining and remains saveable in the bounded 0..40 domain.
Charges are capped at 40; one four-point quantum is consumed per owner tick,
with regeneration and later mining charges retained for the following step.

| Tool tier | Harvest level | Effective speed | Maximum damage |
| --- | ---: | ---: | ---: |
| Wooden | 0 | 2 | 59 |
| Stone | 1 | 4 | 131 |
| Iron | 2 | 6 | 250 |
| Diamond | 3 | 8 | 1561 |
| Golden | 0 | 12 | 32 |

All five tiers have pickaxes, shovels and axes. Shears have durability 238, speed
15 on leaves and vegetation-specific harvesting. Ordinary mining-tool durability changes after a successful Survival destruction,
including wrong-tool destruction of nonzero hardness blocks. Breaking a
zero-hardness block does not wear ordinary tools; shears wear on supported
shearable vegetation.

The numeric drop-generation interface supports zero, one or two stack outcomes,
caller-owned randomness, quantities, metadata and tool restrictions. It reserves
all required entity capacity before block mutation. Only a successful
expected-state destruction commits drops/randomness/durability; deferred
allocation leaves the block intact. Drops never enter inventory directly.

| Block | Ordinary eligible Survival result |
| --- | --- |
| Stone | One cobblestone |
| Grass | One dirt |
| Dirt, sand, sandstone, cobblestone, mossy cobblestone, planks, cactus | One corresponding item |
| Iron ore / gold ore | One ore block; stone / iron pickaxe minimum respectively |
| Coal / diamond / emerald ore | One coal / diamond / emerald; wood / iron / iron pickaxe minimum |
| Redstone ore | 4–5 dust; iron pickaxe minimum |
| Lapis ore | 4–8 lapis; stone pickaxe minimum |
| Obsidian | One obsidian; diamond pickaxe |
| Gravel | Flint with probability 1/10, otherwise gravel |
| Logs | One log, retaining oak/spruce/birch species and removing placed axis bits |
| Leaves | Sapling chance 1/20, matching species; independent oak apple chance 1/200 |
| Leaves with shears | One matching leaf item |
| Tall grass | Seed chance 1/8; shears yield the grass item |
| Dead bush | Nothing; shears yield the bush item |
| Flowers, mushrooms, torches | One corresponding item |
| Snow layer | One snowball with a shovel |
| Ice, bedrock, air, fluids | No normal item drop |

Registered resources include coal, diamond, redstone, lapis, emerald, flint, apple,
oak/spruce/birch saplings, snowball and seeds. Sapling growth/placement, crops,
eating, thrown projectiles, enchantments, XP orbs, fire/lava item damage,
smelting, mobs and combat are not implemented currently.
[Crafting](crafting.md) consumes these registered resources through Server-owned containers.

## Storage and publication

The sole metadata encoding is `MGWORLD7`; earlier development worlds are rejected
and must be recreated. Files are never deleted or converted automatically.
`world.bin` atomically checkpoints player/inventory, environment and all active
or dormant item entities after successful World flush. Unreturned temporary
crafting inputs share that checkpoint only when closure cannot complete; see
[Crafting](crafting.md) for ownership and lifecycle. Entity fields are explicitly
encoded: instance ID/type, namespaced item name, quantity, metadata, damage,
position, velocity, age, pickup delay and grounded state. The next instance ID
and drop random state persist. CRC, actual byte lengths, stack validity,
increasing unique IDs and finite coordinate/velocity ranges are checked before
restoring World ownership. No native structure layout or pointers are serialized.
The encoding has an explicit 32-bit file/identity bound and reports overflow.

The Session carries a separate complete near-player entity interest set, independent
of the fixed player snapshot. It contains epoch, tick and sequence identities;
absence removes an entity. The local mailbox holds at most one pending immutable
publication, transfers detached array ownership and swaps pointers under its short
mutex. The Server never lends mutable World storage to the Client. Older epochs,
ticks and sequences are rejected. Arrays and copy work scale with the actual
64-block interest set; there is no fixed entity array or growing FIFO backlog.
Future remote transport can fragment and atomically install the same complete
revision semantics, but remote networking is not implemented.

## Shared presentation

Item definitions resolve model category, texture and metadata consistently for
hotbar, inventory, cursor, Creative catalog, hand and dropped entities. Block states
supply face UVs, tint/overlay and shapes; there is no separate block texture table.
The existing first-person box/extruded-sprite generator is reused for world models.
Dropped meshes cache by numeric item/metadata. Position publications only update
transforms/light, not vertex buffers. Stack quantities use one to five visual
copies at block thresholds of 2, 6, 21 and 41 items. Sprite stacks use one to
four thin parallel layers at thresholds 2, 16 and 32.

GUI blocks project three shaded faces into 16x16 slots, including grass overlays,
log ends/bark, species tints and cutout/translucent textures. Tools, resources and
non-cube decorations use nearest-sampled sprites. Existing counts, selection,
hover names, cursor movement/splitting and catalog scrolling remain; durability bars and
Creative metadata variants are included.

Both primary backends traverse the same portable material plan. Model matrices
and sampled light use the existing uniform packet, copied with Metal constant
bytes or Vulkan push constants. Shared C sorts transformed translucent item
triangles and draws immutable index ranges, preserving scene depth, foreground
items and UI composition. Ice can use several small triangle draws; ordinary
opaque/cutout models retain grouped draws. No new shader attributes, model
uploads per frame or backend-specific item geometry generator was introduced.
OpenGL/software retain their current capabilities and reject optional scene
features they do not implement.

## Limits and Manual Review

Dormant entities remain in memory; there is no separate entity paging, parallel
entity simulation, mob AI, XP, fire damage or complete food/combat system.
Translucent sorting across intersecting meshes remains approximate.

Programmer review should cover tool-tier harvesting, airborne/submerged penalties,
metadata/chance drops, item collision/merging/pickup delay, full inventories,
partial pickups, active-age expiry, dormant save/reopen and world transitions.
Repeat cached item, alpha, depth, count and durability checks on Metal and Vulkan.
Runtime and visual behavior are not established by compilation.
