# Crafting

MaaGE crafting runs on the authoritative Server owner through portable World
services. Client screens display copied state and submit numeric actions. No
recipe matching, inventory mutation, asset lookup or gameplay rules run in a GPU
backend. Runtime and visual verification remain programmer-owned.

## Source and ownership

- `include/maage/crafting.h` defines recipe registration, matching, temporary
  containers and numeric slot/action contracts.
- `src/world/recipes.c` owns copied definitions and alternatives in a growable
  registry. `recipes_builtin.c` registers the current catalog during content
  initialization; content finalizes and owns recipes alongside items/blocks.
- `src/world/crafting.c` owns container operations. `src/world/gameplay.c` owns the
  active container alongside the existing player inventory. There is one cursor
  and one authoritative owner for every input stack.
- `src/server/runtime.c` validates commands, services container invalidation
  even during Escape pause, and publishes copied container state. Inventory and
  tables release mouse capture without pausing simulation or background music.
- `src/client/ui/gameplay.c` shares inventory, slot, icon, count, tooltip and cursor
  drawing between player inventory and the registered Crafting Table screen.
  Existing GUI scaling, safe area, clipping and shared Metal/Vulkan batches apply.

## Recipe contract

A shaped recipe has a pattern up to 3x3. Matching searches every fitting offset
and optionally its horizontal mirror, requiring all other cells to be empty.
There is no implicit rotation. Shapeless matching assigns each ingredient to a
separate occupied cell with a bounded augmenting-path algorithm; duplicate and
overlapping alternative ingredients cannot reuse a cell. Ingredient quantities
specify the units consumed from that cell. Metadata is exact or explicitly
wildcarded; damaged tools do not match ordinary ingredients.

Registration copies numeric item alternatives and validates every input, output
and remainder against finalized items. There is no fixed recipe-count limit.
Precedence is shaped before shapeless, then more occupied ingredients, then
registration order. Matching uses bounded stack storage, no allocations, names,
filesystem work or mutable global state. Recipes are independent of grid instances.

The player container has four row-major inputs and one derived result. A table
has nine inputs and one derived result. Both use the same registry. Only grid
changes and output transactions recompute matches; drawing never evaluates recipes.

Taking a result rechecks the current recipe and revision. A bounded scratch
inventory/grid preflights the complete output and every remainder before committing
any ownership change. Left and right clicks both take one complete recipe output.
The result never accepts cursor items. Shift-click repeats the same recipe up to
64 times, stopping when inputs, output capacity, remainder capacity or the recipe
changes. This covers the existing maximum input stack size while bounding future
self-replenishing recipes. Each craft is atomic; earlier successful crafts remain
committed if a later craft cannot fit.

Remainders first enter inventory, then the originating input cell if possible.
A remainder that cannot fit stops that craft without consuming ingredients. The
current catalog needs no crafting container items; the registration contract
supports them without adding unrelated bucket, brewing or automation mechanics.

Ordinary input slots reuse the existing left/right pickup, split, place, merge
and swap operation. Shift-click inputs return to inventory; shift-click inventory
slots transfers between main inventory and hotbar. It does not auto-fill recipes.
Normal slot commands can be pipelined in FIFO order; result clicks wait for the
published revision. Pending mutations hide the old result until acknowledged.

## Crafting Table and lifecycle

The table is a registered solid, placeable block/item (`minecraft:crafting_table`),
with hardness 2.5, hand harvesting and axe mining speed. Original top, side, front
and oak-plank bottom textures resolve through the existing resource installation.
There is no table block entity or persistent table storage.

The existing numeric use intent performs an authoritative raycast and expected
state check before opening a table, including when the hand is empty. No command
accepts Client coordinates to open a table. Every slot transaction rechecks health,
container identity, loaded table presence and the reference eight-block container
range. Player inventory opens through its own command.

Escape/E wait for authoritative closure and restore normal input capture.
Invalidation, death, mode changes, respawn, disconnect, world switching and normal
shutdown also close the container. Inputs and cursor first return to inventory;
overflow becomes existing authoritative item entities. All overflow capacity is
reserved before transferring items. Allocation failure leaves unreturned inputs
owned and blocks further container edits; the owner retries closure.

The single current metadata encoding is `MGWORLD5`. Normal shutdown closes the
container before World flush and the atomic player/entity checkpoint. If closure
cannot allocate overflow entities, that same checkpoint retains only the still
unreturned input stacks. Loading returns those stacks before allowing another
container; it does not restore an open table or save a derived result. Inventory,
inputs and drops are never stored as duplicate owners. Older metadata is rejected:
previous development worlds must be recreated. No conversion or automatic deletion
is performed. Chunk encoding is unchanged.

## Session contract

Protocol 8 carries explicit container commands: open player inventory, close an identified container, and act on an
identified slot. Slot groups are inventory, crafting input and crafting result;
actions are primary, secondary and quick move. Group bounds, action values,
container identity, result revision and Server state are checked independently.
Commands contain no recipe IDs, result quantities or cursor stacks.

Snapshots carry the container identity, revision, inputs and result plus the last
processed command sequence. Rejected actions are acknowledged too, allowing the
Client to clear pending presentation without predicting gameplay. Table activation
continues through the established numeric use/raycast path.

## Initial catalog

There are 42 registered recipes; metadata alternatives share definitions where
appropriate.

| Inputs | Output |
| --- | --- |
| One oak, spruce or birch log | Four matching planks (three recipes) |
| Two vertically arranged planks, including mixed species | Four sticks |
| Four planks in a square | One Crafting Table |
| Three planks in a bowl pattern | Four bowls |
| Planks, cobblestone, iron, diamond or gold plus sticks | Pickaxe, shovel and axe for each tier (15 recipes) |
| Coal or charcoal over a stick | Four torches |
| Four sand in a square | One sandstone |
| Two diagonally arranged iron ingots | Shears |
| Brown mushroom, red mushroom and bowl, in any three cells | Mushroom stew |
| Nine gold, iron, diamond, lapis, emerald, redstone or coal | Matching storage block (seven recipes) |
| One of those storage blocks, in any cell | Nine matching resources (seven recipes) |
| Four snowballs in a square | One snow block; shovel harvesting returns four snowballs |
| Nine gold nuggets | One gold ingot |
| One gold ingot, in any cell | Nine gold nuggets |

The tree-to-wooden-tools-to-stone-tools chain uses existing harvesting, drops,
pickup, placement and durability. Iron/gold ingots and charcoal are registered
and visible in Creative, but Survival acquisition awaits smelting. Smelting is
outside this feature; ores are not converted through fake crafting recipes.
Diamond progression consequently still depends on supplied iron equipment.
Bowl/stew items use existing stack/presentation/persistence contracts; food-use
behavior remains part of the provisional food system. Hoes, swords, armor,
recipe books and container automation are outside the supported tool catalog.

## Reference and compilation

The supplied Forge unpacked directory contains Forge/FML sources; vanilla classes
were read from its adjacent local `forgeSrc-1.7.10-10.13.4.1614-1.7.10-sources.jar`.
References examined include `CraftingManager` registration/sorting/matching,
`ShapedRecipes.matches/checkMatch`, `ShapelessRecipes.matches`,
`InventoryCrafting` row/column and stack operations, `InventoryCraftResult`,
`SlotCrafting.onPickupFromSlot`, `ContainerPlayer` and `ContainerWorkbench`
construction/transfer/closure, `Container` shift-click retry behavior,
`BlockWorkbench.getIcon/onBlockActivated`, `GuiInventory`/`GuiCrafting` layout,
`ItemStack.splitStack`, and `RecipesTools`, `RecipesIngots`, `RecipesCrafting`
and `RecipesFood` definitions. Native code implements these behaviors through
MaaGE ownership and transaction contracts.

On macOS ARM64, Debug and Release Client/Server builds and the separate headless
Release Server configure, compile and link successfully. Client configurations
have Metal and Vulkan/MoltenVK enabled and reuse their unchanged renderer
implementations. The linker reports duplicate-library warnings. No applications,
tests, CTest, benchmarks, gameplay scripts or GitHub Actions were executed.
Windows/Linux compilation and both renderers' runtime behavior remain unverified.
Client is `0.1.0-alpha.47`; Server is `1.0.0-alpha.17`. See
[the gameplay parity pass](gameplay-parity.md) for the subsequent presentation,
container pause and session changes.

## Programmer verification checklist

These are expected outcomes, not completed runtime checks. Use newly created
worlds and repeat presentation checks with Metal and Vulkan.

- Mine and collect each available log species. In inventory, one log in any cell
  previews four matching planks; left/right result clicks consume one log and
  take the full four. Plank placement, breaking and tooltips retain species.
- Make four sticks from two vertical planks, then a table from a 2x2 square.
  Empty-space violations, horizontal sticks, unrelated extra items and insufficient
  ingredients produce no result. Valid smaller recipes work at every fitting offset.
- Place the table, open it empty-handed or holding an item, and craft wooden then
  stone pickaxes/shovels/axes. Axes accept the mirrored pattern. Equipping/mining
  uses existing harvest levels, speed and durability; 3-row recipes fail in 2x2.
- Move/split/merge/swap ingredients with both buttons. Craft torches with coal and
  supplied charcoal, and stew with its ingredients in different cells. Extra
  ingredients reject the shapeless match. Try resource-block unpacking in any cell.
- Repeat result clicks, then shift-click stocked grids. Output totals must equal
  consumed ingredients; batching stops at exhaustion, recipe change or capacity.
  Main-inventory/hotbar and grid-to-inventory quick moves preserve quantities.
- Fill inventory or hold a full/incompatible cursor stack. A result that cannot
  fit consumes nothing; no partial recipe output appears. Rapid grid edits must
  hide the old result, and stale clicks must not duplicate items.
- Close with E/Escape while inputs/cursor remain, including immediately after a
  slot action. Items return to inventory or become collectible overflow drops;
  reopening shows an empty grid and mouse capture resumes correctly.
- If a table is removed, unloaded or becomes out of range through available
  administrative/world actions, further crafts must stop and contents return/drop.
  Death, respawn and mode changes must also leave no orphaned inputs.
- Save/quit or switch worlds while crafting, then reload. Count inventory and
  overflow entities once; no table contents or free preview result may reappear.
- On both Metal and Vulkan, inspect table faces, icons, stack counts, durability,
  cursor, tooltips, hover, resized windows, GUI scales and macOS safe areas. Creative
  inventory and table crafting must retain the same shared presentation fidelity.
