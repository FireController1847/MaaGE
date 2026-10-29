# Leaf Decay and Random-Tick Checkpoints

The existing Server-owned random-tick sampler drives leaf decay alongside grass,
cactus and other supported behavior. Default speed remains 3; speed 0 performs no
random samples or decay, and advances neither random-tick stream. Pause keeps
the existing streaming pump alive while stopping simulation ticks.

## Numeric Leaf States

Leaf metadata uses species bits 0..1 (oak 0, spruce 1, birch 2), persistence bit
4, and pending-check bit 8. The immutable registry contains all twelve combinations:

| Metadata flags | Oak / spruce / birch state IDs | Meaning |
| --- | --- | --- |
| 0 | 18 / 274 / 275 | Natural, already checked |
| 4 | 576 / 577 / 578 | Persistent player placement |
| 8 | 579 / 580 / 581 | Natural, awaiting support evaluation |
| 12 | 582 / 583 / 584 | Persistent with check bit retained; never decays |

`maage_block_leaves_state` maps metadata directly to numeric states. Player item
placement sets bit 4, matching `ItemLeaves.getMetadata`. Check-bit mutations
preserve species, textures and biome tint. All variants remain eligible for the
same section sampler; persistent and already-checked leaves return NOOP.

## Support and Propagation

`maage_leaves_changed` participates in the existing owner mutation batch. Replacing
a log with a non-log marks natural leaves in its radius-4 cube. Removing a leaf
marks natural leaves in its radius-1 cube, including diagonals. Already-pending
and persistent leaves are skipped. Flag-only mutations cannot recurse into removal
propagation; repeated removals reuse one private edit per touched column.

On a random opportunity, marked natural leaves first require the radius-5 column
neighborhood to be loaded. The callback samples the 9x9x9 radius-4 cube, seeds all
logs as support distance zero, and propagates through six face-adjacent leaf cells
for at most four edges. The bounded multi-source BFS is equivalent to the original
four propagation passes; an isolated nearby log cannot support disconnected leaves.
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

Flag-only edits save new authoritative states while preserving geometry/light
epochs and GPU resources. An accepted asynchronous lighting result merges current
authoritative state arrays and auxiliary data into its private output before
publication, retaining current leaf flags with the worker's computed light.

## Drops

Natural decay reuses `maage_item_generate_drops` with no tool: each supported
species has a 1/20 chance of one corresponding sapling; oak independently has a
1/200 chance of one apple. Both may drop. No quantities go directly to inventory.
Leaf-decay drop draws derive from the random-tick behavior stream; the existing
player/item-entity RNG remains separate. Capacity is reserved before removal, and
spawns use the existing item-entity store. Ordered owner mutations and expected
state checks prevent a mined leaf from also decaying into a second drop.

Player breaking retains existing shears behavior (one species-matching leaf item)
and ordinary drop probabilities. Persistence flags affect natural decay, not the
ordinary breaking drop table. Fortune and Silk Touch remain unsupported; this
change adds no enchantment system. No jungle leaves/species are registered.

## RNG Ownership and Encoding

`maage_environment_state.random` remains the original weather xorshift word, with
unchanged seed initialization and draw sequence. World separately owns
`maage_random_tick_state.selection` (the coordinate LCG) and `.random` (behavior
xorshift). New state mixes both seed words using distinct domains `0x41c64e6d`
and `0x85ebca6b`; zero initial behavior state normalizes to one. Saved behavior or
weather zero states and words outside 32 bits are rejected; coordinate zero is
valid. Resume copies saved words without reseeding.

`MGWORLD6` is the sole metadata encoding. After the environment payload, selection
and behavior are each written as four little-endian bytes, followed by the existing
CRC. Weather's word remains in the environment payload. Player, item entities,
environment and random-tick state share one atomic metadata replacement after
successful World flush. Leaf flags use existing namespaced block/metadata chunk
palettes; the chunk encoding remains unchanged. Local Session protocol is 10
because its copied environment structure changed.

Earlier metadata is rejected with a dedicated Operation Status detail and a clear
incompatible-development-format message in the world list and startup log. Worlds
must be recreated; no alternate decoder, migration, automatic reset or deletion is
provided. Deterministic continuation requires the same saved state, ticks and
active simulation inputs; different loading histories need not produce identical
gameplay, and Java RNG/seed parity is not promised.

## Reference and Compilation

The supplied local Forge `unpacked` sources/patches and adjacent cached
`forgeSrc-1.7.10-10.13.4.1614-1.7.10-sources.jar` were inspected: `BlockLeaves`
`breakBlock`, `updateTick`, `removeLeaves`, `beginLeavesDecay`, `getDrops`,
`damageDropped`; `BlockOldLeaf.func_150123_b`/`func_150124_c`; `BlockLog.breakBlock`;
`BlockLeavesBase`; `ItemLeaves.getMetadata`; `WorldServer.func_147456_g`;
`Chunk.getBlockStorageArray`; `ExtendedBlockStorage.getNeedsRandomTick`;
`WorldInfo.updateTagCompound`; and `AnvilChunkLoader.writeChunkToNBT`.

macOS ARM64 Debug/Release World, Server and Client and a separate Release headless
World/Server configuration compiled/linked. Metal and Vulkan were enabled in the
Client configurations. Client linking reported duplicate-library warnings.
Windows/Linux were not compiled. No tests, benchmarks, applications or Actions
ran; compilation does not establish runtime or visual correctness.
