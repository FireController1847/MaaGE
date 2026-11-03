# MaaGE Development Roadmap

This is a source-level view of existing foundations and planned work—not authorization for development or proof of runtime/visual correctness. [AGENTS.md](../AGENTS.md) remains the project policy authority.

## Existing Foundations

| Area | Present in source | Principal gaps |
| --- | --- | --- |
| Applications | Native Core/World/Session, independent Launcher, Client and headless Server; one integrated/dedicated authoritative runtime | Remote networking, Server administration GUI and hosting workflow |
| Presentation | Metal/Vulkan, modern OpenGL scene path, limited legacy/Software; Title/world/options/Controls/inventory UI, registered Creative tabs/search, configurable input, visual replica/light, GPU streaming, player appearance and audio | Programmer platform validation, Wayland capture, secondary renderer coverage and visual polish |
| Content and world | Namespace-owned root/submodules, three-stage binding, repeatable Client LOAD, explicit runtime sides, Base content, growable numeric registries, registered fluids/geometry/containers/sounds, regional generation, async I/O and region saves | No dynamic module loader, scripting, modpack distribution or remote content negotiation |
| Gameplay | Movement, mining/placement, typed item/falling entities, drops, support/random ticks, saplings/wheat, food use, transactional 2x2/3x3 crafting and persistent furnaces | Full Survival, mobs/combat/AI, redstone, enchanting, dimensions and endgame |
| Launcher/resources | Microsoft access verification, profile-specific skin cache, verified reconstructed assets, UUID instances and exact Client package selection | Remote package deployment and modpack install support |

Incompatible pre-alpha saves or package formats fail explicitly without silent migration or deletion.

## Remaining Milestones

- **Client completion:** Finish intended UI navigation/settings, honest loading/cancellation, accessibility and resource-failure handling.
- **Visual/game-feel fidelity:** Refine light/AO, water/foliage, sky/weather, effects, animation and player interaction on the primary renderers.
- **Survival progression:** Add wider equipment/food systems, damage/combat, mobs/spawning/AI, XP entities and enchanting.
- **Advanced gameplay:** Build redstone, richer block entities, structures, dimensions/portals and persistent endgame systems.
- **Networking and extensibility:** Extend the same authoritative Server with a versioned remote Session protocol, multiplayer replication/reconciliation and later modpack/module delivery.

## Technical References

[Content module composition](content-modules.md) ·
[World and gameplay](world-and-gameplay.md) ·
[Client presentation](client-presentation.md) ·
[Logging and native crashes](diagnostics.md).

Current source and public headers provide the detailed implementation and API contracts. No compilation, gameplay or visual acceptance is implied by this roadmap.
