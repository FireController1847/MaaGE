# Item entities, harvesting and presentation

This foundation is implemented in source and compiled on macOS ARM64. Functional,
visual and audio behavior still requires programmer verification. No application,
test, benchmark, runtime validation or GitHub Actions was executed.

## Ownership and lifetime

`maage_world` owns a growable `maage_entities` store. Entity instance IDs are
monotonic 32-bit identities, separate from entity type IDs and item/block IDs.
Records carry a numeric type, typed item-stack payload, feet position, previous
authoritative position, velocity in blocks/tick, grounded contact, active age,
pickup delay. Presentation light is sampled from the Client replica. The current entity type is an item; mobs and AI
are outside this change.

The Server advances entities once per unpaused 20 TPS step. The player and items
share swept AABB collision against registered block shapes. Items use a 0.25-block
body, gravity 0.04/tick, drag 0.98, ordinary ground friction 0.588 and ice friction
0.9604. Small floor impacts settle; removing support resumes falling. Water items
sink and receive a bounded current derived from neighboring numeric fluid levels.
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

| Tool tier | Harvest level | Effective speed | Durability |
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

The added resources are coal, diamond, redstone, lapis, emerald, flint, apple,
oak/spruce/birch saplings, snowball and seeds. Sapling growth/placement, crops,
eating, thrown projectiles, enchantments, XP orbs, fire/lava item damage,
smelting, mobs and combat are not implemented by this foundation.
[Crafting](crafting.md) consumes these registered resources through Server-owned containers.

## Storage and publication

The sole metadata encoding is `MGWORLD6`; earlier development worlds are rejected
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

Protocol 8 carries a separate complete near-player entity interest set, independent
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
hotbar, inventory, cursor, Creative pages, hand and dropped entities. Block states
supply face UVs, tint/overlay and shapes; no separate block texture table was added.
The existing first-person box/extruded-sprite generator is reused for world models.
Dropped meshes cache by numeric item/metadata. Position publications only update
transforms/light, not vertex buffers. Stack quantities use one to five visual
copies at block thresholds of 2, 6, 21 and 41 items. Sprite stacks use one to
four thin parallel layers at thresholds 2, 16 and 32.

GUI blocks project three shaded faces into 16x16 slots, including grass overlays,
log ends/bark, species tints and cutout/translucent textures. Tools, resources and
non-cube decorations use nearest-sampled sprites. Existing counts, selection,
hover names, cursor movement/splitting and pagination remain; durability bars and
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

## Compilation and remaining uncertainty

- macOS ARM64 Debug: Client and Server compile/link, including native Metal and
  Vulkan against static MoltenVK.
- macOS ARM64 Release: the same affected applications and primary adapters compile/link.
- Separate headless Release: Server compiles with Client, Launcher, Window and
  Render disabled.
- Client is `0.1.0-alpha.42`; Server is `1.0.0-alpha.14`. Launcher release is unchanged.
- Client linking retains duplicate-library warnings. Windows/Linux native adapters
  are source-updated where needed but were not compiled here. Wayland gameplay
  input remains unsupported.
- Standalone MSL compilation was attempted with `xcrun -sdk macosx metal` and
  failed because the optional Metal Toolchain is missing. The Objective-C Metal
  adapter and embedded source still compile/link; native MSL compilation is
  deferred to the existing runtime path. No component download was attempted.
- Build success does not establish shader runtime compilation, rendering parity,
  physics correctness, audio correctness, leak freedom or visual fidelity.

Main changes are in `include/maage/{entities,items,blocks,gameplay,session_protocol,
local_session,world_store,render,input}.h`, World item/entity/gameplay/storage/player
modules, Server runtime, local Session, Client resources/models/entities/inventory/
viewport, native key mappings and shared/Metal/Vulkan rendering. The existing
shared-renderer consolidation in the starting working tree is a prerequisite of
this integration. Consult the feature commit for the exact file list.

## Programmer-owned manual verification

These are requested actions and expected results, not passed checks. Start with a
new disposable world. Creative pages provide all tools/resources/metadata variants;
select a stack into the cursor, then place it in a hotbar or storage slot. Prepare
blocks/tools in Creative, then switch to Survival through Options.

### Mining and harvesting

- Mine dirt and grass by hand while grounded and dry. Expect 15 and 18 damage
  ticks respectively (0.75 and 0.9 seconds), after acquisition. Items appear in
  the world before collection.
- Mine stone with each tier of pickaxe: expect 23/12/8/6/4 damage ticks for
  wood/stone/iron/diamond/gold. Repeat at substantially different FPS caps; the
  damage-tick timing should remain the same.
- Break stone by hand: expect 150 damage ticks and no cobblestone. Compare axes
  on logs and shovels on dirt/sand/gravel; other tool families should not gain
  those effectiveness bonuses. Cactus should not gain an axe bonus.
- Try wood/gold pickaxes on iron ore, stone on diamond/gold/redstone/emerald,
  and iron on obsidian. Breaking may finish slowly, but normal drops must be
  withheld. Stone can harvest iron/lapis; iron can harvest the higher ores;
  diamond harvests obsidian in 188 damage ticks.
- Mine while airborne, with submerged eyes, and with both penalties. Each
  condition multiplies the ordinary damage-tick duration by five. Flowing and
  still water should both apply the eye penalty.
- Switch target, selected tool or tool damage during mining, then release/repress
  attack. Expect appropriate progress resets and cracks tied to progress.
- Try Survival bedrock, instant plants/torches and Creative destruction. Bedrock
  stays intact in Survival; Creative and zero-hardness blocks break immediately;
  Creative creates no drops and consumes no durability.
- Wear tools through successful mining, including wrong-tool stone/wood actions.
  Expect a decreasing durability bar, final tool removal and the existing break
  sound. Instant ordinary-tool plant destruction should not wear that tool.
- Check every drop-table row above. Mine several gravel/leaf/grass samples to
  observe chance outcomes; an individual sample is not guaranteed to drop.
  Check log/leaf species after mining and after placement with shears/log items.

### Dropped entities and inventory

- Stand beyond collection range and mine a block. Expect one physical drop,
  upward/horizontal impulse, gravity, floor contact, settling, bobbing and spin.
  Hold attack through another block; no repeated drops should come from the
  already destroyed block.
- Drop items onto solid cubes, snow layers, cactus bounds and ice; remove their
  supporting block. Expect shape-aware collision, renewed falling and greater
  sliding on ice. Drop into still and flowing water; expect sinking and a
  current where supported by neighboring fluid levels.
- Walk through a fresh mined drop immediately, then after ten ticks. It should
  remain during the delay, then collect with a pickup sound. Q should drop one;
  Control-Q should drop the selected stack; manually dropped stacks wait forty
  ticks before pickup. Outside-inventory clicks should drop the carried stack
  or one item with right click.
- Put compatible drops close together. Expect merged quantities up to the item
  limit, younger age/longer delay retained, and no merging across metadata or
  damage. Repeat with more than 64 separate drops and a dense pile.
- Fill inventory, then approach drops. Nothing should vanish. Leave exactly one
  matching stack position with limited room; only the fitting quantity should
  collect and the remainder should stay visible. Free an empty slot and collect
  the remainder. Repeat with damaged tools and wood variants.
- Leave a drop in loaded terrain for five active minutes without collecting or
  rejuvenating it through merging. Expect disappearance. Pause halfway through;
  age, movement and presentation animation should freeze until resume.
- Save and Quit with drops present, reopen the same world, then repeat after
  walking far enough to unload/reload their columns. Expect retained quantities,
  IDs/lifetime behavior and positions, without duplicated copies. Dormant time
  should not consume the active despawn timer. Save again after collection to
  ensure collected items do not return.
- Switch worlds and return to Title while drops are visible. Expect immediate
  removal of old-world visuals. Respawn should preserve the same World entities
  and the current inventory-retaining respawn policy.

### Inventory and primary backends

- Inspect grass, dirt, stone, ores, sandstone and all log/leaf species in hotbar,
  Survival inventory, Creative pages and the cursor. Expect shaded isometric
  blocks, correct top/bark/side textures and grass tint confined to its proper
  layers. Check snow/cactus shape and leaf/ice transparency.
- Inspect all tool tiers, shears, resources, saplings, flowers and torches. Expect
  readable sprites with transparent backgrounds, matching names and metadata,
  correct count labels, durability bars and consistent first-person identity.
- **Metal:** select `--renderer=metal` through the Client arguments and perform
  the entity and inventory scenarios above. Check items behind terrain, ice
  through other translucent geometry, sun/moon, foreground hand, GUI overlays,
  resizing/fullscreen and pause/resume under capped and Unlimited FPS.
- **Vulkan:** repeat with `--renderer=vulkan`, on the same saved world and camera
  setup. Expect equivalent authoritative positions, geometry, scale, color,
  textures, alpha behavior and slot alignment. On macOS this exercises MoltenVK;
  Windows/Linux Vulkan still needs native-platform compilation and verification.
- On each backend, repeatedly drop/merge/collect large piles, cross the 64-block
  visibility boundary, save/reopen worlds and switch worlds. Look for lingering
  removed items, disappearing unrelated geometry, growing retained GPU resources,
  invalid synchronization or UI/foreground corruption. Report backend-specific
  artifacts separately; these scenarios have not been run by the agent.

The subsequent [gameplay parity pass](gameplay-parity.md) adds bounded pickup
presentation, shared sprite UV corrections and reconciled visual block breaking.
