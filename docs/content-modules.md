# Content Modules

MaaGE supplies mechanisms; statically linked modules register content and policy. `MaaGE::Base` is an ordinary `maage_content_module` selected by `src/application/content_modules.c` for Client and Server. The authoritative signatures and lifetime comments are in `include/maage/content.h`.

## Composition and Lifetime

`maage_content_config.modules` submits namespaced module descriptors and optional named dependencies. Creation rejects duplicates, missing dependencies and cycles, then chooses a stable topological order for deterministic numeric assignment. All modules **define**, all **resolve** forward references, registries **finalize**, then all **initialize** against immutable definitions. `maage_content_failure` reports the module/dependency, phase and original Status.

`maage_content_context.module_data` survives a module's phases. `maage_content_own` transfers owned state on success; `maage_content_module_data` resolves another module's published state at composition boundaries. Config/descriptors are borrowed through creation, while callback code, registered contexts and module-owned state outlive Worlds, jobs and audio/visual readers. Stop all consumers before destroying content. Client atlas/resource users retain their composition descriptors until teardown.

Registration copies definitions and checks references across content domains. Namespaced names resolve at registration/import boundaries; runtime hot paths use numeric IDs. Optional presentation/appearance resolvers update copied UV/tint information without changing logical texture identities. Headless composition does not require image resolution.

## Extension Interfaces

| Interface | Contract |
| --- | --- |
| `blocks.h`, `items.h`, `biome.h`, `presentation.h` | Immutable finalized numeric definitions and behavior/presentation metadata. |
| `containers.h` | Copied layouts, slot/progress descriptors and Server-owner open/access/policy/action/sample/retryable-close callbacks. Independently paged transport windows do not cap registrations. Transaction failure retains item ownership; custom backing stores stay module-owned. |
| `fluids.h` | Fluid identities and copied state maps for schedules, propagation, contact, physics, material and immersion; finalized dispatch is numeric. |
| `block_geometry.h` | Optimized primitives or immutable worker-safe quad providers. Allocation-free count/write passes agree; collision/selection bounds are separate from visual geometry. |
| `sounds.h` | Registered resources/events/music/ambience independent of playback; Client owns decode, voice and device capacity. |
| `crafting.h`, `smelting.h` | Numeric recipes, smelting and fuel policy used by the authoritative owner. |

No API registry has an artificial limit derived from transport page size, GPU working set or simultaneous sound voices. Saved block/state/item/biome IDs use the World dictionary; other runtime IDs follow the deterministic current composition.

Entity and block-entity registration/codecs remain distinct facilities. Dynamic module loading/discovery, scripting and remote content negotiation are not implemented.
