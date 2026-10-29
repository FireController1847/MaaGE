# Client GUI, Lighting, Mining, and Block Support

The current pass uses Client `0.1.0-alpha.48`, Server `1.0.0-alpha.18`, and
local Session contract 11. The Server owns authoritative blocks and gameplay;
the Client owns visual lighting and meshing over a presentation-only World
replica. There is one simulation, shared by integrated and dedicated hosting.

`MGWORLD6` metadata remains current. `MGCHNK5` is the sole chunk encoding and
includes both fluid and support update kinds. Older development chunks are
rejected and their worlds must be recreated. No migration or automatic deletion
is provided, and this implementation did not modify local development data.

## Reference Inspection

The supplied Forge 1.7.10 `unpacked` directory contains Forge/FML sources.
Vanilla classes were inspected directly in the adjacent
`forgeSrc-1.7.10-10.13.4.1614-1.7.10-sources.jar`. This is read-only source
reference; MaaGE neither executes Java nor embeds its implementation or assets.

The examined GUI classes include `GuiIngame`, `GuiIngameMenu`, `GuiOptions`,
`GuiVideoSettings`, `GuiOptionsRowList`, `GuiScreenOptionsSounds`, `GuiMainMenu`,
`GuiSelectWorld`, `GuiSlot`, `GuiCreateWorld`, `GuiRenameWorld`, `GuiYesNo`,
`GuiInventory`, `GuiCrafting`, `GuiContainer`, `GuiContainerCreative`,
`ContainerPlayer`, `ContainerWorkbench`, `GuiButton`, `GuiScreen`,
`ScaledResolution`, `LoadingScreenRenderer`, and `GuiDownloadTerrain`.

Lighting references are `World.computeLightValue/updateLightByType`,
`Chunk.generateSkylightMap`, block opacity registration, and
`RenderBlocks.getAoBrightness/mixAoBrightness`. Mining references are
`Minecraft.func_147115_a/func_147116_af` (held attack/click),
`PlayerControllerMP.clickBlock/onPlayerDamageBlock/resetBlockRemoving`,
`EntityLivingBase.swingItem/updateArmSwingProgress`,
`ItemRenderer.renderItem/renderItemInFirstPerson`, `ModelBiped.setRotationAngles`,
and `RenderGlobal` damage overlays. Support references are `World` neighbor
notifications and `BlockBush`, `BlockTallGrass`, `BlockFlower`, `BlockDeadBush`,
`BlockTorch`, `BlockCactus`, `BlockSnow`, `BlockMushroom`, and `BlockLeaves` survival,
neighbor, drop, and decay methods. Forge hooks were distinguished from the
underlying vanilla rules; OptiFine menus are not the GUI reference.

## GUI Audit

All 13 registered Client screens were audited, including the Creative catalog
within Inventory. Shared button halves, menu shadows, scaled integer dimensions,
and world-overlay gradients apply across screens. Drawing and input use the
same layout helpers; native fullscreen safe areas remain the content bounds.

| Screen | Reference | Finding and correction |
| --- | --- | --- |
| Gameplay HUD | `GuiIngame` | Corrected the 24x22 hotbar selection, health/hunger Y to height−39, count alignment, 16x16 inverted crosshair, and held-name position/two-second duration/final half-second fade. Existing hotbar item centers and status sprite coordinates needed no change. |
| Pause | `GuiIngameMenu` | Corrected title Y=40 and quarter-height button rows. Unsupported vanilla categories remain omitted; supported Resume, Options and Save/Quit retain actual actions. |
| Options | `GuiOptions` | Existing two-column coordinates, FOV range and Done position needed no change. Added reference FOV endpoint labels, shadows and the in-world gradient. Unsupported categories remain disabled. |
| Inventory | `GuiInventory`, `ContainerPlayer`, `GuiContainer` | The 176x166 panel, player preview at (51,75), 2x2 inputs, output, inventory and hotbar slot coordinates needed no change. Corrected count alignment, slot highlight opacity, tooltip positioning/border/clipping and shared background. Carried stacks and authoritative commands remain intact. |
| Creative catalog | `GuiContainerCreative` | Replaced the invented three-row panel with the original 195x136 item background, five rows of nine items, hotbar at (9,112), and wheel/draggable scrollbar. The implemented single catalog remains; unsupported categories/search are not fabricated. |
| Title | `GuiMainMenu` | Corrected bottom-row offset, full second logo slice, panorama angular timing/projection and splash pulse/scale. Unavailable language control no longer advertises active pointer behavior. Existing Singleplayer/Options/Quit actions remain. |
| World selection | `GuiSelectWorld`, `GuiSlot` | Corrected row selection/text bounds, lower-button offsets and dark list shading. Added half-row wheel scrolling, clipped partial rows, edge gradients and draggable scrollbar; selection, keyboard scrolling and double-click hitboxes share the same offset. Refresh remains MaaGE-specific. |
| Create World | `GuiCreateWorld` | Name/seed fields and main button coordinates already matched. Corrected mode-help text Y=137/149; retained Survival default, More/Done, numeric seed rules and disabled unavailable settings. UUID storage replaces the vanilla folder-name hint. |
| Delete confirmation | `GuiYesNo`, `GuiSelectWorld` | Corrected message Y=70/90 and buttons to height/6+96. Permanent deletion remains a real explicit confirmation. |
| Rename | `GuiRenameWorld` | Corrected stacked 200x20 actions to height/4+108/132. Name field/title coordinates already matched. Text cursor now blinks. |
| Loading | `LoadingScreenRenderer`, `GuiDownloadTerrain` | Corrected the two local-world loading lines to center−20/+4; retained dirt background and truthful preparation text, without invented progress. |
| Video Settings | `GuiVideoSettings`, `GuiOptionsRowList` | Removed OptiFine-style categories; restored vanilla option order, list start/25-pixel rows, title Y=5 and Done at height−27. Existing functional FPS, GUI scale, window modes, VSync, Leaves and game-mode control remain in the original conventions. Wheel/keyboard scrolling keeps controls visible. |
| Sound Settings | `GuiScreenOptionsSounds` | Corrected Master to 310 pixels and Done to height/6+168. Existing 150-pixel category sliders, 24-pixel spacing, drag capture and release preview behavior needed no change. MaaGE's implemented buses remain. |
| Crafting Table | `GuiCrafting`, `ContainerWorkbench` | The 176x166 texture, 3x3 inputs at (30,17), result at (124,35), headings and inventory slots needed no coordinate change. Shared slot highlights, counts, tooltips and backdrop were corrected. |

The crosshair is one triangle-aligned inversion range in the existing UI batch.
Shared segment planning and pipeline factors select inverse-destination and
inverse-source color on both Metal and Vulkan. The derived GUI atlas makes
transparent crosshair pixels neutral; source artwork is unchanged. No second UI
framework or backend-specific layout was introduced.

## Lighting and State Ownership

The old dependency chain was mutation → Server lighting/boundary jobs → Server
CPU mesh → Session mesh mailbox → Client upload budget. Prediction could remove
geometry before that chain supplied nearby light. The Server no longer builds or
publishes visual meshes. Its separately scheduled light cache still supplies
random ticks, mushrooms, grass, melting, and other authoritative light queries.
An unavailable or stale gameplay-light query explicitly defers behavior.

Initial state packets contain numeric blocks and biomes, without lighting,
entities or scheduled-update payloads. Later packets contain changed cells and an
exact acknowledged base revision. A full transport queue retains the Server's
unsent revision; deltas are FIFO and cannot overwrite their required base. Block
revisions are independent of light/mesh epochs, so relighting sends no packets.
A column leaving the transfer interest radius releases its old delta base; a
later entry receives a complete publication. Remote serialization is not added.

The Client reuses portable World chunks, immutable snapshot ownership, numeric
registries, Jobs, light propagation and CPU meshing. Replica mode cannot generate,
run simulation, mutate through the authoritative setter or save. It retains
separate authoritative block copies and bounded expected-state break overlays.
Prediction changes its local light inputs immediately, without waiting for the
Server result. Rejection removes the overlay; confirmation retires it when the
required state revisions reach presentation. Valid original CPU/GPU presentations
are retained for rollback; later authority supersedes an obsolete rollback base.

Visual relighting runs asynchronously as one bounded 3x3 column transaction with
a retained outer halo. Changed-cell queues solve both increases and decreases.
Cached direct-sky shafts are recomputed only at changed X/Z positions, including
all affected Y levels when a roof opens or closes. Unchanged light is reused;
unknown neighbors contribute no light. All input epochs are checked before
publication, and affected cardinal/diagonal mesh dependencies are invalidated.
Independent mesh jobs run through Jobs and upload through the existing measured
GPU budget. The last complete presentation stays visible until replacement.
No render-loop job wait, global World lock, full-world relight, or empty-column
placeholder was added. Initial regions and large batches can take multiple frames.

The same numerical solver serves Server and Client schedules. Smooth sampling,
AO, biome tinting and shader light response remain shared with the existing
mesher/renderers. Day/night changes scale sky brightness in frame uniforms;
they do not trigger relighting. Player, hand, dropped-item and pickup visuals
sample Client light. There are no dedicated lighting-update messages or repeated
light arrays, including in item-entity publications.

## Mining and Punching

The reference swing lasts six ticks and can restart after three. The prior Server
loop restarted only after the entire swing ended and Client presentation waited
for a copied swing observation. One immediate Client action timeline now supplies
both first- and third-person progress. Presses swing immediately; held mining
requests restart at reference tick cadence; a miss does not continuously punch
air. Releasing stops new requests but lets an existing swing finish. Attack echoes
and prediction confirmations do not restart it. Successful placement has a
separate authoritative action identity. The Server's own action timing follows
the same six/three rule.

The existing hand/tool swing arc, sprite transforms, equip speed, fixed 70-degree
foreground projection, bob/lag interaction and shared biped pose already follow
the examined transforms and were preserved. Crack stages already used
floor(progress×10)−1 and multiplicative overlays; those rules remain. The local
raycast now clears cracks immediately on release/target change while progress
continues to come from authoritative mining. No second authoritative mining
simulation, predicted inventory, drops or sounds was added.

## Neighbor Support

Immutable numeric block definitions register survival callbacks shared by
placement and neighbor validation. Every ordinary expected-state mutation queues
the affected supported cell and six neighbors before changing the block. Queue
allocation is preflighted; duplicates require no allocation. The existing heap
is unique by (kind,cell) and processes at most 128 scheduled updates per owner
phase, retaining the rest. Cascades are iterative, not recursive. Missing support
or stale gameplay light retries on a later tick. Loading/generation schedules
support validation, so edits missed while a neighbor was absent are revisited.
Queues and remaining delays persist in `MGCHNK5`.

| Block | Current survival rule | Automatic support-loss drop |
| --- | --- | --- |
| Dandelion, poppy, tall grass | Grass block or dirt below | Flowers drop themselves; tall grass uses the existing 1/8 seed rule. |
| Dead bush | Sand or dirt below | None without shears. |
| Floor/wall torch | Actual metadata-selected full solid support face | Torch, metadata normalized by the existing item-drop rules. |
| Cactus | Sand/cactus below and no solid-material horizontal neighbor | Cactus; stacked support loss cascades. |
| Snow layer | Valid opaque solid ground, leaves, or full snow layer; ice excluded | None, matching the neighbor callback rather than tool harvesting. |
| Brown/red mushroom | Opaque substrate and full sky/block light below 13 | Matching mushroom. Current content has no mycelium/podzol exception. |
| Leaves | Existing natural/persistent flags, log marking and random decay | Existing ordinary leaf-drop rules; no immediate leaf removal. |

Ordinary grass blocks do not acquire a plant support requirement. Automatic loss
is environmental, including when Creative removes the support; direct Creative
mining still follows its existing no-drop rule. Drops use existing item generation
and pre-reserved item entities, never inventory insertion. Duplicate notifications
observe the already changed numeric state and cannot drop the block twice.

## Shared Interfaces and Source Changes

- `include/maage/{world,blocks}.h`: replica configuration, owned state packets,
  independent state/presentation revisions, local light queries and survival hooks.
- `include/maage/{local_session,session_protocol}.h`: ordered column transport,
  protocol 11 and separate swing action observations; obsolete mesh transport removed.
- `include/maage/{entities,gameplay,environment}.h`: presentation light removed from
  authoritative entities, swing identity, and persistent support update kind.
- `include/maage/render.h`, shared renderer and Metal/Vulkan adapters: UI inversion
  ranges, common segment planning, blend factors and native pipelines. Shader
  source and GPU vertex layouts did not change.
- `src/world/`: shared incremental solver, replica imports/prediction overlays,
  state deltas, retained immutable snapshots, ready-mesh queue, support callbacks,
  queue persistence, placement preflight and Server swing cadence. Existing leaf
  decay and random sampling remain in place.
- `src/client/`: replica lifecycle/Jobs/uploads, prediction rollback, common swing,
  local player/entity light and all existing screen implementations/resources.
- `src/server/runtime.c`, `src/session/local_session.c`: state publication and
  backpressure replace visual mesh publication.
- `AGENTS.md`, application versions and related architecture documentation were
  reconciled. Launcher, README, Java source and local worlds were not edited.

### Modified File Inventory

- Project: `AGENTS.md`, `CMakeVersions.cmake`.
- Documentation: `docs/development-roadmap.md`, `docs/gameplay-parity.md`,
  `docs/item-entities.md`, `docs/random-block-ticks.md`, `docs/visual-fidelity.md`.
- Public contracts: `include/maage/blocks.h`, `include/maage/entities.h`,
  `include/maage/environment.h`, `include/maage/gameplay.h`,
  `include/maage/local_session.h`, `include/maage/render.h`,
  `include/maage/session_protocol.h`, `include/maage/world.h`.
- Client presentation: `src/client/interaction.c`, `src/client/interaction.h`,
  `src/client/item_entities.c`, `src/client/item_entities.h`,
  `src/client/player_view.c`, `src/client/player_view.h`,
  `src/client/prediction.c`, `src/client/prediction.h`, `src/client/viewport.c`.
- Client UI: `src/client/ui/draw.c`, `src/client/ui/front.c`,
  `src/client/ui/front_internal.h`, `src/client/ui/gameplay.c`,
  `src/client/ui/internal.h`, `src/client/ui/options.c`,
  `src/client/ui/resources.c`, `src/client/ui/title.c`, `src/client/ui/ui.c`.
- Rendering: `src/core/render/render_shared.c`, `src/core/render/render_shared.h`,
  `src/core/render/metal/render_metal.m`, `src/core/render/vulkan/render_vulkan.c`.
- Authority/transport: `src/server/runtime.c`, `src/session/local_session.c`.
- World: `src/world/CMakeLists.txt`, `src/world/chunk.c`, `src/world/content.c`,
  `src/world/entities.c`, `src/world/environment.c`, `src/world/gameplay.c`,
  `src/world/lighting.c`, `src/world/random_ticks.c`, `src/world/store.c`,
  `src/world/world.c`, `src/world/world_internal.h`, and new `src/world/support.c`.

## Compilation and Limits

macOS ARM64 Debug and Release Client/Server compile and link with Metal and
Vulkan/MoltenVK enabled. The independent headless Release Server compiles and
links with rendering disabled. Affected portable C sources also compile with
`-std=c90 -pedantic-errors`. The existing Client duplicate-library linker warnings
remain. No applications, tests, CTest, harnesses, benchmarks or Actions ran.
Compilation does not establish visual fidelity or imperceptible update latency.
Windows/Linux compilation and all runtime/manual scenarios remain unverified.

Prediction retains its existing conservative exclusions for leaves, ice, opaque
breaks beside fluids, and unavailable/unlit neighborhoods. Those changes still
reach the Client through authoritative state deltas and use local visual lighting.
Creative is the implemented single catalog, not a fabricated full category/search
system. Unsupported controls/content remain disabled or absent. Large initial
light/mesh batches remain asynchronous and must be judged manually.

## Pending Programmer Verification

No scenario below is marked as passed. Repeat visual checks with both renderers.

- **GUI:** Compare every screen above with 1.7.10 at multiple GUI scales and window
  sizes. Inspect text, textures, button bounds/disabled/hover, pointer behavior,
  inventory/crafting slots, carried counts/tooltips, Creative scrolling, world-list
  partial rows/double clicks, navigation, Pause and fullscreen notch-safe content.
- **Lighting:** Break/place a torch in a dark cave; open and close a daylight hole;
  break opaque blocks beside a torch; rapidly break adjacent blocks; repeat across
  chunk seams and with missing neighbors. Inspect source removal, day/night,
  prediction rejection, retained geometry, and absence of black flashes/empty chunks.
- **Mining:** Punch empty-handed; use each tool; hold/release halfway; change target;
  instant-break flowers; break consecutive blocks; compare first/third-person
  cadence, tool/equip/bob/FOV, cracks, cancellation, and confirmation/rejection.
- **Support:** Remove flower/tall-grass/dead-bush ground, floor/wall torch supports,
  cactus bases and side clearance, mushroom terrain, and snow supports. Inspect
  cascades, correct metadata/chance/quantity and no duplicate drops. Remove logs;
  natural leaves must retain random decay and placed leaves must remain persistent.
  Repeat near unloaded boundaries, after saving/reopening, and in Creative.
