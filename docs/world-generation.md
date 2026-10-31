# World Generation

The Server generates detached columns through ordered terrain, cave, underground,
surface, vegetation and structure stages. Integrated and dedicated sessions use
the same generator. Client biome color blending is a separate presentation setting
and cannot change generated blocks, regional climate or feature choices.

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

The nearest supported seed selects one discrete regional biome. A shared coordinate
deformation adds broad terrain-related shape and smaller eight-block edge detail.
The same regional geometry drives surface identity and ecological influences;
materials and individual features do not receive separate perturbations.

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
Gaps at least `128 * 16` square blocks contribute nothing; the local seed geometry
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

`MGWORLD8` and `MGCHNK5` remain the current formats. Existing saved chunks load
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
