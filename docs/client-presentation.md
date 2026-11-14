# Client Presentation

The Client owns UI, controls, camera, sound and visuals, consuming copied Server state. Public contracts: `include/maage/{render,input,presentation,session_protocol,skin}.h`. Durable requirements: [AGENTS.md](../AGENTS.md).

## Screens and Controls

`src/client/ui/` owns the numeric screen registry, stack, layout, atlas/font and hit testing; `frontend.c` owns lifecycle, catalog Jobs and options persistence.

| IDs | Screens |
| --- | --- |
| 0–3 | Gameplay (HUD/F3), Pause, Options, Inventory |
| 4–9 | Title, Worlds, Create World, Delete confirmation, Rename, Loading |
| 10–13 | Video, Sound, Crafting Table, Controls |
| 14–15 | Credits, Saving world |

Pause/Options pause gameplay and music, not streaming. Inventory/table and window focus loss release capture without pausing; focus return restores gameplay capture when no modal is open. World operations and Save/Quit are asynchronous; confirmed rename/delete wait for background save and lease release, keeping the selected UUID. Save/Quit displays a responsive Saving world screen until teardown completes, then opens Title. Window close/Quit hides immediately before teardown. Loading and dimension entry reuse the title panorama with darkening, display actual received/prepared/submitted states for the player's nearby columns (nearest centered square to 13% of the full render-distance square, minimum 3x3, no extra cap; out-of-world cells excluded) and show progress from submitted columns. The map sizes its cells to fit the GUI, averaging neighboring cells only when individual columns cannot fit; the percentage always counts actual columns. Bootstrap/destination preparation is indeterminate. The normal frame loop continues streaming and GPU publication behind the non-pausing screen; no artificial delay or full render-distance barrier.

Music uses registered content playlists and one active native music voice. A session/world epoch or playlist change stops the previous voice and releases its decoder before applying the new playlist's initial delay. Native World resets also stop the music bus, and admitting a new music stream first retires any remaining voice on that bus. DEBUG audio-scene logs identify track starts/stops and their world epochs; tracks decode only while selected.

Auto GUI scale selects the largest integer backing-pixel factor preserving 320×240 logical units (minimum 1). Small/Normal/Large request 1/2/3; larger supported factors are available. Temporary clamping preserves the saved choice. Safe-area margins apply after scale selection. Drawing retains exact framebuffer/factor extents while layout rounds up; native top-origin pointer coordinates use the inverse transform, including Retina/fractional DPI. All screens, HUD and splash share it. Text input/font remain ASCII.

Built-in Control shortcuts use Command on macOS, including editing/select-all, whole-stack actions and the initial/reset sprint binding. Saved and manually assigned physical Control bindings retain their exact HID usages; no raw key remapping occurs.

Growable action bindings use USB HID usages or one-based mouse buttons. Remapping waits for a new edge; Escape unassigns, focus loss cancels, duplicate bindings remain independent. System Escape/F3/F11 and GUI modifiers are separate. `config/options.ini`: `[controls]` values `key:<usage>`, `mouse:<button>` or `none`; `[mouse]` sensitivity 0–100 (50 default), invert; `[video]` render distance defaults to 12 (normally 1–32, larger valid saved values supported). CLI overrides startup preferences.

Credits scroll at ten logical pixels per second with the original logo, subdued scrolling dirt background and edge fades; Escape and natural completion use the same validated session command. Entries come from the immutable content presentation registry in append order, including reconstructed end-poem/original-credit text followed by MaaGE and additional package credits. Text loads once during UI creation, wraps before drawing and substitutes the selected display name; missing optional resources log and leave remaining entries available. The existing ASCII font limits text coverage. The owner freezes gameplay while credits are active, but streaming and the registered credits music remain available; UI completion waits for authoritative acknowledgement.

Portal presentation uses an optional numeric screen texture (animated Nether portal) or a color fade, with per-epoch transition smoothing; Nether screen effects are Survival-only. Nearby portal particles emit on a staggered per-cell cadence; ambient, enter and travel sound gains are content-owned. End portal starfield fragments reconstruct the world-space view ray and sample eight virtual depth planes behind the surface, with bounded grazing-angle projection and continuous coordinates across blocks. Metal, Vulkan and modern OpenGL share the shader without new vertex or uniform storage.

## Creative Inventory

Registered Creative tabs use a 195×136 panel, 9×5 catalog, nine-slot hotbar and original registered tab/background resources. Extra categories page ten at a time; Search and Survival Inventory remain on the right. Reopening retains the tab, resets scroll and clears the query. Search filters ordered entry IDs against variant names/tooltips, case-insensitively, up to 15 ASCII characters. T opens Search; focused text takes precedence over the inventory key, while Escape closes.

Catalog clicks acquire/adjust carried stacks, Shift fills, middle-click clones into an empty cursor, numbers replace hotbar slots, and drop bindings reserve world drops. Player slots use shared stack transactions; catalog Shift-hotbar deletes, while Survival Inventory transfers between storage and hotbar. Survival Inventory shows real storage and a destroy slot; unsupported equipment is decorative. Shift-destroy clears inventory/cursor/temporary inputs. Outside clicks and closure use the owner drop path.

The UI retains typed FIFO requests across backpressure, one container action in flight; Creative actions acquire the next acknowledged revision, output slots keep their observed result revision. Search/tab changes remain local, captured tab scope travels with requests, and no authoritative stacks change on the Client.

## Replica and Asynchronous Presentation

Session sends numeric block/biome columns, acknowledged-revision cell deltas and ordered removals—not light, meshes or live pointers. The replica owns lighting and geometry Jobs on immutable snapshots; obsolete results are rejected/rebased. Geometry can precede light refinement. Missing terrain obstructs, predictions are local overlays, and inventory is authoritative.

Near/forward terrain and collision cells have priority. First reveal advances eight directional sectors in two-ring groups, four concurrently. A bounded audit scans cached revisions once per batch to requeue loaded columns missing GPU geometry and pending mesh work (including empty meshes). Keep old presentation until replacements arrive; distance reduction retires Client meshes independently of transport unloads. No rendering wait for disk work. Video Settings Chunk Fade defaults to 750 ms (OFF–2 s in 250 ms steps, persisted as `chunk_fade_ms`). Newly presented columns ease from full fog to normal shading, with the initial arrival area immediately clear and surrounding pending fades starting when terrain is revealed. Mesh revisions and prediction index views retain the column timestamp; retired columns start fresh on re-presentation. Direct mesh uniforms reuse the unused light-override component; fog blending preserves depth, alpha, and multiply neutrality across Metal, Vulkan and modern GL without additional draw calls or uploads.

## Rendering and Mesh Lifetimes

Common C owns scene/material plans, UI batching, surface tables, mesh preparation and translucency. Metal/Vulkan/modern GL own GPU upload, synchronization and display; legacy GL/Software expose fewer capabilities (`render.h`, `src/core/render/`).

Face merging requires compatible material, UV/animation and corner light/tint/AO. Irregular/sloped/flowing or varying-shade geometry retains separate quads; flat fluid tops merge only with identical height and compatible uniform shading. Immutable mesh bounds survive upload and support conservative culling without altering residency or reveal order. Packed vertices reference deduplicated surface records. Opt-in natural texture rotation hashes the owning world cell in the shared fragment shader, including merged quads, without extra world state or vertex/surface stride changes. Varying-shade rotated faces repeat UVs but remain unmerged. Translucency sorts triangles from one camera reference refreshed after half-block motion, then heap-merges chunk and transformed instance streams into contiguous index ranges; intersecting surfaces remain approximate. Incremental uploads, frame-slot ownership and deferred GPU retirement bound work.

Registered models share part/cuboid/provider APIs for mobs, block entities and item geometry. Chests use rear-hinged lids and inherited latches, with 64×64/128×64 sheets; copied poses are gated by replica readiness. Larger registered containers use background slices and paged 54-slot copied windows. Living instances retain interpolated body/head/limb poses, reset on discontinuity, and show accepted damage as ten-tick tint/recoil and eased death roll. Model-specific animation stays in callbacks. RGB overlays preserve alpha/light/fog within the 128-byte draw uniform; spawn eggs use ordered tinted layers. Model/item caches are resource-generation-owned.

Client LOAD resolves registered textures, padded rectangular model sheets, atlas, appearances and biome colors. Missing optional images use fallbacks. Shared shading handles foliage, AO, tint, fluid, fog, animated textures and foreground items; daylight scales sky contribution only.

F3+T stages atlas, GUI/font/HUD, environment and item resources on the render owner. Failure retains the prior complete generation. Successful publication swaps the replica appearance, invalidates obsolete meshes and rebuilds local geometry without waiting; logical IDs, pending actions and Server state survive. CPU views outlive borrowing Jobs (including shutdown), GPU resources retire through backend rules, and another reload waits while a retired generation remains. No re-registration, INITIALIZE or audio refresh. Resource-pack selection, alternate roots and shader reload are future work.

See [diagnostics](diagnostics.md) and [AGENTS.md](../AGENTS.md) for shared logging, filtering and error policy.

## Player Appearance and Cameras

F5 cycles transient First/Back/Front during captured gameplay. The third-person four-block boom uses local 11³ collision samples, including predictions; unknown terrain obstructs. Targeting/audio remain at the player eye. One interpolated model drives arm, preview and world body using cached geometry; duplicate snapshots do not restart swings or damage. Server-owned sprint changes world FOV separately from saved base/foreground projection; Creative flight uses independent horizontal acceleration (0.05), momentum retention (0.88) and a 1.5x sprint boost for smoother, slower cruising; vertical control and ground sprinting are unchanged.

The Launcher verifies/caches the selected profile's ACTIVE skin at `minecraft/skins/<uuid>/`; failures preserve profile-scoped known-good/default appearance without affecting authorization. Client receives a versioned nonsecret reference and normalizes classic/slim 64×64 or legacy 64×32 skins once, including independent limbs/layers. No per-frame I/O.

Block fragments consume deduplicated authoritative break occurrences independently of prediction. A reusable 20 Hz billboard pool interpolates opaque fragments, samples source biome color and uses sixteen cached quarter-texture patches; resource replacement clears it. Skeleton bows/arrows use registered models.

## Current Limits

Equipment storage, enchanting, potions, capes, remote players, complete combat/Survival, wider effects and secondary renderer coverage remain incomplete. Wayland lacks relative gameplay capture. Optional macOS private LaunchServices Dock naming is best effort (`MAAGE_CLIENT_MACOS_DOCK_NAME=OFF` disables it); failure preserves the system label. Source contracts do not establish runtime correctness.
