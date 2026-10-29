# Visual Rendering

Metal and Vulkan consume the same portable world, UI, scene, foreground and
animation contracts. Runtime appearance and platform behavior require programmer
verification; source integration does not establish visual parity.

## Shared Preparation and Backend Ownership

Common C owns material grouping, triangle centers, camera/depth ordering,
translucent sorting, pipeline rules, uniform packets, UI packing and transient
capacity growth. Mesh handles own immutable CPU preparation; adapters own GPU
resources, retired-frame copies, synchronization, command encoding and presentation.
Transparent sorting refreshes after half a block of camera travel. Translucency
across intersecting meshes remains approximate.

Native MSL and GLSL use shared vertex/uniform field lists, material IDs and
lighting/animation equations. Metal embeds MSL for native compilation. Vulkan
compiles and embeds SPIR-V with host `glslc` or `glslangValidator`; missing tools
fail configuration. Metal-only/headless targets need no Vulkan tools.

Solid/cutout passes write depth; sorted translucent passes read it. Cracks multiply
the already lit/tinted surface. Celestial sprites use source-alpha additive
blending: opaque black asset pixels contribute zero without color-key replacement.
RGBA remains straight alpha. Shared UI segment planning supports destination
inversion for the crosshair; layout is backend independent. Metal uses MPS Gaussian
panorama blur; Vulkan uses separable Gaussian passes with shared sigma/clamping.

Vulkan bounds frame/command resources and uses FIFO with VSync; disabling prefers
MAILBOX, then IMMEDIATE, then FIFO. Baseline swapchain synchronization uses per-image
presentation semaphores and replacement/shutdown drains; maintenance1 presentation
fences are optional. See [Khronos' presentation lifetime guidance](https://docs.vulkan.org/guide/latest/swapchain_semaphore_reuse.html).
Unchanged surface formats preserve pipelines across resize. Texture/mesh GPU
release waits for retired use, not every frame.

Metal bounds frames in flight. VSync Off renders to owned offscreen targets while
a presenter displays the latest completed result; re-enabling VSync restores direct
drawable presentation. Mutable batches/traces are copied rather than borrowed across
threads. Native fullscreen transitions must not join the presenter from the event loop.

## Geometry, Light and Resources

The [Client replica](gameplay-parity.md) calculates visual skylight/emission and
meshes. The mesher retains eight neighbor columns for smooth corner light, AO,
biome colors and fluid heights; result tokens validate cardinal/diagonal revisions.
There is no per-face allocation, name resolution or graphics dependency in World jobs.
First geometry does not wait for final light. Meshes and actor samples share a
roof-aware presentation approximation until numerical light is initialized;
calculated darkness is distinct from missing/pending data.
Nonlinear light response and warm emission are shared shader behavior. Plants and
torches preserve direct sky; leaves attenuate by 1 and water/ice by 3. Day/night
brightness changes do not trigger relighting or remeshing.

Grass top/side overlays receive numeric biome tint while dirt/base sides remain
neutral. Mesh workers average a 3x3 biome neighborhood; spruce/birch colors are
fixed. Snow-covered grass uses its own sides. Fast leaves use opaque textures and
aggressive internal culling; Fancy preserves internal cutout faces; Smart retains
an exposed plane and two interior cutout planes before suppressing deeper faces.
Opaque blocks never substitute for leaf coverage. Mode changes rebuild only meshes.

Standing and wall torches use narrow capped geometry, oriented selection bounds
and emission 14. Logs select bark/end UVs by axis. Partial snow/cactus shapes crop
UVs rather than stretching them. Water uses weighted corner heights and numeric
flow gradients; still/flow frames occupy padded immutable atlas strips. Water
retains its source RGBA and ice culls appropriate interior faces.

Sky sprites use the current camera rotation/FOV/aspect without translation,
including bob, with projective clipping at viewport edges. Server time/weather
supply orbit, moon phases and rain attenuation.

Item definitions choose block/sprite models and resolved metadata tiles. GUI block
icons project top, negative-X and positive-Z faces into 16x16 slots using scale 10,
X=210° and Y=45°; ambient 0.4 plus two diffuse 0.6 lights shade them. Shared tint,
alpha, partial-shape UVs, counts, durability and cursor handling serve all inventories.
Cached world items use shared material traversal and transforms, without geometry
uploads on movement. See [Item Entities](item-entities.md).

## Column Lighting Ownership

Each light task clones one column and borrows four immutable neighboring snapshots.
Its queue holds at most 65,536 deduplicated cells, with an 8,192-byte membership
bitset; accepted-job capacity bounds concurrent memory. Existing direct-sky data
is retained. Dirty bytes select changed cells and shafts, while changed boundary
masks seed only the necessary faces. Sky and emission solve the same attenuating
fixed point, including removal. Boundary passes converge after inputs stabilize;
intermediate retained values are explicitly pending presentation data.

Light output and mesh work have separate owner slots. The owner rebases numerical
output onto current blocks/auxiliary state when edits arrived during execution,
then retains newer dirty work. Incarnation protects unload/reload. Facing boundary
epochs detect arrivals, removals and changed input; follow-up passes replace broad
halo cancellation. Geometry tokens reject changed blocks but allow older lighting
until a budgeted refinement mesh replaces it. There is one solver with independent
Server gameplay and Client visual caches; neither publishes light across Session.

Architectural references reviewed before choosing this independent design:
[Lumi's batching](https://github.com/GTMEGA/Lumi),
[Starlight's propagation and section discussion](https://github.com/PaperMC/Starlight/blob/fabric/TECHNICAL_DETAILS.md),
[Angelica's threaded terrain](https://github.com/GTNewHorizons/Angelica),
[FalseTweaks' AO/boundary fixes](https://github.com/FalsePattern/FalseTweaks), and
[Sodium's worker queue/context ownership](https://github.com/CaffeineMC/sodium/blob/dev/common/src/main/java/net/caffeinemc/mods/sodium/client/render/chunk/compile/executor/ChunkBuilder.java).
Their implementations are not included or translated. Their performance claims
are not MaaGE measurements; no runtime improvement has been measured here.

## Player Presentation and Sprinting

First-person blocks, extruded icons and the selected skin arm use a separate
70° perspective/depth pass before GUI. Equip transitions, swing, bob and camera lag
remain Client presentation; returning to Title clears foreground state. Shared
cuboids/UVs serve arms, inventory and [third person](third-person.md).

The Server owns sprint eligibility and timers: forward input at least 0.8 plus
Left Control or a grounded second tap within seven ticks; Survival hunger must
exceed six. Stopping forward, sneaking, horizontal collision, death, mode change
or modal pause cancels sprint. Its 600-tick timer can reactivate from a held request.
Ground/air acceleration increases 30%; jump adds 0.2 forward impulse. Sneak scales
horizontal input by 0.3; sprint does not multiply Creative flight or fluid movement.
Survival exhaustion is 0.1/m sprinting versus 0.01/m walking and 0.8/jump versus 0.2.

Client sprint FOV targets a factor of 1.15, with an additional 1.1 for flight.
A continuous half-difference-per-20-Hz filter smooths frames; repeated snapshots
do not reset it, pause freezes it and new worlds reset it. World and sky share
effective FOV; saved base FOV and fixed hand projection stay independent.

## Limits and Manual Review

Exact lightmap/gamma/flicker matching, altitude/noise biome tint variation,
randomized plant offsets, particles, underwater overlays/fog, clouds/stars and
maps/bows/eating/enchantment effects remain incomplete or absent. Movement/fluid
feel, sneak ledges, blindness and item-use slowdowns remain provisional/unsupported.
OpenGL and Software retain their implemented subsets and reject unsupported draws.

Pending programmer review: compare day/night/weather sky edges, foliage modes,
biome seams, torch orientations, log axes, partial shapes, cave AO, fluid animation,
ice/translucency and cracks. Exercise sprint activation/cancellation, exhaustion,
FPS/FOV changes and fixed hand framing. Compare the same scene on Metal and Vulkan;
compilation alone establishes none of these outcomes.
