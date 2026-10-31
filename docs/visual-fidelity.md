# Visual Rendering

Metal, Vulkan and modern OpenGL consume the same portable world, UI, scene,
foreground and animation contracts. Runtime appearance and platform behavior
require programmer verification; source integration does not establish visual parity.

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
OpenGL embeds GLSL 3.30 with the same shared headers and compiles three programs
once per context, using the native driver; it requires no Vulkan tools.

Solid/cutout passes write depth; sorted translucent passes read it. Cracks multiply
the already lit/tinted surface. Celestial sprites use source-alpha additive
blending: opaque black asset pixels contribute zero without color-key replacement.
RGBA remains straight alpha. Shared UI segment planning supports destination
inversion for the crosshair; layout is backend independent. Metal uses MPS Gaussian
panorama blur; Vulkan and OpenGL use full-resolution separable Gaussian passes
with shared sigma/radius and edge clamping, followed by crisp UI composition.

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

Modern OpenGL uses WGL/GLX 3.3 Core or NSOpenGL 4.1 Core. Context-local entry
points, GLSL/profile/limits, double buffering, actual RGBA8/depth24/stencil8
framebuffer completeness and sync creation are checked before scene capabilities
are published. Driver version/vendor/device and limits are logged; the device
interface also exposes bounded vendor/version text for F3. Virtualized drivers
follow these same checks, with no version-only bypass or vendor blacklist.

Immutable geometry uses VAO/VBO/index buffers; shared material preparation writes
directly into mapped index storage. UI and sorted indices use reusable growing
buffers in three fence-protected frame slots. Busy slots return `NOOP` without
waiting. World and foreground sorting have separate slot storage. Native deletion
retains queued GPU uses through OpenGL object lifetime rules. Scene/depth and
panorama targets resize transactionally per retired slot; atlas rows preserve the
top-first CPU contract, while framebuffer composition uses native bottom-first
UVs. The mesh shader converts shared clip depth `0..W` to OpenGL `-W..W`, preserving
XY/CCW winding. Composition uses RGBA8 display values with framebuffer sRGB
conversion disabled, matching the other backends' scene blending.

Only hidden initial-frame preparation uses `glFinish`; ordinary rendering flushes
and polls fences. WGL discovers extensions and validates GPU initialization on
disposable drawables before committing the real window's pixel format once;
existing formats are reused. Shader/FBO failures leave a previously unset window
format untouched for fallback. GLX requires a compatible double-buffered RGB window
visual and brackets context/binding/swap-control negotiation against X errors.
NSOpenGL tracks backing-size changes on the main thread. Native VSync uses
WGL EXT, GLX EXT/MESA/SGI or NSOpenGL swap intervals; unavailable or rejected
changes return an explicit status. Wayland OpenGL contexts remain unsupported.
Explicit `opengl` stays within its family. Legacy 1.1 remains clear-only, lacks
Client scene/mesh/UI capabilities and is rejected by Client's required-feature
negotiation. AUTO retains the existing platform preference order.

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
neutral. Client mesh workers support OFF (1x1), Vanilla 3x3 and Smooth 5x5/7x7/9x9;
Smooth 5x5 is the default. Vanilla retains one averaged color per block, including
byte-channel truncation and local tall-grass tint. Smooth modes average the four
adjacent box-filter centers at each world-space corner and interpolate those
shared corner colors across faces, overlays and applicable plants. Spruce/birch colors are
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

The Video Settings Biome Blend control persists numeric mode 0..4 in
`[video] biome_blend` in the instance's `config/options.ini`. It changes only the
Client replica. Existing Server biome identities and colormap definitions remain
unchanged; Server Worlds disable mesh production. No new Session data is needed.
Changing quality advances mesh revisions, cancels pending mesh work and rejects
results from obsolete settings without scheduling light. Old GPU geometry stays
visible until an asynchronous replacement can publish. Prediction patches use
the same setting and rebuild on a mode change.

Each immutable meshing job lazily builds bounded summed-area grass/foliage/count
samples (at most 26x26, with a five-cell halo), then caches at most 17x17 corner
colors. Radius-dependent sampling and constant-time box queries avoid per-face
neighborhood lookups. Patch cells group by column to reuse the same cache.
Missing columns contribute no sample weight. Initial/imported biome changes and
neighbor arrival/removal invalidate the existing eight-column visual halo; job
input epochs reject obsolete neighbor sets. Once the same inputs are available,
shared edge/corner samples agree across chunks regardless of job completion order.
Newly available biome data can still refine edge colors during asynchronous
replacement; no per-frame tint calculation or screen/texture blur is involved.
Snow variants, fixed foliage colors and inventory/item preview tints retain their
separate rules. Lighting/AO and their interpolation are unchanged.

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
70° base perspective/depth pass before GUI. Equip transitions, swing, bob and camera lag
remain Client presentation; returning to Title clears foreground state. Shared
cuboids/UVs serve arms, inventory and [third person](third-person.md).

The Server owns sprint eligibility and timers: forward input at least 0.8 plus
Left Control or a grounded second tap within seven ticks; Survival hunger must
exceed six. Stopping forward, sneaking, horizontal collision, death, mode change
or modal pause cancels sprint. Its 600-tick timer can reactivate from a held request.
Ground/air acceleration increases 30%; jump adds 0.2 forward impulse. Sneak scales
horizontal input by 0.3; sprint does not multiply Creative flight or fluid movement.
Survival exhaustion is 0.1/m sprinting versus 0.01/m walking and 0.8/jump versus 0.2.
Horizontal drag uses contact at step entry: ordinary-ground takeoff retains 0.546,
while a landing entered airborne retains 0.91 until the next step. Dry non-flying
velocity below 0.005 blocks/tick settles before acceleration. Held grounded jumps
repeat no sooner than ten ticks; release rearms immediately. Flight and fluid
movement keep their separate input behavior. Supporting-surface slipperiness and
automatic half-block stepping remain incomplete.

Client sprint FOV targets a factor of 1.15, with an additional 1.1 for flight.
A continuous half-difference-per-20-Hz filter smooths frames; repeated snapshots
do not reset it, pause freezes it and new worlds reset it. World and sky share
effective FOV; saved base FOV and the hand's base projection stay independent.

`camera.c` evaluates movement, hurt and death once for world/hand presentation.
Raw 20 TPS motion observations preserve intermediate ticks across coalesced
snapshots; a bounded Client history keeps simulation endpoints for interpolation.
Horizontal amplitude approaches clamped speed by 0.4 per tick; vertical pitch
approaches `atan(-vertical_velocity * 0.2) * 15` degrees by 0.8, targeting zero on
landing/death. Distance phase advances through airborne travel, while grounded
crouch holds it. First-person crouch lowers the eye by 0.08 blocks with tick-based
release recovery, independently of targeting/audio, and retains the 0.1-block
camera-local view offset. Third-person boom positioning/collision stays separate.

Authoritative damage occurrence/life IDs and tick markers drive ten-tick hurt
rotation, fourth-power easing, captured incoming bearing, death roll and death
FOV. Current environmental damage has zero bearing; combat is still absent.
Water scales world and hand FOV by 60/70. The transform order is death/hurt,
optional movement bob/vertical pitch, then camera orientation/position; the hand
uses the same effect prefix once before its own lag/equip/swing transforms.
These screen effects also apply in third person, preserving the reference order.
Pause freezes evaluated effects; respawn/world changes reset them. Missing raw
observations explicitly return PARTIAL and rebase from a snapshot or hold an
available filter endpoint; this recovery cannot reconstruct an omitted transient.
No rendering work runs on the Server.

Video Settings exposes View Bobbing (default ON), persisted as
`[video] view_bobbing=true|false` in the instance's atomic `config/options.ini`.
OFF suppresses movement bob and vertical pitch, retaining hurt/death transforms,
water/death FOV and required camera positioning.

Grounded sneaking reduces projected X, Z and combined diagonal walk requests in
0.05-block steps until the body shifted one block down overlaps collision shapes.
This permits overhang and partial-height support before the normal Y/X/Z sweep.
It does not invent wall collisions, zero horizontal inertia or alter gravity;
upward takeoff, airborne motion and Creative flight bypass the guard. Unknown
terrain retains the World collision contract. This uses registered collision
geometry; slab/stair content and multibox shapes are not introduced here.

Fluid surfaces retain shared four-cell weighted corner heights and numeric
still/flow animations. Player, item and texture orientation now share the same
flow observation, including downward openings and falling-water wall currents.
Non-solid material lowers exposed corners; same-fluid internal faces stay hidden.
Top faces remain visible under opaque roofs, and stacked falling-fluid sides meet
without a per-cell vertical slit. Fluid light takes the brighter channels from
the sampled cell and the cell above, without solid-cube AO. Existing async mesh
jobs, local prediction patches, transparency sorting and retained GPU presentation
remain in use; no separate fluid mesher or synchronous relight is introduced.

## Underwater Presentation

Client replica surfaces at the interpolated camera select water presentation.
World and hand projection apply the existing 60/70 water FOV factor once; in
third person the world FOV follows the boom camera. This is independent of the
Server eye threshold used for breathing and mining. Exit restores the ordinary
scene immediately. Water uses exponential view-depth fog with density 0.1 and
base RGB (0.02, 0.02, 0.2), weighted by cached local light/daylight and view distance.
It blends distant geometry toward blue, preserving ordinary lighting on nearby
terrain and entities. The exact reference fog-light temporal smoothing is not
implemented. Unknown light retains the last presentation value.

One 112-byte shared uniform packet and shader equation serve Metal, Vulkan and
modern OpenGL; native adapters bind/upload the additional fog vector. Fog applies
to scene geometry and instances, with neutral fading for multiplicative damage
cracks. Foreground and HUD are unfogged. Unsupported backends reject nonzero fog.
The existing optional `textures/misc/underwater.png` loads once into the scene
atlas. First-person immersion draws four repeats with orientation-linked UVs and
half alpha after the hand, before the HUD, using at most 25 clipped quads. It adds
no time-based wobble or refraction. Missing texture retains fog with one resource
notice. Resource bytes are never packaged or acquired by the Client.

## Limits and Manual Review

Exact lightmap/gamma/flicker matching, altitude/noise biome tint variation,
randomized plant offsets, particles, clouds/stars and
maps/bows/enchantment effects remain incomplete or absent. Eating uses shared
first/third-person poses; food-fragment particles remain absent. Movement/fluid
feel and blindness remain provisional/unsupported.
Legacy OpenGL and Software retain their implemented subsets and reject unsupported draws.

Pending programmer review: compare day/night/weather sky edges, foliage modes,
biome seams, torch orientations, log axes, partial shapes, cave AO, fluid animation,
ice/translucency and cracks. Exercise sprint activation/cancellation, exhaustion,
FPS/FOV changes and fixed hand framing. Compare the same scene on Metal and Vulkan;
compilation alone establishes none of these outcomes.
