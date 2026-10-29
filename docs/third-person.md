# Player Perspectives and Character Presentation

## Input and camera ownership

F5 cycles First Person, Third Person Back, Third Person Front, then First Person.
The numeric input key is mapped to macOS keycode 96, Windows scan code 0x3f,
X11 physical FK05 and Wayland evdev 63. Existing press-edge tracking prevents
repeat cycling. Only focused, captured, visible gameplay accepts it; modal text
entry and native mode transitions do not. Focus loss clears edges/capture.

Perspective starts in First Person and remains transient within the Client
process. It is absent from Options, persistence and Server commands. Movement still uses the player's own
look, including in front view; F5 does not reverse controls or change reach.

The existing first-person projection, hand, equip/swing, bob and dynamic FOV stay
on their original path. First person excludes all full-body instances. Third
person clears the foreground hand and uses the current interpolated feet plus
the 1.62-block eye offset as its pivot, lowered 0.2 while crouching. Back places
the camera four blocks opposite the look vector; Front places it along that
vector and faces back with yaw + pi and negated pitch. Mouse-look is immediate.
Camera and character share one interpolation parameter; repeated publications
of the same authoritative tick do not reset that interval. Delayed snapshots
clamp interpolation rather than extrapolating another movement simulation.

The final camera matrix drives the world and environment/sky in the same frame.
Targeting, mining prediction, reach and positional audio retain the player's eye
and look. A distant camera cannot mine around a wall.

## Local Collision Samples

The Client samples an 11x11x11 block-state volume from its effective replica around
the interpolated eye cell. Four footprint geometry epochs include predictions and
corrections; only a changed center or epoch refreshes it. This presentation cache
uses neither a Session collision mailbox nor any Server sampling. It continues
while paused as replica updates arrive, without I/O, worker waits or a World lock.

Local DDA traversal checks immutable numeric definitions' solid collision AABBs.
A center ray and eight corner rays shorten either camera boom.
Offsets are at least 0.1 blocks and widen to enclose the actual near plane at the
active aspect/FOV; a small contact margin avoids coplanar wall clipping. Missing
columns, absent publications, unknown states and positions outside the copied
volume obstruct travel. Clearing an obstruction restores the four-block boom.
A boom compressed inside the head omits the head, and an eye-close boom omits the
body, avoiding a view through skin interiors. F5 itself never reloads geometry.

## Shared player and held items

`player_model.c` owns the same twelve skin cuboids used by first-person arms,
inventory and world presentation: six base parts plus inflated hat, jacket,
sleeves and trousers. Head is 8x8x8 pixels, torso 8x12x4, legs 4x12x4; arms are
4x12x4 classic or 3x12x4 slim with their existing model offset. Modern limbs have
independent UVs in the already normalized 64x64 atlas. No new skin acquisition,
decoding or per-frame resource lookup occurs.

The shared Y-up anatomical conversion preserves UV orientation and reverses
winding when converting cuboid X coordinates. World presentation lazily uploads
six immutable meshes, each containing its base and outer layer, once per world.
It submits part matrices through existing Render instances, with sampled sky/
block light and fixed face tones. Outer layers retain sorted fractional-alpha
translucency and normal world depth behavior. Changing F5 mode reuses these
resources. Resource failures are cached, diagnosed once and omit the affected
optional decoration until resource destruction/reload.

Shared pose calculation supplies idle arm motion, distance-derived walk/run
swings, opposite leg swings, head pitch, bounded relative head yaw, crouching,
held-arm posture and the shared Client attack/mining swing. Body turning uses
a 0.3 response and a 75-degree relative-head limit with additional
turning beyond 50 degrees. Walk amount follows copied displacement at 20 TPS;
render interpolation and time-based turn response avoid FPS-dependent speed.
Jump/fall/fly use authoritative vertical positions and the normal airborne biped
pose; no new movement or flight simulation is introduced. Crouching raises leg
pivots in Y-up coordinates and lowers the root to keep the feet planted. Health
loss adds brief recoil; observed death rolls the model onto its side. These are
local decorations over the existing provisional health/death rules.

The right-hand transform inherits shoulder, torso/swing and arm rotation. Selected
blocks, tools and flat icons borrow the existing item-entity model cache, using
separate equipped scales/orientations for centered native geometry. Items retain their existing texture/tint and lighting.
One instance submission composes the player with dropped/pickup item instances;
no moving-object mesh upload or second renderer is needed. Instance references
are cleared before resource teardown.

Inventory keeps its mouse-following head/body rotation and GUI clipping/batching.
It uses the same cuboids, UV generation, pivots and pose/part-matrix helpers, then
projects into the existing GUI batch; it has no world camera or private GPU pass.

Metal and Vulkan consume exactly the same cached meshes, transforms, materials,
light levels and ordered translucent traversal. Their existing renderer/shader
interfaces suffice; no backend-specific character algorithm or layout change is
needed. OpenGL and Software retain their smaller capability subsets.

## Limits and Manual Review

Remote character replication, armor, capes, emotes and a general item-use animation
system are absent. Wayland has keyboard events but lacks the relative mouse
capture required by Client gameplay; Linux can use X11.

Check F5 press edges, focus/modal routing, front-view controls and reach, four-block
boom collision at walls/ceilings/unloaded boundaries, head/body turning, sprint,
crouch and attack poses. Compare classic/slim clothing, held items, sky alignment
and stable cached resources across perspectives, FPS/FOV and world transitions on
Metal and Vulkan. Runtime and visual behavior remain programmer-owned.
