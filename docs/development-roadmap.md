# MaaGE Gameplay Roadmap

Minecraft: Java Edition **1.7.10** is the reference. The following phases describe
gameplay development, separately from Launcher releases. Only Phase 1 is currently
implemented in source; compilation does not establish runtime correctness or
vanilla fidelity. Client remains **0.1.0-alpha.1**.

## Phase 1 — Rendering and Movement

Implemented:

- Standalone `maage-client` lifecycle using existing native Window, Render, Log,
  Time, Storage and MinecraftAssets modules. Version queries still bypass game
  initialization. Launcher passes its resource root via `--data-directory=<path>`;
  direct startup defaults to the shared platform MaaGE data directory.
- Separate numeric block-type and state IDs. Air, stone, grass and dirt retain
  type IDs 0–3; their first states occupy a distinct numeric domain. State records
  reference types, numeric metadata and resolved per-face textures. Registration
  rejects duplicate names/IDs and invalid definitions, then freezes definitions.
- Direct, deterministic O(1) numeric definition arrays. External names use an
  initialization/import-only, collision-checked hash table with expected O(1)
  resolution. World cells, collision, movement, meshing and draw commands contain
  no namespaced identity lookups or string comparisons.
- Portable `MaaGE::World`: nine contiguous 16×16×16 sections, a deterministic
  48×16×48 grass/dirt/stone landscape, modest raised terraces and a safe spawn.
  World storage owns no GPU resources; future headless simulation can reuse it.
- One-time indexed CPU meshing, with outward normals/winding, four shared vertices
  per face and hidden-face culling across section boundaries. Only visible faces
  are uploaded; the immutable world never rebuilds meshes per frame.
- Startup PNG decoding through `MaaGE::Image` (libpng 1.6+). Actual reconstructed
  `minecraft/textures/blocks/{stone,dirt,grass_top,grass_side,grass_side_overlay}.png`
  and `minecraft/textures/colormap/grass.png` produce a padded, nearest-filtered
  atlas. Grass top/overlay tint and per-face UVs are computed before gameplay.
  Required files must exist and decode correctly; Client never downloads or
  authenticates. No acquired textures are embedded or distributed.
- Renderer-neutral opaque indexed-mesh upload/destroy/draw commands, implemented
  by native Metal: perspective, camera transform, depth testing, back-face culling,
  viewport/depth resizing and sky clear. GPU geometry/atlas are uploaded once.
  Original MSL is embedded during the build and compiled by Metal at initialization
  of mesh rendering; an offline Metal compiler is not required.
- Native physical WASD/Space/Escape input, relative capture and focus-loss cleanup
  for Cocoa, Win32 and X11. Camera input remains separate from portable physics.
  WASD walks, Space jumps, Escape pauses/releases capture, and clicking resumes.
- Fixed 20 TPS player simulation with bounded catch-up and interpolated camera
  position. Gravity, jumping, ground detection and swept axis-aligned voxel
  collision run independently of rendering. Focus loss/capture release pauses
  simulation; inactive/hidden windows use event waits.

Current limitations and provisional behavior:

- **Metal is the only textured-mesh implementation.** Vulkan, OpenGL and Software
  retain existing clear/presentation paths but reject this gameplay requirement.
  Explicit renderer requests never switch families. Wayland gameplay input/capture
  is unsupported; X11's warp-relative mouse path retains native pointer acceleration.
- Static finite terrain has an invisible collision boundary at its horizontal
  edges and a solid floor. There is no chunk streaming, terrain generation,
  block interaction, save format or network authority yet.
- Centralized movement constants approximate classic 20 TPS movement: 0.6×1.8
  player bounds, 1.62 eye height, per-tick gravity/drag/jump and ground/air
  acceleration. Exact 1.7.10 fidelity and platform input feel need programmer
  verification. There is no automatic step-up, sprinting, sneaking, flying or water.
- Fixed grass colormap sample (51,173), precomputed directional face shading and
  a fixed sky replace biome tinting, light propagation and time of day. Camera
  sensitivity/FOV are provisional. The atlas has no mipmaps or resource-pack stack.
- Local builds cover macOS arm64. Windows/Linux source paths and runtime shader
  compilation, visual appearance, movement and collision require programmer testing.
  No applications, automated tests, downloaded assets or CI runs are part of this
  implementation's build verification.

## Phase 2 — World Architecture and Generation

Planned: faithful 16×16 chunk columns, 16×16×16 sections and original 256-block
world-height compatibility; block updates/tick scheduling, lighting, world seeds,
terrain noise, biomes, procedural generation, chunk loading/unloading and persistence.

## Phase 3 — Player Interaction

Planned: block raycasting, breaking/placement, hotbar/inventory, item stacks,
creative/survival player state, health/hunger and basic world interactions.

## Phase 4 — World Systems

Planned: caves/structures, ores/vegetation, fluids, day/night, weather, block
entities, more complete block behavior and world simulation fidelity.

## Phase 5 — Full Classic Gameplay

Planned: entities/mobs, AI, combat, crafting/furnaces, redstone, enchanting,
Nether/End, progression and faithful Minecraft 1.7.10 mechanics.

## Phase 6 — Client/Server Architecture

Planned: authoritative dedicated Server simulation, a versioned network protocol,
compatibility rules, multiplayer, player/world synchronization and Launcher server
hosting integration. Portable world/simulation must remain independent of graphical
Client ownership. The Server stays headless; optional administration UI remains a
frontend over its simulation, rather than a second implementation.
