# World Generation

The Server generates detached columns through ordered terrain, cave, underground,
surface, vegetation and structure stages. Integrated and dedicated sessions use
the same generator. Client biome color blending is a separate presentation setting
and cannot change generated blocks, regional climate or feature choices.

## Registration and Ownership

`MaaGE::World` supplies detached columns, numeric registries, seed/policy services,
Jobs, publication and persistence. The statically linked `MaaGE::Base` module
registers the current generator/spawn providers and owns the terrain sampler,
biome ecology, trees, structures and decoration choices. Biome ecology is copied
opaque registration data; generic biome lookup does not interpret its layout.

`maage_generation_ore_register` copies individually named numeric ore parameters,
including a stable coordinate-RNG salt and optional density callback. The base
provider preserves regional-owner order and its existing distributions while
reading that registry. `maage_generation_feature_register` adds deterministic
callbacks at existing stage boundaries. Workers borrow finalized definitions and
mutate only their detached output; callback contexts are immutable and must outlive
jobs. Registration order determines extension ordering, never job completion order.

## Regions and Terrain

One seeded sampler classifies ocean, hills, taiga, desert, forest and plains from
low-frequency continental, relief, temperature and rainfall fields. Inland region
seeds occupy a 128-block lattice with bounded positional jitter of 24 blocks per
axis. Their biome and underlying climate come from those environmental fields,
rather than independent random biome assignments.

Each seed checks an immutable 3x3 neighborhood of raw classifications. A class
with fewer than three votes yields only when a replacement has at least five.
The check also applies to underlying climate; junctions and supported small regions
remain. It is one bounded support check, not an iterative flood-fill or a global
connected-component minimum. The lattice and bounded jitter give seeds spatial
extent, so intersecting environmental thresholds cannot directly create an
arbitrarily thin categorical strip at individual columns.

Compact neighboring seed influences are summed by biome, and the strongest group
selects one discrete regional identity. Combining seeds of the same biome rounds
their cell joins instead of exposing individual nearest-seed vertices. The broad
quintic distance-gap window rounds junctions over tens of blocks rather than a
narrow band around seed-cell edges, without introducing per-column randomness.
Compact support preserves pure seed interiors. A shared coordinate deformation adds broad
terrain-related shape and smaller eight-block edge detail. The same regional geometry
drives surface identity and ecological influences; materials and individual features
do not receive separate perturbations. Sampling uses World coordinates, including
on opposite sides of chunk boundaries, without borrowing loaded neighbors.

The continental field retains its undeformed coordinates, preserving its geographic
boundary. The four corners of the existing 128-block terrain lattice also retain
their undeformed classification. Base height, variation and terrain/detail noise
retain the existing interpolated height function; regional detail does not move
terrain-height anchors or quantize biome identities into square cells.

Categorical surfaces use the selected biome's own surface/filler pair. That identity
is stored in the chunk for gameplay, persistence and Client tint sampling. No random rank
chooses neighboring surface materials. A desert therefore keeps its sand profile
throughout its region, while neighboring grassland keeps its registered profile.
Sand retains sandstone backing; submerged non-sandy ground retains gravel.
Terrain height and sea-level water fill still determine the physical waterline.

The current selector chooses six built-in categories. Registering another biome
does not itself add a selection rule. IDs remain discrete, and registry definitions
are immutable after finalization; ecological interpolation never modifies them.

## Transition Ecology

Compact quintic weights use the squared-distance gap from the nearest seed.
Gaps at least `128 * 64` square blocks contribute nothing (roughly 32 blocks
from an ordinary two-site bisector on either side); the local seed geometry
therefore controls the transition's physical width. Same-profile neighbors do not
change the environment at internal seed-cell edges. Multiple neighbors can contribute
at junctions, while region interiors retain a pure profile. Surface/filler selection
always stays categorical. Coastal influences retain the original 0.02-noise-unit
continental margin, and never move the geographic ocean boundary.

Relief retains its registered surface, filler, decoration densities and ore/lake
weights. Its effective temperature is the lower of its own and its underlying
regional climate; effective rainfall is the higher. Where that climate has a tree
mixture, relief uses those species proportions at its own sparse tree density.
Cold-region hills therefore stay cold and retain conifers instead of replacing
taiga climate with a warmer Hills profile. Registered definitions remain unchanged;
this is contextual generation influence, not new biome IDs or a second generator.

Temperature, rainfall and decoration probabilities interpolate only within these
transition regions. Forest tree density can taper toward plains, and species
probabilities can shift toward conifers at taiga boundaries. The existing altitude
lapse of one temperature unit per 600 blocks above Y=64 and cold threshold of 0.15
remain. One seeded 16-block-scale field adds a small equivalent-elevation offset
with amplitude four blocks above Y=64. It varies marginal snow contours without
random snow coverage or a separate decision for each feature. Snow, exposed source
sea-water freezing and cold-sensitive plants use the same temperature query.
Ground snow requires precipitation and supporting ground; it is placed on the
final exposed surface after trees. Each generated tree takes one snow decision
from its root's shared climate at crown elevation. Exposed canopy cells retain that
feature decision across biome and chunk boundaries instead of resampling climate
independently for every branch column. Overlapping trees retain the decision of the
feature that supplied the final exposed block, including equal-state writes that
leave the existing block intact. Ice does not support a generated snow layer.
Ordinary owner light-based melting remains active; subsequent gameplay is not
forced to preserve a generated tree's initial snow coverage.

These are generation-time decisions. Runtime weather observations use saved
discrete biome identities. Weather-driven accumulation/freezing is not implemented.

## Feature Placement

`maage_biome_definition.decoration` contains oak/spruce/birch tree probabilities,
grass, flower, dead-bush, cactus and mushroom densities, plus emerald and lake
attempt weights. Each value is finite in 0..1, tree probabilities sum to at most
one, and zero disables a feature in that biome's profile. Registration callers
initialize every property; finalization rejects invalid values. The aquatic trait
is a validated boolean. Existing registered properties are not retuned by region
selection or transition sampling.

Trees retain jittered eight-block candidates and the regional species probabilities.
Ordinary feature randomness selects individual plants after the shared environment
and actual numeric floor traits establish eligibility. It never chooses regional
materials or injects a separate vegetation-cover field. Trees/grass/flowers require
grass or dirt; dead bushes require dirt or sand, and cacti require sand, horizontal
clearance and coordinate-priority spacing. Mushrooms require opaque ground and
an overhead canopy. Shared tree geometry still serves live sapling growth.

Emerald and small underground basin eligibility use the same boundary influences.
Unrestricted ore bands, cave paths and dungeon placement remain independent of
biome decoration.

## Parallel Generation and Outward Presentation

The World scheduler admits independent jobs through one progressing square-ring
spiral, without waiting for earlier rings to finish. Completed loads, generation,
lighting and changed neighbor inputs immediately reopen the closest eligible ring
for its next job rather than waiting for a full scan around the view. Missing
blocks in the forward half-plane (plus the immediate collision halo) take
admission priority over new disk reads/generation behind the player. The rear
initial loads are deferred until the current requested forward range has blocks;
already loaded rear data remains resident and can still receive gameplay/light
updates. Completed column transfers favor the forward direction as well.
Accepted jobs are prioritized by facing hemisphere, then distance; changing
the facing octant reprioritizes pending jobs without cancelling running work.
Client CPU jobs and completed GPU mesh uploads also favor ready forward
geometry, with the one-chunk collision halo and existing fairness retained. Authoritative disk loads and durable saves run on a
separate I/O worker pool, so blocking filesystem operations never occupy a
generation/lighting/meshing CPU worker. Both pools share bounded owner-managed
task contexts, immutable snapshots and completion handling; shutdown drains
both before closing the world store. Asynchronous saves retain reserved task
contexts and oldest-job fairness at background priority. Independent load,
generation, lighting and mesh jobs execute in parallel, including farther work
while nearby jobs are still running.
Server columns transfer whenever their independent load/generation finishes;
there is no initial packet ring barrier. Local Session transport retains
per-column acknowledged revision identities and prioritizes nearer packets, while far columns
can be received, lit, and meshed before their first visible turn. Initial
block/biome transfer does not wait for Server lighting. The default cache policy reclaims previously viewed Server columns on
render-distance reductions through ordered Client unloads. Explicit
RETAIN_ON_SHRINK instead preserves the previous radius for fast re-expansion.
Neither residency policy limits the world's logical size, and dirty unsaved
Server columns remain resident. Client capacity remains large enough to
accept queued transport packets while ordered unloads are processed. The Server streams new/changed chunks only
within the *current* view radius, while retaining acknowledged revision identities
just outside it so later expansion does not repeat the initial transfer. Cached chunks are
hidden outside the view. On movement, stale acknowledged Server columns receive
regular unload publication opportunities alongside new front-facing terrain,
and eligible old columns retire as soon as their unload is acknowledged. The
Client drops off-screen CPU source meshes and native GPU geometry as soon as
they leave the visual radius plus a one-column margin, without waiting for the
transport's ordered unload; the data replica can remain until the unload arrives.

The Client freely uploads completed meshes to its GPU cache ahead of first
display. Its first mesh waits for stable local lighting, not neighboring
columns to finish light jobs; immutable halo snapshots and epochs reject stale
geometry, and later arrivals trigger normal refinement.

First presentation uses eight independent directional sectors around the
player rather than a global one-ring barrier. Each sector tracks complete
two-ring groups, allowing four groups (up to eight rings of lookahead) to
become visible whenever individual GPU meshes are ready. An unfinished
chunk delays only future groups in its own sector; other sectors and chunks
within the four active groups can appear immediately. Existing presented
geometry remains visible and can update independently. Each sector's
frontier rebases across already-presented cached geometry after player
movement or render-distance changes, and missing columns reopen only their
own two-ring group. Submission stays outward-prioritized, while completion,
transport, lighting, meshing and GPU preparation remain independent; no
worker waits for a ring, neighbor or render-frame barrier.

The Client additionally performs a bounded post-load presentation audit.
It incrementally checks visible imported columns against the Client-owned
GPU cache, including an explicit cache entry for valid empty meshes. If a
loaded column has neither current cached geometry nor pending mesh work, the
replica requeues its existing revision for background meshing. Lost initial
light requests and missing first-frame preparation acknowledgments are also
reconciled. Ready and in-flight geometry is left intact; no Server generation,
disk operations, global barriers, or unconditional remeshing are introduced.

## Mesh Residency and Prediction

CPU mesh vertices contain full-precision position/UV, tint/AO, light, material,
animation and optional atlas-repeat bounds. Native GPU vertices occupy 32 bytes:
position and UV remain 32-bit floats, tint/light use normalized bytes, and one
32-bit index selects an immutable surface table. Each 32-byte surface entry holds
material, animation and repeat bounds, deduplicated during CPU preparation with
expected O(1) hashing. Surface tables grow with actual content and are shared by
index-only prediction views. Metal uses a device buffer, Vulkan a read-only
storage buffer, and modern OpenGL a texture buffer. Unsupported hardware resource
sizes fail explicitly; no registry-count or shape ceiling is introduced.

Full opaque cube faces with identical atlas/material/animation tuples and constant
corner tint, AO and light merge into rectangles within each 16³ section. Atlas UVs
repeat per block inside the selected tile. Arbitrary shapes, overlays, fluids,
transparency and varying corner shading keep their ordinary quads. Prediction
retains each quad's inclusive owner-cell bounds and face direction. If an edit
intersects a merged rectangle, its replacement regenerates that direction across
the rectangle plus the ordinary local patch, leaving other cached faces intact.
No asynchronous World job or GPU wait is introduced in that synchronous patch.

Native indices use 16-bit elements for meshes with at most 65536 vertices
and automatically retain 32-bit elements for larger meshes. Incremental native
uploads pad compact index allocations to four-byte alignment for Vulkan
transfer compatibility. Translucent camera-sorted indices remain independent
32-bit buffers; CPU-side geometry always uses 32-bit indices. Once the base GPU upload finishes, the Client
discards its full CPU source vertex and index arrays. It retains only two 16-bit
chunk-local owner bounds and a byte of material/winding/direction per face.
Immediate local prediction reconstructs only the selected index list; the
renderer groups its draw passes and reuses original GPU vertex data and
transparent triangle centers without rehydrating CPU vertex arrays.
Source resource ownership is reference-counted through the renderer: retiring
an old mesh does not free its native vertex allocation while a derived view
still uses it.

The debug overlay displays one process resident-memory number followed by a
memory breakdown (MiB), ordered left to right: GPU (base mesh payload),
SW (Server World retained storage), CW (Client World retained storage),
and U (unattributed resident-memory remainder). World figures inspect the owner-thread's allocated
column snapshots, packed sections, lighting, indexing structures and pending CPU
meshes; detached
worker outputs, external registries, and renderer allocations are not included.
GPU bytes sum live native mesh vertex/index/surface allocations, including
translucent sort buffers and index-only prediction views without double-counting
shared source vertices. The counter excludes textures, temporary uploads,
retired native resources and driver overhead. The fourth number is a subtraction from process
resident memory, not an independent allocation measurement, and can be unknown
if the GPU allocation footprint exceeds reported residency. Native allocation pooling
remains a separate opportunity; these counters establish no process-memory target.

Server lighting is scheduled within the configured simulation distance plus
one neighboring column for authoritative gameplay queries. Render-only Server
columns retain block/biome data without allocating sky or section-light caches
unless they were previously active. Newly entered simulation columns resume
their existing pending light work; obsolete distant light jobs are cancelled
without holding up Client lighting, which independently covers all rendered
columns. The one-column simulation halo preserves gameplay light availability
at the active edge.

Numerical direct-sky caches retain exact four-bit values. Mutable lighting outputs
use a 32 KiB dense array; immutable publication elides uniform 16³ sections and
packs only nonuniform sections (2 KiB each), plus section descriptors. Fully uniform
columns need only descriptors and a minimal allocation. Complex lighting can retain
the complete dense representation. Snapshots share payloads until an actual sky
write expands a private dense cache; compatible light rebases share the compact
result. These derived caches are absent from persistence.
The independent per-column replication-change bitmap is allocated only after a
block mutation and released when the matching Client update is acknowledged;
new columns and clean Client visual columns do not carry an idle 8 KiB map.
The Server retains only the acknowledged block revision and dirty-cell bitmap
for replication: the complete old chunk snapshot (including lighting) is no
longer pinned solely as an update baseline. Owner-thread World accounting
reports shared payload allocations proportionally across their live references
and excludes detached worker-owned copies. Lighting-change bitsets are also
allocated only on actual light-affecting block mutations, and lighting-job
snapshots only allocate 8 KiB when there are dirty light cells. Initial
lighting jobs use an immutable zero mask without allocating per-column masks.

Transient job admission also accounts for bytes, independently of worker/context
counts. `maage_world_config.job_memory_budget` supplies bytes; zero selects positive
decimal `MAAGE_WORLD_JOB_MEMORY_MIB`, defaulting to 64 MiB per World. Reservations
cover accepted work through owner collection, and pending CPU meshes remain charged
until handed to the Client's separately bounded incremental upload. Generation,
load, light and save reservations estimate scratch plus retained inputs; they are
not a hard bound on arbitrary content callbacks or process residency. Mesh workers
count faces before allocating and request a larger conservative reservation when
needed. The owner retries them without blocking a worker. An oversized mesh drains
other visual work and can run alone; invalidated requests release that preference.
Saves have an independent quarter-budget and likewise allow one oversized writer,
so visual pressure cannot prevent authoritative persistence. Allocation failures
still propagate normally. World statistics expose the budget, reservations, pending
mesh bytes and memory-pressure occurrences.

Initial column packets own independent palette-packed sections and 256 biome IDs.
Uniform-air sections are omitted; state IDs remain full-width numeric identities.
Import validates palette identities, duplicate entries, packing widths and every
index before publication, creates packed payloads directly, and derives the
heightmap without building expanded state arrays. Only exact-base deltas use
sorted cell/state records. No Server snapshot or light storage is borrowed.

Replica capacity reserves the transport's packet capacity above the largest live
footprint: the Server's target, its observed residency, and the Client's retained
base count. This preserves import headroom while a render-distance shrink still
has ordered unloads in flight, including a reversal of that shrink. Reading the
Client count is O(1); target changes do not preallocate column data or evict bases.
Presentation shutdown captures its failing status before saving options. A rejected
column records its coordinates, revision/base, retained count and capacity before
releasing the packet, so shutdown cannot replace the cause with a generic fatal.

## Runtime Section Storage

A 16x16x16 populated section keeps calculated light in a separately allocated
4 KiB byte array. Its light buffer is reference-counted independently of block
identities and detached by the first mutation in a private lighting job or
compatible snapshot merge. Mutable jobs use direct 32-bit numeric state
arrays, while immutable published section snapshots use an adaptive bit-packed
palette: uniform (zero-bit), 1-, 2-, 4-, 8-, or 16-bit indices. At most 4096
distinct states can physically occur inside a section; palette width does not
cap the global runtime block registry. Section expansion happens only on a
private mutation, never in a render-frame read or in a shared worker snapshot.
Workers compact completed generated/loaded columns before publication; other
private edits compact at owner publication. If optional palette allocation
fails, direct state storage remains valid. Each compacted palette and packed
index array lives in a single immutable reference-counted allocation. Chunk
clones share both the immutable block payload and the separately reference-counted
lighting array, copying only their small section metadata until a write requires
a private copy. Mutating a block expands and detaches only that section's
palette, while relighting detaches only the affected light array; neither
operation writes into shared payloads.
References use platform-native atomic operations because independent workers
clone and retire payloads concurrently. On the first Client import the immutable
authoritative block palettes and initially zeroed lighting are shared with the
visual replica; subsequent prediction and lighting edits detach only the affected
section storage. The Server and Client remain
independently owned across the Local Session boundary.

## Storage Codec Performance

Current `MGCHNK9` region records use numeric saved state/biome IDs and
CRC32 integrity checks. The encoder now discovers the small set of
distinct block identities using an allocated per-save hash table, sorting only
unique entries instead of all occupied cells. The decoder copies validated
section lighting in bulk, and the shared CRC implementation uses zlib's
optimized tables while retaining the same CRC32 polynomial for corruption detection.
No second filesystem metadata query is made by the general file reader
after its caller has already checked missing chunk paths.

`world.bin` contains one checksummed `MGIDMAP1` dictionary: block, item and
biome names occur once per identity; state entries reference a saved block ID
plus numeric metadata. Opening resolves these identities once and retains both
saved-to-runtime and runtime-to-saved tables. Registration sequences may change
between sessions. Existing saved IDs retain their meanings; new definitions append
to each domain and the complete extension publishes atomically before chunk jobs
start. Missing definitions and duplicate, invalid or incomplete maps reject opening.
The encoded dictionary is cached for metadata saves, without repeated name work.

Chunks are grouped into 32x32 region files. Each region header binds the World dictionary UUID and its own coordinates/checksum.
Each region holds an append-only
sequence of independently zlib-compressed, CRC-verified chunk records and an
in-memory latest-record index. A complete footer commits each record; an
incomplete final append is ignored and truncated before the next save.
Regular appends avoid redundant truncation calls. After all I/O jobs drain,
region close compacts substantially stale append logs by copying only the
latest verified compressed record of each slot to a synced temporary file,
then atomically replacing the old region. The original file survives any
failed pre-replacement compaction. Region
mutexes serialize overlapping operations while independent region files run
on different I/O workers. The disk layout deliberately rejects legacy
per-chunk files and old metadata: worlds are recreated during pre-alpha.

## Determinism and Persistence

Each generation job caches 256 profiles and up to 64 regional seed records in its
own stack storage. The seed cache memoizes raw and supported values; collisions
and eviction change only cost. Neighbor support always reads raw classifications,
never another cached filtering result. No pointer survives another cache lookup.
Feature owners, halo queries and spawn clearance evaluate the same seed/coordinate functions.
Signed coordinates use floor division; hash arithmetic retains explicit 32-bit
masking. No loaded neighbor, shared RNG, render option, job completion order,
global smoothing pass or cross-column wait participates in regional decisions.
Feature emission retains deterministic regional replay and clips writes to the
detached output column. Tree snow ownership uses two small job-local arrays of
256 entries, with no persisted tree records or neighbor access. Spawn floor queries
replay the same tree emitter and snow decisions.

`MGWRLD12`, `MGCHNK9` and `MGRGN2` are the current formats. Block-entity records identify their owning block through the World block dictionary and dispatch versioned registered codecs; runtime handler IDs are never persisted as an ordering assumption. Existing saved chunks load
their authoritative blocks and biome identities; they are not regenerated or
rewritten to adopt a new region model. Only missing chunks use current generation.
Existing-to-new material, decoration or biome-tint seams can therefore remain,
while the terrain-height function is retained. No migration or alternate generator
is introduced.

## Reference and Verification Limits

Minecraft 1.7.10 has no universal minimum biome area or width. It magnifies coarse
regions, uses opposing-neighbor smoothing and climate-edge buffers, and requires
neighbor support for many hill variants. Four final zooms in the default world
(six for Large Biomes) help establish regional scale; shores, rivers and later
variants can be narrower. A final perturbed nearest-region lookup adds local detail.
Its terrain parameters blend neighboring regions, while surface replacement and
decoration use discrete biome choices. Biome outlines do not strictly track height
contours. Snow/ice depend on biome temperature and elevation, with a small spatial
temperature adjustment above Y=64. The final snow pass checks each precipitation
column after decoration; it does not guarantee whole-tree snow coverage.

MaaGE's supported seeds and shared boundary deformation independently provide
regional extent and irregular outlines while retaining its existing terrain function.
Neither generator promises that every final biome component has a minimum area,
and coastlines can intentionally clip a region. MaaGE uses contextual hill climate
instead of adding the reference game's separate climate-specific hill identities.
Snow follows actual elevation, but biome outlines are not forced onto constant-height
contours. Whole-tree initial snow decisions intentionally improve on vanilla's
per-column behavior. Seed parity and the full vanilla biome/feature catalog are
not implemented.

Source inspection and formatting do not establish visual quality, runtime cost,
or cross-platform floating-point equivalence. Those outcomes require programmer
verification; no runtime or visual result is implied by this description.
