# Player Perspectives and Character Presentation

Client `0.1.0-alpha.47`, Server `1.0.0-alpha.17`, local Session protocol 10.
World/player checkpoint formats and the Launcher appearance contract are unchanged.

## Input and camera ownership

F5 cycles First Person, Third Person Back, Third Person Front, then First Person.
The numeric input key is mapped to macOS keycode 96, Windows scan code 0x3f,
X11 physical FK05 and Wayland evdev 63. Existing press-edge tracking prevents
repeat cycling. Only focused, captured, visible gameplay accepts it; modal text
entry and native mode transitions do not. Focus loss clears edges/capture.

Perspective starts in First Person and remains transient within the Client
process. It is absent from Options and Server commands. The reference GameSettings field is likewise
not serialized by its load/save methods. Movement still uses the player's own
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

## Collision publication

World samples an 11x11x11 block-state volume around the owner's eye cell and the
four footprint column revisions. Only a changed center or revision refreshes it.
A separate latest-value Session mailbox copies this bounded volume; the Client
retains it and rejects stale sequence/epoch data. No World pointer, worker buffer,
render-loop I/O, global World lock or frame-driven Server request is involved.
The sampler also runs during the existing paused publication pump.

Local DDA traversal checks immutable numeric definitions' solid collision AABBs.
A center ray and the reference eight corner rays shorten either camera boom.
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
decoding or per-frame resource lookup is added.

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
held-arm posture and the reference eased attack/mining swing. Body turning uses
reference-derived 0.3 response and a 75-degree relative-head limit with additional
turning beyond 50 degrees. Walk amount follows copied displacement at 20 TPS;
render interpolation and time-based turn response avoid FPS-dependent speed.
Jump/fall/fly use authoritative vertical positions and the normal airborne biped
pose; no new movement or flight simulation is introduced. Crouching raises leg
pivots in Y-up coordinates and lowers the root to keep the feet planted. Health
loss adds brief recoil; observed death rolls the model onto its side. These are
local decorations over the existing provisional health/death rules.

The right-hand transform inherits shoulder, torso/swing and arm rotation. Selected
blocks, tools and flat icons borrow the existing item-entity model cache, using
separate equipped scales/orientations adapted from RenderPlayer/ItemRenderer to
centered native geometry. Items retain their existing texture/tint and lighting.
One instance submission composes the player with dropped/pickup item instances;
no moving-object mesh upload or second renderer is needed. Instance references
are cleared before resource teardown.

Inventory keeps its mouse-following head/body rotation and GUI clipping/batching.
It uses the same cuboids, UV generation, pivots and pose/part-matrix helpers, then
projects into the existing GUI batch; it has no world camera or private GPU pass.

Metal and Vulkan consume exactly the same cached meshes, transforms, materials,
light levels and ordered translucent traversal. Their existing renderer/shader
interfaces suffice; no backend-specific character algorithm or layout change is
needed. OpenGL and Software interfaces remain unchanged.

## Source reference and compilation

The supplied Forge `unpacked` tree contains Forge/FML sources; adjacent
`forgeSrc-1.7.10-10.13.4.1614-1.7.10-sources.jar` supplies vanilla sources.
Examined EntityRenderer.orientCamera, Minecraft.runTick, GameSettings load/save
and key fields, EntityLivingBase.onLivingUpdate/onUpdate and body turning,
ModelBiped constructor/setRotationAngles, RenderPlayer.doRender,
preRenderCallback/renderEquippedItems, RendererLivingEntity.doRender/rotateCorpse,
RenderLiving, EntityPlayerSP.onLivingUpdate, EntityPlayer eye-height handling and
ItemRenderer.renderItem/renderItemIn2D. ModelPlayer is absent in 1.7.10; slim and
modern outer layers extend the shared current skin contract.

Debug/Release Client and Server compiled and linked on macOS ARM64, including
Metal, Vulkan/MoltenVK, existing OpenGL/Software code and Vulkan shader generation.
A separate Release headless Server also compiled/linked. Existing warnings remain
in renderer initializers, unused helpers/variables, resource signedness and the
bundled audio decoder; compilation is not visual or gameplay verification.
Windows/X11/Wayland sources were reviewed but not compiled on this host. Wayland
now receives physical keyboard events and focus changes, but its pre-existing
relative mouse capture limitation still prevents the Client gameplay input path
from running on that adapter. Use its existing X11 fallback on Linux.

No MaaGE applications, tests, benchmarks, synthetic harnesses, runtime-validation
scripts or GitHub Actions were run. No multiplayer/remote camera replication,
armor, capes, emotes, item-use animation system or new combat rules are included.

## Pending programmer verification

- Cycle F5 First → Back → Front → First; hold/release, lose focus and type in
  menus. Check F3, F11, Escape and inventory shortcuts alongside it.
- Check first-person arms, held items, equip/swing, bob and sprint FOV; compare
  back-view walking, sprinting, jumping, falling, flying and crouching.
- Check front-view face/orientation, unchanged movement direction and mining reach;
  rotate quickly, look vertically/backward and compare smooth head/body turning.
- Move against walls, ceilings, trees, caves, tunnels and unloaded boundaries in
  both third-person modes; compress/release the boom and inspect near clipping.
- Compare authenticated classic/slim asymmetric skins and translucent outer layers
  across arms, inventory mouse-follow preview and both world views. Swing/mining
  should carry blocks, tools and flat items with the right hand.
- Repeat at different FPS/FOV, resize/fullscreen, pause/resume, death/respawn and
  world transitions on Metal and Vulkan; compare depth, sky and stable resource use.

These scenarios remain unexecuted.
