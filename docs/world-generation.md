# World Generation

The Server generates detached columns through ordered terrain, cave, underground,
surface, vegetation and structure stages. Integrated and dedicated sessions use
the same generator. Client biome color blending is a separate presentation setting
and cannot change generated blocks, climate profiles or feature choices.

## Biome Influences

One seeded biome sampler supplies the four corners of the existing 128-block
terrain lattice. Quintic interpolation supplies nonnegative weights that sum to
one. The nearest corner still supplies the discrete, persisted biome identity;
base height, variation and terrain/detail noise retain the existing height function.

Generation also uses these weights for temperature, rainfall, vegetation and
biome-limited feature probabilities. All consumers read numeric finalized registry
properties, without a list of biome-pair exceptions. Interiors with identical
corner biomes keep that biome's characteristic material and decoration profile.
At multi-biome junctions, the same rule combines all participating definitions.

The current selector chooses ocean, plains, desert, hills, forest and taiga.
Registering another biome does not itself add a selection rule. The influence
calculation accepts the registry definitions selected by that sampler; decoration
and material consumers do not switch on built-in biome IDs.

## Materials and Climate

Discrete blocks cannot be averaged. A smooth seeded 12-block value field chooses
one contributor's surface/filler pair from its weighted interval. Contributors
are ordered by numeric biome ID, so changing corner indices at a lattice edge
does not change the choice. The field produces coherent patches through the
transition band rather than independent per-block material noise. Sand filler
retains its sandstone backing. Submerged non-sandy ground retains gravel.

The terrain height and sea-level water fill determine the coastline. Aquatic
surface material is excluded from dry-land mixtures when terrestrial influence is
available; underwater material still mixes coastal sand/gravel. This does not warp
or smear the waterline with the material field.

Temperature and rainfall interpolate continuously. A separate smooth 24-block
microclimate field adds at most 0.06 temperature units in either direction; the
existing altitude lapse of one unit per 600 blocks above Y=64 still applies.
The cold threshold remains 0.15. Snow therefore becomes patchy near the freezing
contour while cold taiga interiors remain cold. Exposed source sea water freezes
using the same climate. Snow requires precipitation and actual supporting ground
or leaves, and is placed on the final exposed surface after trees. Ice cannot
support a generated snow layer. Ordinary owner light-based melting remains active.

These are generation-time decisions. Runtime weather observations still use saved
discrete biome identities. Weather-driven accumulation/freezing is not implemented.

## Vegetation and Features

`maage_biome_definition.decoration` contains oak/spruce/birch tree probabilities,
grass, flower, dead-bush, cactus and mushroom densities, plus emerald and lake
attempt weights. Each value is finite in 0..1, tree probabilities sum to at most
one, and zero disables a feature. Registration callers initialize every property;
finalization rejects invalid values. The aquatic trait is a validated boolean.

Tree probabilities apply to the existing jittered eight-block candidates. Density
and species weights blend independently, so forest can thin into plains and
conifers can gradually replace broadleaf trees. A smooth 24-block cover field
modulates vegetation density; sparse per-candidate randomness chooses actual
plants. Flowers also share a coherent type field. Actual numeric floor traits
prevent trees/grass/flowers from occupying sand or gravel; dead bushes and cacti
retain their own support restrictions. Cactus candidates use coordinate priority
for irregular spacing rather than a parity grid. Mushrooms require opaque ground
and an overhead canopy. Shared tree geometry still serves live sapling growth.

Emerald attempts blend the registered hill influence instead of testing a discrete
hill ID. Small underground basin attempts likewise use the blended lake weight.
Unrestricted ore bands, cave paths and dungeon placement remain independent of
biome decoration.

## Determinism and Persistence

Each generation job caches 256 profiles in its own stack storage. Feature owners,
halo queries and spawn clearance evaluate the same seed/coordinate functions.
Signed coordinates use floor division; hash arithmetic retains explicit 32-bit
masking. Independent salts separate materials, cover, climate and feature streams.
No loaded neighbor, shared RNG, render option, job completion order or cross-column
wait participates in these decisions. Feature emission retains deterministic
regional replay and clips writes to the detached output column.

`MGWORLD8` and `MGCHNK5` remain the current formats. Existing saved chunks load
their authoritative blocks and biome identities; they are not regenerated or
rewritten to adopt a new generation profile. Only missing chunks use the current
generator. Existing-to-new decoration/material seams can therefore remain, while
the terrain-height function itself is retained. No migration or alternate
generator is introduced.

## Reference and Verification Limits

Minecraft 1.7.10 blends terrain parameters but still chooses surface replacement
and decoration from discrete biome definitions, with altitude-sensitive snow/ice
decisions. MaaGE deliberately extends blending to physical materials, climate and
decoration instead of reproducing those abrupt boundaries. Algorithms and noise
are independent; seed parity and the full vanilla biome/feature catalog are not
implemented.

Source inspection and formatting do not establish visual quality, runtime cost,
or cross-platform floating-point equivalence. Those outcomes require programmer
verification; no runtime or visual result is implied by this description.
