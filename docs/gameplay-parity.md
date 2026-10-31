# Client UI and Visual World

The Client owns UI, camera, visual lighting, meshes and presentation prediction.
The integrated and dedicated Server share one authoritative simulation. All
runtime and visual verification remains programmer-owned.

## Screens and Input

Screens register before finalization and use direct numeric indexing. The stack
pushes submenus and pops on Back/Escape; Gameplay resumes play and Title remains
the frontend root. Unknown IDs and cycles fail explicitly.

| ID | Screen | Current Layout and Behavior |
| --- | --- | --- |
| 0 | Gameplay HUD | Centered hotbar, 24x22 selection, health/hunger at height−33 (no experience row), right-aligned counts and a 16x16 destination-inverted crosshair. Held names last two seconds with a final half-second fade. |
| 1 | Pause | Title at Y=40; quarter-height rows for Resume, Options and Save/Quit. |
| 2 | Options | Two 150-pixel columns, FOV endpoint labels, persistent supported settings and Done navigation. |
| 3 | Inventory | Survival panel 176x166; player preview at (51,75), 2x2 inputs and result. Creative uses a 195x136 panel, five rows of nine catalog items, hotbar at (9,112), wheel/drag scrollbar. |
| 4 | Title | Horizontal panorama with fixed tilt, projected logo, pulsing splash, Singleplayer, Options and Quit. Multiplayer is unavailable. |
| 5 | Worlds | Scrolling/clipped rows, keyboard selection, double-click Play, scrollbar, Refresh and world actions. |
| 6 | Create World | Name/seed fields, Survival/Creative selection, More/Done and mode help at Y=137/149. |
| 7 | Delete confirmation | Messages at Y=70/90 and actions at height/6+96; deletion requires explicit confirmation. |
| 8 | Rename | Name field and stacked 200x20 actions at height/4+108/132. |
| 9 | Loading | Dirt background and truthful preparation text at center−20/+4; no fabricated progress. |
| 10 | Video | Scrollable settings at 25-pixel row spacing; title Y=5, Done at height−27. |
| 11 | Sound | Master width 310, category sliders 150 wide with 24-pixel spacing; Done at height/6+168. |
| 12 | Crafting Table | Panel 176x166, 3x3 inputs at (30,17), result at (124,35), shared inventory interaction. |

Drawing and hit testing share layout calculations. Logical dimensions round up
independently of framebuffer size; native safe areas bound application controls.
Buttons use unscaled horizontal texture halves. Slider tracks stay dark while
interactive, with enabled handles. Hovering enabled controls requests the pointer;
primary hold uses an arrow. Disabled controls and keyboard focus do not request it.
World-list clicks share the drawn row bounds, and keyboard navigation skips
disabled actions while catalog work runs. Tooltips keep their padded bounds inside
the safe viewport; carried items, counts and half-opacity slot highlights
share the inventory path. Text entry is ASCII with backspace and blinking cursor.
Unsupported controls remain disabled or absent. There is one Creative catalog,
without fabricated category/search functionality.

Catalog/admin I/O runs on bounded Jobs work. Title starts without a Server;
Play starts the integrated runtime. Save/Quit flushes and joins before returning.
Pause and Options stop gameplay; inventory and tables keep simulation/music active.
The death screen keeps the owner clock running for the death transition and respawn.
Modal screens release capture, and closure restores it when appropriate. Local
look angles stop immediately on release and remain fixed through menu closure;
delayed Server angle echoes never rotate the view or become new input commands.

HUD damage flashes use the existing alternate heart outlines and pre-hit full/half
heart sprites behind current health. A new authoritative damage occurrence starts
the classic three-tick blink phases within its first ten ticks; duplicate snapshots
never restart it. At four health or less, each heart jitters downward by zero or
one logical pixel. Empty saturation shakes food icons by up to one pixel on the
`food * 3 + 1` tick cadence. Cosmetic variation is stable within each owner tick,
independent of render FPS and gameplay randomness; pause freezes it, and new
worlds/lives/modes reset flash history. The accepted height−33 baseline is retained.
Regeneration-potion waves and status-specific icons await those gameplay effects.
Air bubbles already use the popping sprite for the last two ticks before each
bubble disappears; resurfacing immediately hides the air row.

## State Transfer and Visual Lighting

The Server publishes owned numeric block/biome columns initially, then changed
cells against exact acknowledged block revisions. Per-column FIFO never coalesces away
a required delta base; backpressure retains unsent state and changed-cell bits.
Subsequent packets enumerate deduplicated mutated cells rather than comparing every
section. A successful queue transfer advances the producer base only after the
packet owns all necessary state. Ordered unload packets close an interest lifetime;
the replica retains bases until that removal arrives. Reentry starts with a complete
column. Initial local capacity covers Server residency plus bounded packets in flight;
remote transports must negotiate an equivalent bound. Packets contain no light arrays,
scheduled work, camera collision samples, ambient candidates or visual meshes. Protocol is 15;
remote encoding is not implemented.

Snapshots include per-life raw travel/velocity observations and authoritative
damage occurrence/tick/bearing markers. FIFO player-motion events retain raw
intermediate owner steps for Client camera interpolation when snapshots coalesce.
They carry no camera matrices or animation filters and use bounded existing
event transport; omitted samples produce explicit partial presentation recovery.

The presentation-only World replica reuses numeric chunks, immutable snapshots,
registries, Jobs, light propagation and CPU meshing. It cannot generate, simulate,
mutate through the authoritative setter or save. Separate authoritative copies
anchor expected-state prediction overlays. Block revisions are independent of
light/mesh epochs, so relighting creates no state traffic.

Visual lighting has one exclusive output column per job and immutable facing
boundary inputs. Multiple columns and their mesh jobs run independently on the
existing bounded Jobs pool. Dirty-cell bitsets and four boundary masks coalesce
work; direct-sky caches restrict rescans to affected X/Z shafts. Both channels
retain the attenuating removal/addition fixed point. Missing boundaries contribute
no numerical light and are revisited when membership changes.

Owner publication validates incarnation and local/facing opacity/emission epochs. Results
from incompatible block inputs are discarded with their consumed dirty cells
requeued; compatible auxiliary edits retain their current data while merging light.
Changed facing boundaries request another pass. Geometry epochs are
separate from light epochs: correct geometry with older illumination can upload
while refinement stays pending. Block/membership changes still reject obsolete
geometry. Cardinal and diagonal AO meshes refresh after relevant boundary changes.
No worker mutates published buffers, and no render-loop wait or global World lock
is needed. Prior GPU data remains usable until replacement, with local edit patches
covering interactions immediately.

The Server independently schedules the same numerical solver for gameplay light.
Growth, melting and survival defer while the queried cache or its input halo is
unavailable/pending. This readiness gate does not apply to Client geometry. The Client
samples its own light for player, hand, item entities, pickup effects and F3.
Block, first-geometry, light-initialization, pending-light and GPU-ready states
are separate. First geometry uses a roof-aware sky approximation until numerical
light exists. Light queries distinguish calculated, retained/approximate and
unavailable values; pending actor samples may use adjacent cached illumination.
The initial sky approximation never enters propagation buffers. At an edited cell
with initialized light, a bounded sky-shaft/neighbor calculation seeds both channels
from effective opacity/emission before its geometry is exposed. Other cells retain
prior illumination until asynchronous propagation handles increases and decreases;
calculated darkness remains valid. Direct-sky caches stay untouched by this seed so
the solver still detects changed shafts. Only numeric opacity/emission changes dirty
visual light.
Day/night scales sky brightness in frame uniforms without relighting.

Nearby admission, state reception, mesh selection and GPU upload use square-ring
priority. A progressing outer scan survives individual arrivals/edits; a bounded
near-player pass gives urgent resident work another opportunity each pump. Movement
reprioritizes queued jobs without cancellation. Periodic oldest-work turns preserve
background progress at admission, worker dispatch, reception and upload. Reordering
between columns never reorders a column's own state packets.

## Optional Pipeline Diagnostics

`MAAGE_WORLD_DIAGNOSTICS=1` enables bounded records under `maage.world.latency`,
`maage.server.latency` and `maage.client.latency`. Action IDs correlate prediction,
Server admission/acknowledgment and confirmed geometry. Column/revision IDs correlate
mutations, receipt, worker queue/execution/disposition and GPU upload wait/API duration.
World backlog summaries are limited to one per second. Records allocate no diagnostic
history. Input timing begins at the native input poll; Survival timing includes
intended mining duration. First successful frame submission is recorded separately.
Upload API duration excludes later asynchronous device execution; neither upload
nor frame submission is display scanout timing.
Local transport durations use one process monotonic clock; remote adapters must mark
untranslated timestamps unavailable and report peer-local durations separately.

Diagnostics distinguish block availability, light initialization/refinement, mesh
work, upload readiness and stale/cancelled dispositions. Runtime latency remains
unmeasured; submitted geometry timing does not establish display latency.

Camera obstruction samples come from the effective Client replica and refresh
only on eye-cell or geometry-epoch changes. Environmental water/lava/rain and cave
sound opportunities also sample that replica on a bounded local 20 Hz cadence;
paused presentation accumulates no catch-up work. Missing data is skipped, and cave
ambience requires calculated darkness. Gameplay impacts, pickups, damage and thunder
remain authoritative events. No cosmetic World scan runs on the Server owner.

## Block Prediction and Swing

Prediction is bounded Client presentation over expected/replacement numeric states
and owned meshes. Breaking and ordinary block placement change the effective replica
immediately. Placement uses the same read-only destination, replacement, support and
player-overlap checks as the Server; tables and unavailable/indeterminate actions stay
authoritative. INPUT still performs every Server action; preview markers only request
confirmation of its recorded result, never authorize mutations or consume inventory.

A deduplicated patch regenerates affected 3x3x3 cells and axial foliage dependencies
through the ordinary face generator. The patch and every filtered source replacement
are prepared before any draw pointer changes. Failed preparation retains the previous
complete presentation and retries; background GPU replacements also defer while that
correction remains uncommitted. Streamed meshes are filtered against the currently
published patch before replacing cached sources, including sources with no remaining
faces. Removed owner cells and their replacement geometry therefore share one publication.
Leaves, ice and fluid neighbors retain ordinary material/height rules.

Block availability gates prediction independently of full light convergence. The edited
cell's provisional light belongs to the same effective snapshot as immediate geometry.
Frame submission still precedes budgeted background uploads; the next prediction update
refreshes affected views before those cache entries are drawn, advancing mining once.
This frame's input and preview marker flush after local state resolution and before
GPU preparation, so its own Server result cannot retarget that click or wait for
resource uploads. No render-loop barrier, GPU synchronization or scheduler replacement
is introduced.

Overlapping edits compose in action-ID order. Accepted actions stop overriding authority
at their acknowledged column revision, preventing an old break from hiding a later
placement of the same type. Patches retire only after required authority and compatible
cached geometry arrive; neighbor occlusion invalidation also advances geometry epochs.
Rejection restores effective authoritative blocks/light and rebuilds the whole local
transaction. Target decoration uses the effective raycast rather than a separate
pending-break hide mask. Unknown terrain remains obstructing. Inventory, drops, sounds and persistent
world mutations are never predicted. The local session contract is version 15.

One Client timeline drives first- and third-person swings: six ticks long, eligible
restart after three. Presses respond immediately; held mining requests follow
20 TPS cadence, misses do not repeatedly punch air, and release lets the active
swing finish. Attack echoes do not restart it. Successful placement has its own
Server action observation. Mining rules remain authoritative; local progress drives
crack stages, which are
`floor(progress * 10) - 1` and clear locally on release/target change. See
[Visual Rendering](visual-fidelity.md) for hand projection and material behavior.

## Water Movement

The owner samples the contracted player body against numeric fluid surfaces,
independently of eye immersion. Water motion uses 0.02 horizontal acceleration,
0.8 drag on all axes, then 0.02 downward gravity per tick. Held jump adds 0.04
upward before movement; crouching reduces horizontal input and natural gravity
provides descent. Water currents add a normalized 0.014 impulse, including falling
water beside walls. Creative flight keeps its own movement. A horizontal collision
permits a 0.3 ledge impulse only when the projected raised body is dry and clear;
unknown terrain and ceilings obstruct it. Dry jump timing, friction and collision
remain on the ordinary movement path. These are source-reviewed rules, not evidence
of observed gameplay fidelity.

Water updates retain the deduplicated scheduled queue and five-tick delay. A bounded
breadth-first footprint ranks horizontal outlets by nearby downward paths (up to
five cells); tied directions spread together. Existing same-fluid cells are not
overwritten by incoming flow. Decay observes neighboring levels and water above;
two horizontal sources regenerate a source only over solid material or another
source, including when water is above. Unknown supply/routing defers changes and
retains one retry. Loading revisits active fluids and exposed boundaries on a
worker, preserving pending delays. Replaceable vegetation drops are reserved
before water replaces it. Water never displaces lava; water beside/above lava
forms obsidian for sources and cobblestone only at levels 1–4, while lava falling
into water forms stone. Comprehensive lava behavior remains outside this pass.

Survival breathing uses the classic eye threshold (nominal fluid height plus
1/9 block), independently of leg/body contact. Air starts at 300 owner ticks, decreases once per unpaused
tick underwater, and at −20 resets to zero and deals two damage through the
existing environmental damage path. Surfacing restores 300 immediately; Creative,
death and respawn reset breath. There are no enchantment/potion protections yet.
The checkpoint retains air and its damage phase; `MGWORLD7` rejects older worlds
without conversion. Protocol 15 copies air and eye immersion in ordinary snapshots.
The Survival HUD draws up to ten filled/popping bubbles above hunger from the latest
air value; it needs no per-bubble events. GUI scale and the accepted health/hunger
alignment are unchanged.

Entry splashes retain transition gating and weighted incoming speed. Swimming
cues use incoming tick velocity, including vertical motion while grounded, on
the existing travel cadence. No special exit sound is added. Flow ambience keeps
its bounded 20 Hz local trials, levels 1–7, 1/64 chance, volume 0.75–1 and pitch
0.5–1.5; unavailable columns are excluded. There is no general particle emitter
or billboard lifetime pool yet. Entry splash/bubble bursts, drowning bubbles,
suspended water motes and underside drips remain absent; the existing pickup and
weather decorations do not provide that subsystem.

## Neighbor Support

Immutable numeric survival callbacks serve placement and neighbor validation.
Expected-state mutations preflight checks for the affected supported cell and six
neighbors before changing blocks. The persistent heap deduplicates (kind,cell),
processes at most 128 scheduled updates per owner phase and retains excess work.
Cascades are iterative; unavailable support or stale gameplay light retries later.
Loading/generation schedules validation to catch edits missed while absent.

| Block | Survival Rule | Automatic Support-Loss Drop |
| --- | --- | --- |
| Dandelion, poppy, tall grass | Grass block or dirt below | Flower itself; tall grass has a 1/8 seed chance. |
| Dead bush | Sand or dirt below | None without shears. |
| Floor/wall torch | Metadata-selected full solid supporting face | Torch with normalized item metadata. |
| Cactus | Sand/cactus below, no solid-material horizontal neighbor | Cactus; stacked loss cascades. |
| Snow layer | Valid opaque solid ground, leaves or a full snow layer; excludes ice | None. |
| Brown/red mushroom | Opaque substrate and full sky/block light below 13 | Matching mushroom; no mycelium/podzol content exists. |
| Leaves | Natural/persistent flags and deferred random support evaluation | Existing leaf drops; no immediate removal. |

Ordinary grass blocks do not require plant support. Environmental loss can create
drops when Creative removes support; direct Creative mining still creates none.
Capacity is reserved before removal and drops become item entities. Repeated
notifications observe changed states and cannot drop the same block twice.
[Random ticks and leaf support](random-block-ticks.md) describe separate decay rules.

`MGWORLD7` metadata and `MGCHNK5` chunks are the sole current encodings. Chunks
persist fluid/support kinds and remaining delays. Incompatible development worlds
are rejected without conversion or automatic deletion.

## Manual Review

Static review covers the thirteen registered HUD/menu/container layouts above,
shared draw/hit bounds, font advances, texture origins, scaling and navigation.
It does not establish visual parity. Title/logo appearance, exact text rasterization,
texture alignment and every screen at different scales still need visual comparison.

Checks below are pending; repeat presentation work on Metal and Vulkan.

- Inspect every screen at multiple scales/window sizes: text, control bounds,
  hover/disabled states, inventory/crafting, carried counts/tooltips, Creative
  scrolling, world-list partial rows/double clicks, navigation and safe areas.
- Break/place cave torches, open/close daylight holes and rapidly break neighboring
  blocks across chunk seams. Check light removal, rejection rollback, retained
  geometry, latency and absence of black flashes or empty chunks.
- Punch empty-handed, mine with tools, hold/release halfway, switch targets,
  instant-break plants and break consecutive blocks; compare both perspectives.
- Remove each support type, inspect cascading quantities/metadata/chances, save
  and reopen near unavailable boundaries, and distinguish natural/placed leaves.

Pending water gameplay review (no scenarios have been run):

- Swimming: shallow/deep entry, partial immersion, horizontal motion and release,
  upward swimming, passive/crouched descent, surface balance, wall/ceiling/channel
  collision, ledge exit, opposed/unopposed currents, high falls into water, and
  ordinary walking/jumping/sprinting/flight after exit.
- Fluids: flat sources, nearby drops/obstacles, source removal/decay, terrain
  break/place/block/unblock, supported two-source regeneration, lava contact,
  boundary flow, and unload/reload of active streams.
- Breathing: full depletion, repeated two-damage intervals, immediate resurfacing
  recovery, reentry, drowning death/respawn and Creative immunity.
- Items: still/flowing water sinking, falling currents, bottom rest, air transitions,
  submerged pickup, normal pickup delay and expiry.
- Presentation: entry/exit fog/FOV/overlay, ten-bubble HUD at several GUI scales,
  still/flow animations, roofs/waterfalls/transparent neighbors, chunk seams and
  rapid adjacent edits on Metal, Vulkan and modern OpenGL where available.
- Audio: entry splash, swimming and exit, flowing ambience, pause/resume and
  repeated small surface contacts without excessive repeated sounds.
