# Content Modules

MaaGE owns mechanisms; statically linked modules provide content and policy.
`MaaGE::Base` exports `maage_base_module`, an ordinary `maage_content_module`.
The shared application composition in `src/application/content_modules.c` selects
that descriptor for both Client and Server. Integrated sessions borrow the same
finalized content; dedicated startup uses the same composition.

## Lifecycle and Ownership

`maage_content_config.modules` supplies unique namespaced descriptors and optional
named dependencies. Creation validates every dependency, duplicate and cycle before
running callbacks. A stable topological order selects the earliest submitted ready
module, making numeric assignment deterministic. All modules define independent
content, then all resolve forward references, all registries finalize, and finally
all modules initialize runtime data against immutable definitions.

`maage_content_context` exposes the shared registries and current `module_data`.
A module retains that pointer across phases; `maage_content_own` transfers owned
state only on success. `maage_content_module_data` resolves another module's
published state at initialization boundaries. Runtime callbacks retain numeric
references and immutable contexts rather than names or registration contexts.
Descriptor/configuration borrows end after creation; callback code and registered
contexts outlive all Worlds, jobs and playback users. Client resource initialization
explicitly retains its composition descriptors through resource destruction because
it creates both an atlas catalog and presentation-resolved content.

`maage_content_failure` reports the failing module, dependency, phase and originating
Status. Shared finalization failures have no module owner. The application boundary
logs those details with the causal trace. Failed creation releases registries before
module-owned state. Shutdown stops consumers before destroying their content.

## Registered Systems

- `containers.h` registers copied slot, layout, texture and progress descriptors,
  immutable module context and owner-thread open/access/policy/action/sample/close
  callbacks. Server validates identity, access, revisions and visible slot policies.
  Callbacks preflight ownership changes and retain uncommitted items on failure.
  Custom backing storage stays module-owned. Failed closure keeps the container
  closing for owner retries; fallback recipe descriptors validate their grid
  contracts. Progress uses bounded logarithmic work even for large pixel ranges. Copied slot and progress windows page
  independently over the same origin; the packet capacity does not cap registration.
  Client draws the authoritative type's registered layout. Existing recipe scratch
  remains specifically 2x2/3x3; arbitrary containers use their own transactions.
- `fluids.h` assigns fluid identities and copies level/state maps. Definitions select
  scheduling, propagation, regeneration, displacement, physics, material, fog and
  immersion presentation. Optional immutable contact policies select cross-fluid
  products. Base owns water/lava values and stone/cobblestone/obsidian reactions.
  Finalization caches each state's fluid definition for numeric runtime dispatch.
- `block_geometry.h` registers optimized primitives or worker-safe quad providers.
  Providers use immutable state/neighborhood queries and allocation-free sinks;
  count and write passes, including prediction patches, must agree. Cuboid services accept resolved face appearance,
  material, UV and culling data. The same providers feed World, held-item and GUI
  geometry. Providers can own any number of copied appearances, accessed through
  `query.geometry` and resolved once by the configured `appearance_resolver`. This
  requires no artificial block states. Collision and selection retain independent
  registered bounds; a state can explicitly override or disable type collision.
  World collision and placement share the same bounds resolver, and changing
  physical bounds invalidates geometry epochs even when visual keys match.
  Custom providers do not require another renderer or shader.
- `sounds.h` separates registered resources, events, materials, music and ambient
  policies from playback. Engine occurrence hooks select registered numeric events;
  modules can emit additional events directly. Registration capacity is independent
  of decode queues and concurrent voices. Base owns resource paths and selection
  policies; Client owns scene state, decoding, streaming, device handles and reclamation.

Registries reject unknown nonzero references. Content finalization validates
cross-domain item/block/biome, material, fluid-map, geometry and presentation
references. Client atlas resolution may change copied UV/tint presentation but
cannot change logical texture identities. Biome colormap resolution applies to all
composed modules with matching numeric/name identities.

## Runtime Boundaries

Owner-thread callbacks keep Server gameplay authoritative. Meshing providers run on
detached workers and borrow immutable content; they allocate no per-voxel storage
and perform no path/name lookup. Common geometry retains optimized native paths
and packed surface tables. Atlas strips pack by dimensions, including registered
GUI images, so new content does not require fixed positional texture lists.

Session protocol 22 carries registered container and sound identities, copied
container windows and generic progress values. Persistent block/state/item/biome
names continue through the existing World dictionary; fluid, geometry and sound
IDs derive from the current deterministic composition and are not saved as new
independent numeric domains. Missing content remains an explicit failure.

Entity and block-entity registration, storage, codecs, lifecycle and iteration are
separate systems. This content API does not generalize those interfaces. A dynamic
loader, package discovery, scripting and remote content negotiation are future work.
Source contracts do not establish compilation or runtime/platform correctness.
