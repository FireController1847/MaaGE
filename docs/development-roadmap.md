# MaaGE Development Roadmap

Source-level status, not development authorization or runtime verification. [AGENTS.md](../AGENTS.md) owns project policy.

## Existing Foundations

| Area | Present in source | Principal gaps |
| --- | --- | --- |
| Applications | Native Core/World/Session, independent Launcher/Client/Server, shared authoritative runtime | Remote networking, administration/hosting |
| Presentation | Metal/Vulkan, modern GL, limited legacy/Software, native input, game UI, Creative, replica/light, GPU streaming, player appearance/audio | Platform validation, Wayland capture, secondary backends, fidelity |
| Content/world | Static root/submodules, phased binding, repeatable LOAD, numeric registries, typed properties/tags/providers, recipes, fluids/models/entities, regional terrain and async region saves | Dynamic loader, scripting, distribution, remote negotiation |
| Gameplay | Movement/mining/placement, passive/hostile mobs, spawning/combat, drops, support updates, scheduled/random ticks, saplings/wheat, food, crafting/furnaces/chests | Validation, richer AI, full Survival, redstone, enchanting, dimensions/endgame |
| Launcher | Native navigation, async catalog/access/resources, verification receipts, UUID instances, exact Client packages | Platform/startup validation, remote packages, modpacks |

Incompatible development formats fail explicitly without migration/deletion.

## Remaining Milestones

- **Client:** Complete UI/settings, accessibility, loading/cancellation and resource-failure handling.
- **Fidelity:** Refine lighting, foliage/water, sky/weather, effects, animation and interaction on primary renderers.
- **Survival:** Equipment, food, AI/navigation, breeding/riding, XP entities and enchanting.
- **Advanced gameplay:** Redstone, structures, block entities, dimensions/portals and endgame.
- **Networking/extensibility:** Versioned remote Session protocol, multiplayer replication/reconciliation, module and modpack delivery.

## Technical References

[Content modules](content-modules.md) · [World/gameplay](world-and-gameplay.md) · [Client presentation](client-presentation.md) · [Launcher](launcher.md) · [Diagnostics](diagnostics.md).

Current source/public headers own detailed implementation contracts; programmer verification remains necessary.
