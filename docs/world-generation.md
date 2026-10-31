# World Generation

The Server generates detached columns through ordered terrain, cave, underground,
surface, vegetation and structure stages. Integrated and dedicated sessions use
the same generator. Client biome color blending is a separate presentation setting
and cannot change generated blocks, regional climate or feature choices.

## Regions and Terrain

One seeded sampler classifies ocean, hills, taiga, desert, forest and plains from
low-frequency continental, relief, temperature and rainfall fields. The existing
thresholds and classification precedence select one discrete registered biome at
each World coordinate. Inland fields share a continuous coordinate deformation
with two octaves at a 96-block scale and a 24-block amplitude. One displacement
component uses the broad octaves of the terrain field, tying boundary detail to
landform structure. The same deformed coordinates drive regional identity and all
ecological influences; materials and features do not receive separate perturbations.

The continental field retains its undeformed coordinates, preserving its geographic
boundary. The four corners of the existing 128-block terrain lattice also retain
their undeformed classification. Base height, variation and terrain/detail noise
retain the existing interpolated height function; regional detail does not move
terrain-height anchors or quantize biome identities into square cells.

Boundaries follow contours of those environmental fields. Categorical surfaces
use the selected biome's own surface/filler pair, and the same identity is stored
in the chunk for gameplay, persistence and Client tint sampling. No random rank
chooses neighboring surface materials. A desert therefore keeps its sand profile
throughout its region, while neighboring grassland keeps its registered profile.
Sand retains sandstone backing; submerged non-sandy ground retains gravel.
Terrain height and sea-level water fill still determine the physical waterline.

The current selector chooses six built-in categories. Registering another biome
does not itself add a selection rule. IDs remain discrete, and registry definitions
are immutable after finalization; ecological interpolation never modifies them.

## Transition Ecology

Continuous influences use a finite margin of 0.02 noise units on either side of
the sampler's environmental classification thresholds. Quintic ramps follow the
same precedence and compound conditions as region selection. Desert influence
requires warmth and dryness together; relief and continental boundaries share
that same model. Outside the threshold margins, the selected biome contributes
its unmodified climate and decoration properties exclusively.

The transition's width in blocks follows the local environmental gradient rather
than a random material field or a fixed per-block mixing probability. Terrain
corners do not spread ecological influence throughout their entire 128-block cell.
At junctions, the participating conditions combine through that one classifier;
there is no list of biome-pair exceptions or separate blending noise per feature.

Temperature, rainfall and decoration probabilities interpolate only within these
transition regions. Forest tree density can taper toward plains, and species
probabilities can shift toward conifers at taiga boundaries. The existing altitude
lapse of one temperature unit per 600 blocks above Y=64 and cold threshold of 0.15
remain. Snow follows that continuous climate and elevation, without a separate
microclimate patch field. Exposed source sea water freezes using the same climate.
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

Trees retain jittered eight-block candidates and their registered species ratios.
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

Each generation job caches 256 profiles in its own stack storage. Feature owners,
halo queries and spawn clearance evaluate the same seed/coordinate functions.
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

Minecraft 1.7.10 uses repeated regional zooming/smoothing and a final perturbed
nearest-region lookup to give discrete biome outlines detail at several scales.
Its terrain parameters blend neighboring regions, while surface replacement and
decoration use discrete biome choices. Biome outlines do not strictly track height
contours. Snow/ice depend on biome temperature and elevation, with a small spatial
temperature adjustment above Y=64. The final snow pass checks each precipitation
column after decoration; it does not guarantee whole-tree snow coverage.

MaaGE's shared boundary deformation is an independent algorithm for irregular
regional outlines, retaining its existing terrain function and ecological margins.
Snow follows actual elevation, but biome outlines are not forced onto constant-height
contours. Whole-tree initial snow decisions intentionally improve on vanilla's
per-column behavior. Seed parity and the full vanilla biome/feature catalog are
not implemented.

Source inspection and formatting do not establish visual quality, runtime cost,
or cross-platform floating-point equivalence. Those outcomes require programmer
verification; no runtime or visual result is implied by this description.
