# Visual Fidelity Implementation

This document records content refinements against Minecraft Java Edition 1.7.10
and the shared Metal/Vulkan rendering architecture. The original fidelity pass
compiled on macOS; the subsequent renderer consolidation has not been compiled
or executed. In-game appearance remains unverified. No applications, tests,
benchmarks or GitHub Actions were executed.

## Reference Evidence

The requested local `unpacked` directory was inspected directly. It contains
Forge sources and patch archives; the Minecraft implementations are in the
adjacent `forgeSrc-1.7.10-10.13.4.1614-1.7.10-sources.jar` under
`~/.gradle/caches/minecraft/net/minecraftforge/forge/1.7.10-10.13.4.1614-1.7.10/`.
These were read as behavioral references, without copying Java implementations
or assets into MaaGE.

| Reference | Behavior Examined |
| --- | --- |
| `RenderGlobal.renderSky`, `WorldProvider.calculateCelestialAngle` | Additive celestial blending, orbit, sprite size/orientation, moon phases |
| `RenderGlobal.drawBlockDamageTexture/drawSelectionBox` | Multiplicative cracks and translucent black selection edges |
| `WorldProvider.generateLightBrightnessTable`, `EntityRenderer.updateLightmap`, `World.getSunBrightnessBody/getSkyColorBody` | Nonlinear light levels, warm emission, daylight and weather colors |
| `RenderBlocks.renderStandardBlockWithAmbientOcclusion/getAoBrightness` | Corner light and occlusion sampling |
| `BlockGrass.getIcon/colorMultiplier`, foliage/tall-grass color methods, `ColorizerFoliage`, `BiomeGenBase` | Base/overlay textures, neighborhood colors, fixed spruce/birch colors |
| `BlockTorch.onBlockPlaced/collisionRayTrace`, `RenderBlocks.renderBlockTorch/renderTorchAtAngle` | Standing/wall states, narrow geometry, cropped cap UVs and bounds |
| `BlockRotatedPillar`, `BlockSnow`, `BlockCactus`, associated `RenderBlocks` methods | Axis textures, partial shapes and UVs |
| `BlockLiquid`, `RenderBlocks.renderBlockLiquid/getLiquidHeight`, `TextureAtlasSprite.updateAnimation` | Fluid corner heights, flow direction, texture layout and timing |
| `ItemRenderer.renderItem/renderItemIn2D/renderItemInFirstPerson/updateEquippedItem` | Block versus icon presentation, perspective, equip/swing transforms |
| `EntityRenderer.renderHand/setupViewBobbing/getFOVModifier`, `RenderPlayer.renderFirstPersonArm`, `ModelBiped/ModelBox/TexturedQuad` | Arm geometry/UVs and camera/hand relationships |

Forge patches introduce custom sky renderers, item renderers, color hooks and
additional passes. Those extension hooks are distinct from the baseline visual
behavior and are not Java/Forge compatibility features implemented by this pass.

## Differences Corrected

- **Celestial backgrounds:** the original local sun and moon PNGs have no alpha
  channel. PNG decoding correctly supplies straight RGBA, atlas composition copies
  all channels, and Metal uploads RGBA unchanged. Ordinary UI alpha blending
  therefore overwrote the sky with opaque black. Explicit source-alpha additive
  blending now preserves the original black texels as zero light contribution,
  without color-keying or altering source pixels. Celestial planes use the eased
  orbit, reference proportions/orientation, moon phases and rain attenuation.
- **Grass/foliage:** removed biome-baked atlas variants. Grass tops and separate
  side overlays receive one numeric biome tint; dirt bottoms/base sides remain
  untinted. Mesh workers average a 3x3 biome neighborhood across chunk boundaries.
  Tall grass uses local color; spruce/birch use their fixed reference colors.
  Snow-covered grass sides have their own texture. Inventory and held-item
  colors share one presentation helper. Fast/Smart/Fancy culling remains distinct.
- **Torches/logs:** replaced crossed torch planes with narrow capped standing and
  tilted wall geometry. Server placement chooses numeric states and validates
  support; raycasting copies orientation-specific bounds. Emission remains 14.
  Logs resolve end faces and bark UV orientation by axis; drops preserve species.
- **First person:** removed the fixed HUD cube. Cached block meshes, extruded
  plant/torch icons and the original default-skin arm use a separate 70-degree
  perspective pass with independent depth. Copied authoritative swing/light
  observations drive presentation; equip transitions, bob and camera lag remain
  Client effects. Foreground state clears when returning to Title.
- **Lighting:** smooth corner sky/block samples, AO, partial-face interpolation
  and diagonal-aware triangulation replace flat face illumination. Nonlinear
  light response and warm emission replace a linear scalar floor. Numeric
  opacity preserves direct skylight through plants/torches, attenuates leaves
  by 1 and water/ice by 3. Night changes do not remesh or relight chunks.
- **Materials/models:** solid/cutout depth writes, sorted translucent depth reads,
  multiplicative cracks and additive sprites are separate behaviors. Water RGBA
  stays intact. Shared weighted fluid heights remove per-cell surface steps;
  flowing UVs follow numeric gradients. Still/flow animation uses preloaded padded
  atlas strips with original timing. Snow UV cropping, cactus planes/collision
  height and ice interior culling are corrected. Raw cracks preserve the already
  rendered block texture, light and tint instead of replacing them with baked tiles.

## Interfaces and Ownership

Changed shared contracts are `blocks.h` (opacity, models, state bounds, numeric
tint/overlay/flow/animation fields), `geometry.h` (material/animation channels),
`render.h` (blend, foreground and animation presentation), `environment.h`
(shared celestial phase), `content.h` (neutral numeric atlas slots), and
`session_protocol.h` (version 2 with copied authoritative swing).

Implementation changes cover Client resources, environment, interaction,
viewport and gameplay UI; the common renderer and Metal shader/backend; World
content, registry validation, items, gameplay, raycast, lighting, mesh and job
snapshot handling; and Server snapshot publication. Project instructions and
roadmap descriptions were updated. Launcher behavior is unchanged.

Mesh jobs retain eight immutable neighboring columns and validate their revisions.
Lighting propagation still uses cardinal neighbors. No global World lock,
per-face allocation, runtime name lookup, render-thread World wait or repeated
texture decoding was added. Animation and lighting use uniforms; held geometry
uploads only on item changes. Translucent index copies are owned per frame slot;
distance sorting refreshes after camera travel rather than every mouse rotation.

The original fidelity pass advanced Client to `0.1.0-alpha.35` and Server to
`1.0.0-alpha.10`. Earlier worlds
containing torch metadata 0 are rejected: recreate that disposable development
data. No migration was added. Stored lighting is recalculated on load.

## Shared Primary Rendering

Metal and Vulkan consume the same indexed world, UI, scene, foreground and
animation contracts. Shared portable C groups solid/cutout, translucent and
multiply indices, prepares triangle centers, derives camera origin, and orders
transparent meshes/triangles. Sorting updates after half a block of camera
travel; each backend owns retired-frame index copies. Frame planning and
transient capacity growth are shared; native command encoding remains separate.

Native MSL and GLSL include authoritative material IDs, animation and nonlinear
lighting equations. Metal embeds MSL for native compilation. Vulkan builds
SPIR-V with a host `glslc` or `glslangValidator` and embeds it into the executable;
no external shader files are required at runtime. Windows vcpkg provisions host
glslang tools; Linux requires shader tools alongside Vulkan headers/loader.
Metal-only and headless builds do not require Vulkan shader tools.

The primary backends use the same straight-alpha and additive batches, material
depth rules, atlas animation, independent foreground depth and projective
panorama inputs. Metal retains MPS Gaussian blur; Vulkan uses separable GPU
Gaussian passes with shared sigma and edge clamping. Native resource handling
and presentation differ: Metal retains its bounded asynchronous presenter;
Vulkan bounds command/fence slots and swapchain image synchronization. Vulkan
VSync selects FIFO; disabling prefers supported MAILBOX, then IMMEDIATE, with
FIFO as the required fallback. Hardware backpressure bounds uncapped work.
KHR/EXT swapchain/surface maintenance1 and its presentation fences are optional.
The baseline uses per-image presentation semaphores and drains queues/devices
at swapchain replacement and shutdown. Maintenance1 provides explicit presentation
retirement; [Khronos documents the baseline limitation](https://docs.vulkan.org/guide/latest/swapchain_semaphore_reuse.html)
in proving terminal presentation completion through queue idle alone. Ordinary resize preserves pipelines when
the surface format is unchanged. UI copies and sorted indices belong to retired
frame slots; immutable texture/mesh destruction defers GPU release until completion.

## Build Results and Remaining Limits

macOS ARM64 Debug and Release Client/Server compile and link successfully.
A separate Release headless Server builds with Client, Launcher, Window and
Render disabled. These results predate the shared renderer changes. Compiled
macOS OpenGL/software adapters retain their existing capabilities. The current
Metal/Vulkan changes have not been configured or compiled on any platform;
Windows/Linux and Vulkan remain uncompiled here. Existing historical unused
helper/initializer and duplicate-library warnings were not rechecked.

The shader source is embedded by the build. Standalone `xcrun metal` compilation
could not run because the optional Metal Toolchain is absent; native shader
compilation and rendered results require programmer verification.

Remaining limits include exact vanilla lightmap/gamma/flicker parity, altitude/noise
biome-color variation, randomized plant offsets, torch support-loss drops and
particles, underwater fog/overlays, clouds/stars and unsupported item effects
(maps, bows, eating, enchantment). The arm uses the default skin. Breaking geometry
uses target bounds for special shapes. Translucency sorts within meshes and by
mesh distance, so intersecting surfaces across meshes remain an approximation.

## Programmer Comparison Steps

These requested steps were not executed:

1. Compare noon/sunset/night sun and all moon phases against 1.7.10; turn the
   camera across screen edges and compare rain attenuation and black-background removal.
2. Compare grass tops, dirt bottoms, side fringes, snow-covered sides and biome
   borders; inspect oak/spruce/birch leaves in Fast, Smart and Fancy.
3. Place torches on a floor and all four walls; compare thickness, lean, cap UVs,
   support position, targeting bounds and emitted light. Place logs on each axis.
4. Compare empty hand, terrain blocks, grass, foliage, plants and torches while
   switching slots, swinging, walking, turning, pausing and returning to Title.
5. Inspect cave corners, partial snow, cactus edges, leaves and torch-lit surfaces
   by day/night; cross loaded chunk corners while comparing light and tint seams.
6. Inspect still/flowing water and lava, underwater-facing surfaces, animation
   cycles, ice against terrain and overlapping translucent surfaces while moving.
7. Break lit and biome-tinted blocks in Survival; confirm cracks darken the
   existing surface and selection edges follow the authoritative target bounds.
