# MaaGE Development Roadmap

MaaGE is an independent native voxel platform using Minecraft Java Edition 1.7.10
as loose gameplay/content inspiration. This roadmap describes current boundaries
and future direction; it authorizes no implementation, build or runtime work.
Current source is the authority for implemented capabilities. Runtime, visual,
audio and cross-platform completion require programmer verification.

## Existing Foundations

| Area | Implemented in Source | Current Limits |
| --- | --- | --- |
| Platform | C90 shared libraries; native Window/Input/Storage/Jobs/Status/Log; Metal/Vulkan rendering and a modern OpenGL scene/UI backend | Modern OpenGL awaits compilation and programmer runtime/platform validation; legacy OpenGL/Software implement smaller subsets; Wayland lacks Client relative capture. |
| Applications | Independent native Launcher, Client and headless Server; one integrated/dedicated authoritative runtime | Remote transport, administration GUI and Launcher Server management are absent. |
| World | Numeric registries, deterministic staged generation, sparse columns, immutable asynchronous I/O/jobs, environment, scheduled fluids/support and random ticks | One current generator/format; no Java seed/save interoperability or migration. |
| Gameplay | Movement/collision, breaking/placement, provisional vitals, inventory, typed item/falling-block entities, cactus contact damage, sapling/tree growth, food use, transactional 2x2/3x3 crafting | Full Survival progression, mobs, combat and smelting remain incomplete/absent. |
| Presentation | Title/world management, gameplay/settings/inventory screens, local visual light/meshing, break/placement prediction, shared player skins/perspectives, eating poses, configurable biome blending, audio | Creative has one catalog; unavailable settings/content remain disabled or absent. |
| Launcher/resources | Microsoft authentication/access verification, encrypted account records, verified local resources, UUID instances and exact local Client selection | No remote distribution or modpack installation system. |

Predevelopment data is disposable: incompatible encodings fail explicitly without automatic
reset, conversion or deletion.

## Current Priorities

Resolve observable defects in existing systems before adding speculative layers.
Keep UI navigation, input capture, pause/save/quit, fullscreen safe areas and
resource failure recovery coherent. Verify Client lighting and prediction across
chunk boundaries; preserve Server gameplay light, scheduled cascades and saved RNG.
Maintain Metal/Vulkan presentation semantics and resource lifetimes together.

Current technical references:

- [Client UI, visual World, prediction and neighbor support](gameplay-parity.md)
- [Rendering, materials, light sampling and sprint presentation](visual-fidelity.md)
- [Item entities, harvesting and drops](item-entities.md)
- [Crafting transactions and temporary containers](crafting.md)
- [Random ticks, leaf support and RNG checkpoints](random-block-ticks.md)
- [Authenticated skin acquisition and cache](player-skins.md)
- [Player perspectives and camera collision](third-person.md)
- [Numeric audio catalog](../src/audio/catalog/README.md)

## Remaining Milestones

### Client GUI and Menu Completion — Phase 8

Extend the existing numeric screen/navigation system. Finish supported Options,
controls/focus workflows, Creative authoring/navigation and truthful loading
progress as services expose it. Preserve ASCII font/text limits until deliberately
expanded. Completion means a coherent startup → world → settings → save/quit
workflow, including failure and cancellation, rather than merely drawing controls.

### Visual Fidelity and Game Feel — Phase 9

Refine light/AO, water/lava, foliage, sky/weather, particles, animation, interaction
feedback and movement/camera feel. Correct defects without rebuilding gameplay in
the Client or introducing backend-specific behavior. Completion requires consistent,
responsive scenes on the primary renderers, not just available textures/shaders.

### Core Gameplay Completion — Phase 10

Build complete Survival progression on the existing Server: furnaces/smelting,
food/equipment, damage/combat, experience, passive/hostile mobs, spawning, AI and
navigation. Reuse current entities, drops, recipes and persistence. Current ingot/
charcoal crafting availability does not imply Survival smelting exists.

### Advanced Gameplay — Phase 11

Add ordered redstone mechanisms, pistons/repeaters/comparators, richer block
entities, enchanting, dimensions/portals, structures and endgame progression.
Parallelize only independent workloads while preserving deterministic ordering
where required. Features must work as persistent integrated gameplay.

### Remote Networking — Phase 12

Extend the existing authoritative Server with remote transports and an explicitly
serialized versioned protocol independent of application SemVer. Support multiple
players, sequenced input/reconciliation, atomic chunk/entity replication, lifecycle,
configuration and administration. Launcher hosting workflows follow those services.
Single-player and multiplayer must never become separate gameplay engines.

## Dependencies and Completion

Numeric content definitions and ownership precede new gameplay; persistence must
cover authoritative state, while Client effects derive from copied publications.
A Server GUI is a frontend to the same runtime and must not impose graphics on
headless startup. Mods should prefer data-driven definitions and C-compatible
extensions; Java/Forge execution and existing Java mod compatibility are not goals.

Compilation establishes build compatibility only. The programmer owns runtime and
platform acceptance. Follow [AGENTS.md](../AGENTS.md) for authorization, security,
formatting and permanent development constraints.
