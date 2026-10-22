# Agent 10 — Client presentation integration handoff

All code here is Client/main-thread presentation code. Branch baseline:
`514abaddc659546d213f52b8ff39f45142e72dd4`. Merge the canonical
Agent 6 `include/maage/session_protocol.h` and Agent 5 audio-enabled
`src/client/options.h` before compiling the Client.

## Exact API replacements

- Removed `maage_client_ui_set_gameplay(maage_client_ui *, const maage_item_registry *, maage_gameplay *, const maage_block_registry *)`.
- Added `maage_client_ui_set_content(maage_client_ui *, const maage_item_registry *, const maage_block_registry *)`. Both registries must be finalized and remain alive throughout UI use.
- Added `maage_client_ui_set_snapshot(maage_client_ui *, const maage_session_snapshot *)`. Copies the full numeric value; caller may immediately reuse its source buffer.
- Added `maage_client_ui_take_command(maage_client_ui *, maage_session_command *)`; SUCCESS consumes the oldest queued command, NOOP means empty.
- Added `maage_client_ui_retry_command(maage_client_ui *, const maage_session_command *)` for restoring the just-taken command at FIFO front if `maage_local_session_send` returns BUFFER_OVERFLOW. Break the drain loop after restoring it.
- Added `maage_client_ui_set_click_callback(maage_client_ui *, maage_client_ui_click_callback, void *)`; local presentation-only click intent (e.g. audio service plays numeric `MAAGE_SOUND_GUI_CLICK`), never a Server command.
- Replaced `maage_client_interaction_update(visual, renderer, atlas, world, gameplay, trace)` with `maage_client_interaction_update(visual, renderer, atlas, blocks, snapshot, trace)`.
- Replaced `maage_client_environment_update(view, renderer, environment_state, world, player, yaw, pitch, width, height, fov)` with `maage_client_environment_update(view, renderer, snapshot, width, height, fov)`.
- Replaced `maage_client_debug_collect(debug, now, world, record, stats, player, yaw, pitch, radius, paused, renderer, meshes, game)` with `maage_client_debug_collect(debug, now, snapshot, radius, paused, renderer, meshes)`.

## Authoritative command ownership

Inventory clicks enqueue `MAAGE_CMD_INVENTORY` with numeric slot and
secondary-click flag. Creative item selections enqueue
`MAAGE_CMD_CREATIVE_PICK` with numeric item ID. Options game-mode toggle
enqueues `MAAGE_CMD_GAME_MODE` with requested mode; Pause respawn
enqueues `MAAGE_CMD_RESPAWN`. None call World or gameplay mutation APIs.
The 64-entry FIFO never reallocates, never blocks, and notifies the user
with a canonical BUFFER_OVERFLOW status if full.

The UI leaves `command.sequence = 0`: Agent 11 must assign one globally
monotonic command sequence for both UI and input commands immediately before
sending over the local session. Transport failure must not lose commands:
restore the just-taken command via `maage_client_ui_retry_command`,
and defer the remaining drain to the next frame.

The Server snapshot owns HUD selected slot, health, hunger, inventory cursor,
game mode and target/break progress. Optimistic UI state is not written to
this copy. Snapshot version is validated on attachment.

## Presentation fallbacks and pending protocol improvements

Version-1 snapshots copy the player climate/sky observation but not
per-target light. Cracks remain fullbright rather than reading World
memory or abusing the player's observation. The HUD held item remains
stationary between item changes because v1 lacks a copied swing fraction.
Adding a target light value and swing fraction to a later protocol version
would restore those two visual details.

Interaction mesh caching also checks session/world epochs. Environment
rendering samples snapshot day/weather and the authoritative player
observation without querying World; F3 uses copied stats/player/target.

## Fullscreen and audio

Exactly three labels remain: Windowed, Borderless, Fullscreen.
Fullscreen maps to `MAAGE_WINDOW_NATIVE_FULLSCREEN`; Borderless maps to
`MAAGE_WINDOW_BORDERLESS_FULLSCREEN`. F11 toggles Windowed and the
remembered non-windowed choice. Mode requests remain pending until the
Window layer reports the requested observed mode with no transition;
failed/unconfirmed transitions restore the last confirmed UI setting and
produce a deduplicated TIMEOUT notice. Agent 12 owns actual native Space
entry/exit and notch-safe-area reporting.

Options presents Master, Blocks, Player, Ambient, Weather, Music, and UI
numeric sliders from 0–100 percent. Keyboard left/right changes by 5
points; the mouse can drag sliders. Save at established Client options
boundaries, never per adjustment. Agent 5 owns defaults and persistence;
Agent 11 applies audio bus changes to playback.
