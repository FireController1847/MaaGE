# Gameplay and presentation parity pass

This pass implements the requested pickup, mining, presentation, container audio,
inventory and frontend corrections. Client is `0.1.0-alpha.47`, Server is
`1.0.0-alpha.17`, and the local session contract is version 8. World metadata
remains `MGWORLD4`; this pass adds no save reader or migration.

## Reference inspection

The supplied local Forge 1.7.10 `unpacked` directory was inspected directly. Its
contents are Forge/FML sources; the corresponding vanilla classes were read from
the adjacent `forgeSrc-1.7.10-10.13.4.1614-1.7.10-sources.jar`. The original
reconstructed resource installation supplied `sounds.json` and GUI/skin textures.
No Java runtime, compatibility layer or downloaded replacement resource is used.

| Issue | Original classes and methods consulted | Confirmed discrepancy and change |
| --- | --- | --- |
| 3: pickup | `EntityPlayer.onLivingUpdate`, `EntityItem.onCollideWithPlayer`, `EntityPickupFX.onUpdate/renderParticle` | Existing expanded collection bounds already matched. The missing element was the three-tick post-collection flight. Confirmed transfers now animate over 0.15 seconds with squared interpolation toward feet + 0.5. No speculative pre-pickup magnet was added. |
| 4: tools | `ItemRenderer.renderItemIn2D/renderItemInFirstPerson`, `RenderItem.doRender/renderItemIntoGUI`, item/tool definitions | The extruded sprite front and pixel-edge samples disagreed horizontally. Shared UVs now match the reference. Flat GUI/hotbar sprites and existing centered first-person transforms were already appropriate and remain shared. |
| 5: breaking | `Minecraft.clickMouse/sendClickBlockToController`, `PlayerControllerMP.clickBlock/onPlayerDamageBlock`, block selection/relative-hardness methods | Display waited for authoritative mutation, lighting, mesh jobs, mailbox delivery and budgeted GPU upload. There was no local predicted removal. A bounded presentation layer now filters the voxel and reconstructs newly exposed faces, then reconciles Server results/revisions. |
| 6: container music | `GuiContainer.doesGuiPauseGame`, `GuiScreen.doesGuiPauseGame`, `Minecraft.displayInGameMenu/runTick`, `MusicTicker.update`, sound pause methods | Every modal sent the same simulation pause, which also paused music. Inventory/table screens now release capture while simulation and music continue. Effects and music have separate pause controls; true Escape pause still freezes music. |
| 7: mining | `PlayerControllerMP.clickBlock/onPlayerDamageBlock`, `Block.getPlayerRelativeBlockHardness`, `EntityPlayer.getBreakSpeed`, `BlockFlower` | Fresh Survival clicks inherited a previous charged-break delay, and target initialization discarded initial charged progress. Fresh presses now clear the retained controller delay and contribute that tick's damage. Zero hardness explicitly returns immediate damage. |
| 8: inventory | `GuiInventory.drawGuiContainerBackgroundLayer/func_147046_a`, `GuiContainer`, `RenderPlayer`, `RenderManager`, `ModelBiped`, `ModelBox`, `RenderHelper` | The 176x166 panel and existing crafting/grid slots were retained. The player preview was absent and an extra Inventory heading crossed the texture layout. Added a textured biped at (51,75), scale 30, with mouse-dependent head/body rotation and corrected heading visibility. |
| 9: menus | `GuiCreateWorld.initGui/drawScreen/actionPerformed`, `GuiSelectWorld.initGui/drawScreen`, `GuiSlot` | Creation combined name/seed in a customized layout and defaulted to Creative. It now defaults to Survival, separates More World Options and uses the original button/field hierarchy. Selection uses centered 36-pixel rows and the original two-row action layout while preserving Refresh. |
| 10: menu music | `MusicTicker` constructor/update/context delays, `Minecraft.func_147109_W`, original `sounds.json` | Menu category and the initial 100-tick equivalent (five seconds) were already correct. World replacement unnecessarily stopped/rearmed a track even when its category matched. That reset was removed; category transitions retain their own scheduling. |

The reported transparent-world flash was not reproduced because runtime execution
was prohibited. Source inspection showed that normal mesh replacement already
retains the old GPU mesh until a complete replacement uploads. It did establish
the full-mesh delay. This pass does not claim a runtime diagnosis of every possible
flash; the new path never substitutes an empty whole column for one removal.

## Prediction, authority and ownership

The Server copies a revision-keyed 3x3x3 neighborhood around its target, packed
light, a 5x5 biome halo, reach, mining conditions and controller delays into its
existing snapshot. The neighborhood is resampled only when its center/relevant
column revisions change or the previous sample was unusable. All accesses remain
on the World owner; the Client never borrows columns or calls live World queries.

The Client ray-tests the copied numeric shapes and shares the portable damage
calculation with authoritative gameplay. When enough consistent mesh/neighborhood
information exists, it records one of at most 32 pending actions. Immutable CPU
mesh handoffs retain a numeric voxel owner per quad. A temporary GPU view removes
only that voxel's indices; original CPU/GPU geometry remains available for rollback.
New neighboring boundary faces use the existing face emitter, UVs, log orientation,
materials, biome colors, overlays and copied light/AO samples. Lighting in the
opened cell is a local estimate; authoritative light propagation still runs on
the Server and replaces it with the final mesh.

Prediction is conservative when information is insufficient. Leaves/ice removals
and opaque removals beside fluids wait for authoritative geometry, as do missing,
stale or unlit neighborhoods. This avoids guessing fluid surfaces or distant Smart
leaf coverage from a small sample. Other instant plants/torches need no newly
occluded faces reconstructed. Rapid actions can coexist in the bounded cache.

A numeric preview request carries an action ID and expected target alongside the
existing held-input stream. The Server independently raycasts, checks attack,
health/container/pause state and records the expected target. It never accepts
Client hardness, progress, inventory or a claimed mutation. Actual gameplay
mutations resolve the request; rejection restores the retained presentation.
Confirmation carries required column revisions. Temporary faces retire as each
authoritative neighboring mesh catches up, and the hole retires once the affected
resident columns are current. Session/world epochs and monotonic IDs isolate old
actions. Sounds, drops, particles and inventory are never predicted or duplicated.

Resolved results remain in the bounded Server action records when the event queue
is full. Optional prediction allocation/upload failure restores authoritative
geometry and suspends new predictions until existing results drain and a fresh press retries. GPU handles
are replaced/destroyed through the existing renderer ownership path. CPU sources
live only with resident render columns; unload/world teardown releases both views.
No global World lock, render-loop Server wait or second world simulation is added.

## Communication and pickup behavior

Unchanged held input is no longer sent each rendered frame. Changed levels/view
and fresh edges still enter the ordered bounded outbox; unsent edges stay latched.
Each predicted completed break adds one request and one necessary result. Copied
neighborhoods enlarge the existing snapshot; there are no per-frame state queries,
additional state-query round trips or separate rejection polling.

Pickup remains a Server inventory transaction. The existing aggregate pickup cue
now carries up to 32 numeric transfer records in one event per owner step, rather
than one extra message per item. Capacity/partial transfers preserve quantities;
additional eligible transfers wait until the next step. The Client keeps at most
32 short animations and uses cached shared item meshes. Optional visual overflow
may omit decoration; it cannot create, lose or recollect items. Mined/Q-drop delays
remain ten/forty ticks, and unavailable terrain still freezes retained entities.

## Inventory, rendering and frontend

The preview builds textured player cuboids entirely on the Client. The subsequent
[authenticated skin integration](player-skins.md) extends its original Steve
appearance with classic/slim geometry, modern independent limbs and outer layers,
using the same normalized atlas resource as first-person arms. Independent head/body mouse rotations use the
reference `atan(delta/40)` and 20/40-degree factors, converted to MaaGE coordinates.
Shared face lighting, backface culling, depth sorting and polygon clipping produce
textured 3D quads in the existing GUI batch; no black offscreen surface, private
depth buffer, second GUI renderer or native 3D-in-GUI API is introduced. The panel
retains its crafting, result, main inventory and hotbar coordinates and scale.

Tool extrusion, prediction geometry and preview calculations are shared C code.
Metal and Vulkan consume the existing mesh/UI contracts, transformed-item path,
nearest atlas sampling and alpha rules. Neither gets a private rendering algorithm;
native shaders and GPU vertex layouts are unchanged. Compilation confirms native
integration only, not matching images or shader runtime behavior.

Creation separates the basic name/mode page from seed/options. Structures and
default world type are shown disabled at their implemented fixed settings; cheats
and bonus chest are explicitly unavailable. No unsupported generation mode is
enabled. Name/seed editing, signed 64-bit parsing, blank random seeds, existing
storage/jobs and lifecycle remain intact. Empty/whitespace names and invalid numeric
seeds cannot submit. Tab/focus/Enter/Escape follow visible controls. Selection
preserves play, create, rename, confirmed deletion, refresh and cancel operations.

## Music timing

Music uses monotonic seconds independently of FPS and Server ticks. Menu music
selects the four original menu tracks. First eligibility remains five seconds;
normal post-track menu gaps are 1–30 seconds, Survival 600–1200 seconds and Creative
60–180 seconds. Creative includes the original game pool plus its six additional
tracks. A playing track changes category by stopping and scheduling within zero
to half the new minimum gap. Matching contexts, inventory and table transitions
do not restart it. Escape pause freezes playback and the music deadline; simulation
effects retain their deliberate pause behavior. Asset failure/retry and volume
controls retain the existing resource/error contracts.

## Compilation and remaining limits

- macOS ARM64 Debug Client/Server compile and link successfully.
- macOS ARM64 Release Client/Server compile and link successfully, including fresh
  Metal Objective-C and Vulkan/MoltenVK implementation compilation.
- The independent headless Release Server compiles and links successfully.
- The Client linker retains existing duplicate-library warnings. No applications,
  tests, CTest, fixtures, benchmarks, synthetic harnesses or GitHub Actions ran.
- Windows/Linux compilation, runtime shaders, visual/audio behavior and the manual
  scenarios below remain unverified. Authenticated account skins are covered by the
  [skin integration](player-skins.md); equipment and player animation remain absent.
- Prediction has the conservative coverage and temporary lighting limits above.
  Remote transport/serialization is future work; C layouts are not a wire format.
- Supporting-block/neighbor-update propagation remains deferred: a flower can
  remain after its supporting block disappears. Loading-screen redesign is also
  deferred. Neither subsystem was expanded in this pass.

## Programmer verification checklist

These are pending manual scenarios and expected results, not completed checks.
Repeat the visual groups with Metal and Vulkan at several GUI scales.

- **Pickup:** Walk within expanded bounds of mined/Q-dropped stacks after their
  delays; observe collection and short flight without precise rendered contact.
  Repeat with a full inventory, one partially available stack and a dense pile;
  confirm residual quantities, one pickup cue per collecting step and no duplicate
  entity after reopening the world.
- **Mining:** Fresh-click flowers, tall grass, torches and consecutive plants in
  Survival. Try fresh presses immediately after a charged stone break, held mining
  while changing targets, Creative repeats, missed presses and changed tools.
  Instant objects should break on the initial authoritative action; deliberate
  charged/Creative held-controller delays should remain. Compare airborne/water
  mining and authoritative sound/tool durability timing.
- **Prediction:** Break ordinary opaque blocks rapidly, including column seams
  and tinted/log surfaces. Observe immediate local holes with neighboring faces,
  stable surrounding terrain and no duplicate sound/drop. Turn away, release
  attack, change tool, open a container or leave a world during pending actions;
  rejected/stale results must restore/retire correctly. Observe the conservative
  fluid/leaf/ice cases and compare final lighting once authoritative meshes arrive.
- **Tools:** Inspect each existing pickaxe/axe/shovel in main inventory, hotbar,
  first person and world drops. Compare handed orientation, edge colors, transparent
  pixels, sprite thickness, transforms, lighting and nearest pixel sampling.
- **Containers and audio:** Open/close inventory and tables during a playing track
  and a scheduled gap. Music and gameplay should continue with volumes intact;
  Escape pause should freeze them deliberately. Craft, move/split stacks, close
  with items pending, and save/reopen to check authoritative quantities.
- **Inventory preview:** Compare the 176x166 layout with 1.7.10. Move the cursor
  above/below and side to side; head/body should follow smoothly, skin/hat should
  remain transparent where intended, and geometry must stay inside the preview
  region without a black rectangle or depth artifact. Check all slots/tooltips.
- **World screens:** Compare Select World row/button spacing, keyboard selection,
  scrolling, double-click play, rename, confirmed deletion and Refresh. Create
  Survival/Creative worlds, switch More/Done, enter negative/large/blank seeds,
  reject invalid seeds/names and verify persistence of supported settings. Disabled
  controls must never claim to change generation or enable cheats/bonus chests.
- **Music contexts:** From a fresh title session, allow the intentional initial
  five-second eligibility delay. Verify menu tracks, subsequent gaps and transitions
  into/out of Survival/Creative without an extra reset gap or incorrect pool.
- **Renderer comparison:** Repeat prediction, tools, preview, alpha edges and GUI
  scaling on both primary backends; inspect resizing/fullscreen and world teardown
  for stale geometry or resource lifetime faults.

Git attribution follows the repository convention; the completion response gives
the commit ID. Concurrent Launcher/window/icon edits and the pre-existing
`.DS_Store` change are outside this parity commit.
