#include "maage/server_runtime.h"
#include "maage/content.h"
#include "maage/gameplay.h"
#include "maage/local_session.h"
#include "maage/thread.h"
#include "maage/time.h"
#include "maage/world_store.h"
#include "maage/log.h"
#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* One authoritative timeline; no Client frame-time or graphics dependency. */
#define MAAGE_SERVER_STEP_SECONDS 0.05
#define MAAGE_SERVER_MAX_CATCHUP 4U
#define MAAGE_SERVER_COMMAND_BUDGET 64U
#define MAAGE_SERVER_MESH_BUDGET 16U
#define MAAGE_SERVER_WAIT_MS 25UL

struct maage_server_runtime {
    maage_server_config config;
    char *instance_root, *world_id, *world_name;
    maage_generation_policy policy;
    maage_mutex *mutex;
    maage_condition *wake;
    maage_thread *thread;
    int started, joined, stop, finished, paused;
    maage_status result;
    maage_status_trace trace;
    maage_content *owned_content;
    const maage_content *content;
    maage_world_store *store;
    maage_world *world;
    maage_world_record record;
    maage_gameplay game;
    maage_player player;
    int game_ready, player_ready;
    double yaw, pitch;
    maage_gameplay_intent sampled;
    int pending_jump, pending_attack, pending_use;
    unsigned long tick, publication_sequence, event_sequence;
    unsigned long last_command_sequence, overloads, dropped_sound_events;
    maage_status last_world_notice;
    maage_session_event pending_status;
    int has_pending_status;
    maage_world_stats stats;
    double last_stats_time;
};

static maage_status server_status(unsigned char code, unsigned char operation)
{
    return maage_status_make(code, MAAGE_CONTEXT_SERVER, operation, 0);
}

static char *server_copy(const char *value)
{
    char *copy;
    size_t length;
    if (!value) return NULL;
    length = strlen(value);
    if (length == (size_t)-1) return NULL;
    copy = (char *)malloc(length + 1);
    if (copy) memcpy(copy, value, length + 1);
    return copy;
}

static int server_finite(double value)
{
    return value == value && value >= -DBL_MAX && value <= DBL_MAX;
}

static void server_capture_failure(maage_server_runtime *runtime, maage_status result,
    const maage_status_trace *source)
{
    if (!maage_status_failed(result) || maage_status_failed(runtime->result)) return;
    runtime->result = result;
    if (source) runtime->trace = *source;
    else {
        maage_status_trace_init(&runtime->trace);
        MAAGE_STATUS_TRACE(&runtime->trace, result, MAAGE_CONTEXT_SERVER, MAAGE_OPERATION_UPDATE);
    }
}

static int server_stop_requested(maage_server_runtime *runtime)
{
    int stop;
    maage_mutex_lock(runtime->mutex);
    stop = runtime->stop;
    maage_mutex_unlock(runtime->mutex);
    return stop;
}

static void server_status_event(maage_server_runtime *runtime, maage_status result,
    const maage_status_trace *source)
{
    maage_session_event *event = &runtime->pending_status;
    if (!runtime->config.session || maage_status_code(result) == MAAGE_STATUS_SUCCESS ||
        maage_status_code(result) == MAAGE_STATUS_NOOP) return;
    /* A single bounded pending status retains its causal trace across
     * backpressure; never block the simulation on a slow presentation reader. */
    if (runtime->has_pending_status && maage_status_failed(event->status) &&
        !maage_status_failed(result)) return;
    memset(event, 0, sizeof(*event));
    event->protocol_version = MAAGE_SESSION_PROTOCOL_VERSION;
    event->kind = MAAGE_EVENT_STATUS;
    event->session_epoch = event->world_epoch = 1UL;
    event->tick = runtime->tick;
    event->sequence = ++runtime->event_sequence;
    event->status = result;
    if (source) { event->trace = *source; event->has_trace = 1; }
    runtime->has_pending_status = 1;
}

static void server_publish_status(maage_server_runtime *runtime)
{
    maage_status result;
    if (!runtime->config.session || !runtime->has_pending_status) return;
    result = maage_local_session_publish_event(runtime->config.session,
        &runtime->pending_status);
    if (!maage_status_failed(result)) runtime->has_pending_status = 0;
    /* BUFFER_OVERFLOW is backpressure, not a gameplay failure. */
    else if (maage_status_code(result) != MAAGE_STATUS_BUFFER_OVERFLOW &&
        maage_status_code(result) != MAAGE_STATUS_INVALID_STATE)
        server_capture_failure(runtime, result, NULL);
}

static void server_emit_sound(maage_server_runtime *runtime,
    const maage_sound_event *sound)
{
    maage_session_event event;
    maage_status result;
    if (!runtime->config.session || !sound) return;
    memset(&event, 0, sizeof(event));
    event.protocol_version = MAAGE_SESSION_PROTOCOL_VERSION;
    event.kind = MAAGE_EVENT_SOUND;
    event.session_epoch = event.world_epoch = 1UL;
    event.tick = runtime->tick;
    event.sequence = ++runtime->event_sequence;
    event.sound = *sound;
    result = maage_local_session_publish_event(runtime->config.session, &event);
    if (maage_status_code(result) == MAAGE_STATUS_BUFFER_OVERFLOW) {
        /* Audio is best effort; never block or replay an obsolete positional
         * effect when Client consumption is slower than the simulation. */
        ++runtime->dropped_sound_events;
    } else if (maage_status_failed(result) &&
        maage_status_code(result) != MAAGE_STATUS_INVALID_STATE)
        server_capture_failure(runtime, result, NULL);
}

static void server_inventory_sound(maage_server_runtime *runtime)
{
    maage_sound_event event;
    maage_status result;
    if (!runtime->config.session) return;
    result = maage_gameplay_build_sound(&runtime->game, &event,
        MAAGE_SOUND_INVENTORY_CLICK, MAAGE_MATERIAL_DEFAULT,
        runtime->player.position, 0.5, 1.0);
    if (!maage_status_failed(result)) server_emit_sound(runtime, &event);
}

static maage_status server_reject(maage_server_runtime *runtime,
    unsigned char code, unsigned char operation)
{
    maage_status result = server_status(code, operation);
    maage_status_trace trace;
    maage_status_trace_init(&trace);
    MAAGE_STATUS_TRACE(&trace, result, MAAGE_CONTEXT_SERVER, operation);
    server_status_event(runtime, result, &trace);
    return result;
}

static maage_status server_apply_command(maage_server_runtime *runtime,
    const maage_session_command *command)
{
    maage_status result;
    const maage_item_registry *items = maage_content_items(runtime->content);
    if (command->protocol_version != MAAGE_SESSION_PROTOCOL_VERSION)
        return server_reject(runtime, MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_VALIDATE);
    /* Reject missing, stale or replayed command identities. */
    if (!command->sequence || command->sequence <= runtime->last_command_sequence)
        return server_reject(runtime, MAAGE_STATUS_INVALID_STATE, MAAGE_OPERATION_VALIDATE);
    runtime->last_command_sequence = command->sequence;
    switch (command->kind) {
    case MAAGE_CMD_INPUT:
        if (!server_finite(command->intent.movement.forward) ||
            !server_finite(command->intent.movement.strafe) ||
            !server_finite(command->intent.movement.yaw) ||
            !server_finite(command->intent.pitch) ||
            fabs(command->intent.movement.forward) > 1.0 ||
            fabs(command->intent.movement.strafe) > 1.0 ||
            fabs(command->intent.movement.yaw) > 1000000000.0 ||
            fabs(command->intent.pitch) > 1.553343034 ||
            (command->intent.attack != 0 && command->intent.attack != 1) ||
            (command->intent.use != 0 && command->intent.use != 1) ||
            (command->intent.jump_pressed != 0 && command->intent.jump_pressed != 1) ||
            (command->intent.movement.jump != 0 && command->intent.movement.jump != 1) ||
            (command->intent.descend != 0 && command->intent.descend != 1))
            return server_reject(runtime, MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_VALIDATE);
        runtime->pending_jump |= command->intent.jump_pressed;
        runtime->pending_attack |= command->intent.attack;
        runtime->pending_use |= command->intent.use;
        runtime->sampled = command->intent;
        /* jump_pressed is an edge, unlike held movement.jump/attack/use.
         * Only the pending latch may survive to the next simulation step. */
        runtime->sampled.jump_pressed = 0;
        runtime->yaw = command->intent.movement.yaw;
        runtime->pitch = command->intent.pitch;
        return server_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_UPDATE);
    case MAAGE_CMD_HOTBAR:
        if (command->hotbar_slot >= MAAGE_HOTBAR_SLOTS)
            return server_reject(runtime, MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_SELECT);
        runtime->game.inventory.selected = command->hotbar_slot;
        return server_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_SELECT);
    case MAAGE_CMD_INVENTORY:
        if (command->inventory_slot >= MAAGE_INVENTORY_SLOTS ||
            (command->secondary_click != 0 && command->secondary_click != 1))
            return server_reject(runtime, MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_VALIDATE);
        result = maage_inventory_click(&runtime->game.inventory, items,
            command->inventory_slot, command->secondary_click);
        if (!maage_status_failed(result) &&
            maage_status_code(result) != MAAGE_STATUS_NOOP)
            server_inventory_sound(runtime);
        break;
    case MAAGE_CMD_CREATIVE_PICK:
        if (runtime->game.mode != MAAGE_GAME_MODE_CREATIVE)
            return server_reject(runtime, MAAGE_STATUS_ACCESS_DENIED, MAAGE_OPERATION_MUTATE);
        result = maage_inventory_creative(&runtime->game.inventory, items, command->item_id);
        if (!maage_status_failed(result) &&
            maage_status_code(result) != MAAGE_STATUS_NOOP)
            server_inventory_sound(runtime);
        break;
    case MAAGE_CMD_GAME_MODE:
        if (command->game_mode != MAAGE_GAME_MODE_SURVIVAL &&
            command->game_mode != MAAGE_GAME_MODE_CREATIVE)
            return server_reject(runtime, MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_SET);
        result = maage_gameplay_set_mode(&runtime->game, command->game_mode);
        break;
    case MAAGE_CMD_RESPAWN:
        result = maage_gameplay_respawn(&runtime->game, &runtime->player, runtime->world);
        runtime->pending_jump = runtime->pending_attack = runtime->pending_use = 0;
        break;
    case MAAGE_CMD_PAUSE:
        if (runtime->config.execution_mode != MAAGE_SERVER_INTEGRATED)
            return server_reject(runtime, MAAGE_STATUS_ACCESS_DENIED, MAAGE_OPERATION_SET);
        if (command->paused != 0 && command->paused != 1)
            return server_reject(runtime, MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_VALIDATE);
        runtime->paused = command->paused;
        /* Modal input is not allowed to reappear as an action on resume. */
        memset(&runtime->sampled, 0, sizeof(runtime->sampled));
        runtime->pending_jump = runtime->pending_attack = runtime->pending_use = 0;
        return server_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_SET);
    case MAAGE_CMD_VIEW:
        if (command->render_radius < 1 ||
            command->render_radius > MAAGE_WORLD_MAX_RADIUS ||
            command->leaves_mode > MAAGE_LEAVES_FANCY ||
            !server_finite(command->view_yaw) ||
            fabs(command->view_yaw) > 1000000000.0)
            return server_reject(runtime, MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_VALIDATE);
        /* World currently freezes the streaming radius at create; do not
         * claim a changed radius took effect until World exposes its setter. */
        if (command->render_radius != runtime->config.render_distance)
            return server_reject(runtime, MAAGE_STATUS_NOT_IMPLEMENTED, MAAGE_OPERATION_SET);
        result = maage_world_set_leaves_mode(runtime->world,
            (maage_leaves_mode)command->leaves_mode);
        if (!maage_status_failed(result)) runtime->yaw = command->view_yaw;
        break;
    case MAAGE_CMD_DISCONNECT:
        maage_server_runtime_request_stop(runtime);
        return server_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_STOP);
    default:
        return server_reject(runtime, MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_VALIDATE);
    }
    if (maage_status_failed(result)) {
        maage_status_trace trace;
        maage_status_trace_init(&trace);
        MAAGE_STATUS_TRACE(&trace, result, MAAGE_CONTEXT_SERVER, MAAGE_OPERATION_MUTATE);
        server_status_event(runtime, result, &trace);
    }
    return result;
}

/* Copy at most a bounded number of commands per service cycle. A fast input
 * producer cannot monopolize the Server owner or starve World completions. */
static maage_status server_take_commands(maage_server_runtime *runtime)
{
    maage_session_command command;
    maage_status result;
    unsigned int count;
    if (!runtime->config.session) return server_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_POLL);
    for (count = 0; count < MAAGE_SERVER_COMMAND_BUDGET; ++count) {
        result = maage_local_session_take_command(runtime->config.session, &command);
        if (maage_status_code(result) == MAAGE_STATUS_NOOP) break;
        if (maage_status_code(result) == MAAGE_STATUS_INVALID_STATE) {
            /* A closed Client mailbox is a disconnected session, not an
             * invitation to keep advancing orphaned integrated gameplay. */
            maage_server_runtime_request_stop(runtime);
            break;
        }
        if (maage_status_failed(result)) return result;
        (void)server_apply_command(runtime, &command); /* rejected action is not fatal */
        if (command.kind == MAAGE_CMD_DISCONNECT) break;
    }
    return server_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_POLL);
}

static void server_publish_snapshot(maage_server_runtime *runtime, double now)
{
    maage_session_snapshot snapshot;
    int cell_x, cell_y, cell_z;
    if (!runtime->config.session || !runtime->world || !runtime->game_ready) return;
    memset(&snapshot, 0, sizeof(snapshot));
    snapshot.protocol_version = MAAGE_SESSION_PROTOCOL_VERSION;
    snapshot.session_epoch = snapshot.world_epoch = 1UL;
    snapshot.tick = runtime->tick;
    snapshot.sequence = ++runtime->publication_sequence;
    snapshot.player.player = runtime->player;
    snapshot.player.yaw = runtime->yaw;
    snapshot.player.pitch = runtime->pitch;
    snapshot.player.inventory = runtime->game.inventory;
    snapshot.player.game_mode = runtime->game.mode;
    snapshot.player.health = runtime->game.health;
    snapshot.player.hunger = runtime->game.hunger;
    snapshot.player.saturation = runtime->game.saturation;
    snapshot.player.flying = runtime->game.flying;
    snapshot.player.has_target = runtime->game.has_target;
    snapshot.player.breaking_active = runtime->game.breaking_active;
    snapshot.player.target = runtime->game.target;
    snapshot.player.breaking = runtime->game.breaking;
    snapshot.player.break_progress = runtime->game.break_progress;
    snapshot.player.swing = runtime->game.swing;
    maage_world_environment(runtime->world, &snapshot.environment);
    cell_x = (int)floor(runtime->player.position[0]);
    cell_y = (int)floor(runtime->player.position[1] + MAAGE_PLAYER_EYE_HEIGHT);
    cell_z = (int)floor(runtime->player.position[2]);
    if (cell_y >= 0 && cell_y < MAAGE_WORLD_HEIGHT)
        snapshot.has_observation = maage_world_observe(runtime->world,
            cell_x, cell_y, cell_z, &snapshot.observation);
    if (runtime->last_stats_time == 0.0 ||
        now - runtime->last_stats_time >= 0.25) {
        maage_world_get_stats(runtime->world, &runtime->stats);
        runtime->last_stats_time = now;
    }
    snapshot.world_stats = runtime->stats;
    (void)maage_local_session_publish_snapshot(runtime->config.session, &snapshot);
}

static void server_offer_meshes(maage_server_runtime *runtime)
{
    unsigned int n;
    maage_world_mesh_update update;
    maage_local_mesh_update outgoing;
    maage_status result;
    if (!runtime->world) return;
    for (n = 0; n < MAAGE_SERVER_MESH_BUDGET; ++n) {
        memset(&update, 0, sizeof(update));
        result = maage_world_take_mesh(runtime->world, &update);
        if (maage_status_code(result) == MAAGE_STATUS_NOOP) return;
        if (maage_status_failed(result)) {
            server_capture_failure(runtime, result, maage_world_trace(runtime->world));
            return;
        }
        if (!maage_world_mesh_current(runtime->world, update.x, update.z,
                update.revision) || !runtime->config.session) {
            (void)maage_world_mesh_destroy(update.mesh);
            continue;
        }
        memset(&outgoing, 0, sizeof(outgoing));
        outgoing.x = update.x; outgoing.z = update.z;
        outgoing.revision = update.revision; outgoing.world_epoch = 1UL;
        outgoing.mesh = update.mesh;
        result = maage_local_session_offer_mesh(runtime->config.session, &outgoing);
        if (outgoing.mesh) (void)maage_world_mesh_destroy(outgoing.mesh);
        /* Transport explicitly retains ownership on a full queue. Dropping
         * the detached obsolete CPU mesh never blocks simulation. */
        if (maage_status_failed(result) &&
            maage_status_code(result) != MAAGE_STATUS_BUFFER_OVERFLOW &&
            maage_status_code(result) != MAAGE_STATUS_INVALID_STATE)
            server_capture_failure(runtime, result, NULL);
    }
}

static maage_status server_open_world(maage_server_runtime *runtime,
    maage_status_trace *trace)
{
    maage_world_config world_config;
    const maage_item_registry *items;
    maage_status result;
    char error[256];
    if (!runtime->config.content) {
        result = maage_content_create(NULL, &runtime->owned_content, trace);
        if (maage_status_failed(result)) return result;
    }
    runtime->content = runtime->config.content ?
        runtime->config.content : runtime->owned_content;
    if (!maage_block_registry_is_finalized(maage_content_blocks(runtime->content)) ||
        !maage_item_registry_is_finalized(maage_content_items(runtime->content)))
        return MAAGE_STATUS_TRACE(trace, server_status(MAAGE_STATUS_INVALID_STATE,
            MAAGE_OPERATION_INITIALIZE), MAAGE_CONTEXT_SERVER, MAAGE_OPERATION_INITIALIZE);
    items = maage_content_items(runtime->content);
    memset(&runtime->record, 0, sizeof(runtime->record));
    error[0] = 0;
    result = maage_world_store_open(runtime->config.instance_root,
        runtime->config.world_id, runtime->config.create_world,
        runtime->config.world_name,
        runtime->config.has_seed ? &runtime->config.seed : NULL,
        runtime->config.generation_policy,
        items, &runtime->store, &runtime->record, error, trace);
    if (maage_status_failed(result)) return MAAGE_STATUS_PROPAGATE(trace,
        result, MAAGE_CONTEXT_SERVER, MAAGE_OPERATION_OPEN);
    memset(&world_config, 0, sizeof(world_config));
    world_config.registry = maage_content_blocks(runtime->content);
    world_config.biomes = maage_content_biomes(runtime->content);
    world_config.seed = runtime->record.seed;
    world_config.store = runtime->store;
    world_config.worker_count = runtime->config.world_workers;
    world_config.render_distance = runtime->config.render_distance;
    world_config.new_world = runtime->record.new_world;
    memcpy(world_config.position, runtime->record.position,
        sizeof(world_config.position));
    world_config.generation_policy = runtime->record.generation_policy;
    world_config.initial_environment = &runtime->record.environment;
    result = maage_world_create(&world_config, &runtime->world, trace);
    if (maage_status_failed(result)) return MAAGE_STATUS_PROPAGATE(trace,
        result, MAAGE_CONTEXT_SERVER, MAAGE_OPERATION_CREATE);
    result = maage_player_spawn(&runtime->player, runtime->world);
    if (maage_status_failed(result)) return MAAGE_STATUS_PROPAGATE(trace,
        result, MAAGE_CONTEXT_SERVER, MAAGE_OPERATION_INITIALIZE);
    runtime->player_ready = 1;
    result = maage_gameplay_init(&runtime->game, items);
    if (maage_status_failed(result)) return result;
    runtime->game_ready = 1;
    if (!runtime->record.new_world) {
        result = maage_gameplay_restore(&runtime->game, items,
            &runtime->record.gameplay);
        if (maage_status_failed(result)) return MAAGE_STATUS_PROPAGATE(trace,
            result, MAAGE_CONTEXT_SERVER, MAAGE_OPERATION_LOAD);
    }
    if (runtime->config.initial_game_mode >= 0) {
        result = maage_gameplay_set_mode(&runtime->game,
            (unsigned int)runtime->config.initial_game_mode);
        if (maage_status_failed(result)) return result;
        if (runtime->record.new_world &&
            runtime->config.initial_game_mode == MAAGE_GAME_MODE_SURVIVAL)
            memset(&runtime->game.inventory, 0, sizeof(runtime->game.inventory));
    }
    runtime->game.reach = runtime->config.interaction_range;
    runtime->yaw = runtime->record.yaw;
    runtime->pitch = runtime->record.pitch;
    runtime->sampled.movement.yaw = runtime->yaw;
    runtime->sampled.pitch = runtime->pitch;
    return server_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_INITIALIZE);
}

static void server_close_world(maage_server_runtime *runtime,
    maage_status_trace *trace)
{
    maage_status result;
    maage_environment_state environment;
    maage_status_trace cleanup_trace;
    char error[256];
    if (runtime->world) {
        maage_status_trace_init(&cleanup_trace);
        result = maage_world_flush(runtime->world, &cleanup_trace);
        if (maage_status_failed(result)) {
            server_capture_failure(runtime, result, &cleanup_trace);
            server_status_event(runtime, result, &cleanup_trace);
            runtime->player_ready = 0; /* preserve previous metadata */
        }
    }
    if (runtime->store && runtime->world && runtime->player_ready &&
        runtime->game_ready) {
        maage_world_environment(runtime->world, &environment);
        maage_status_trace_init(&cleanup_trace);
        error[0] = 0;
        result = maage_world_store_save(runtime->store,
            runtime->player.position, runtime->yaw, runtime->pitch,
            &runtime->game, &environment, error, &cleanup_trace);
        if (maage_status_failed(result)) {
            server_capture_failure(runtime, result, &cleanup_trace);
            server_status_event(runtime, result, &cleanup_trace);
        }
    }
    if (runtime->world) {
        result = maage_world_destroy(runtime->world);
        if (maage_status_failed(result)) {
            /* Workers may still borrow registries and the store. Retain all
             * borrowed objects and report the fatal lifecycle failure. */
            server_capture_failure(runtime, result, NULL);
            server_status_event(runtime, result, &runtime->trace);
            return;
        }
        runtime->world = NULL;
    }
    if (runtime->game_ready) {
        result = maage_gameplay_destroy(&runtime->game);
        server_capture_failure(runtime, result, NULL);
        runtime->game_ready = 0;
    }
    if (runtime->store) {
        result = maage_world_store_close(runtime->store);
        server_capture_failure(runtime, result, NULL);
        runtime->store = NULL;
    }
    if (runtime->owned_content) {
        result = maage_content_destroy(runtime->owned_content);
        server_capture_failure(runtime, result, NULL);
        runtime->owned_content = NULL;
    }
    runtime->content = NULL;
    (void)trace;
}

static maage_status server_tick(maage_server_runtime *runtime,
    maage_status_trace *trace)
{
    maage_gameplay_intent intent = runtime->sampled;
    maage_sound_event sounds[32];
    size_t sound_count = 0, i;
    maage_status result;
    /* A pulse sampled between steps survives latest-state replacement, but
     * every pulse is consumed once. Held attack/use continue per tick. */
    intent.jump_pressed |= runtime->pending_jump;
    intent.movement.jump |= runtime->pending_jump;
    intent.attack |= runtime->pending_attack;
    intent.use |= runtime->pending_use;
    intent.movement.yaw = runtime->yaw;
    intent.pitch = runtime->pitch;
    runtime->pending_jump = runtime->pending_attack = runtime->pending_use = 0;
    result = maage_gameplay_tick(&runtime->game, &runtime->player,
        runtime->world, maage_content_items(runtime->content), &intent,
        sounds, sizeof(sounds) / sizeof(sounds[0]), &sound_count, trace);
    /* Effects describe outcomes already resolved by authoritative gameplay.
     * Transport saturation may omit a sound, never roll back block mutations. */
    for (i = 0; i < sound_count; ++i) {
        sounds[i].tick = runtime->tick + 1UL;
        server_emit_sound(runtime, &sounds[i]);
    }
    if (maage_status_failed(result)) return result;
    if (maage_status_is_warning(result)) {
        server_status_event(runtime, result, trace);
        maage_status_trace_init(trace);
    }
    result = maage_world_tick(runtime->world);
    if (maage_status_failed(result)) return MAAGE_STATUS_PROPAGATE(trace, result,
        MAAGE_CONTEXT_SERVER, MAAGE_OPERATION_TICK);
    ++runtime->tick;
    return server_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_TICK);
}

static void server_thread_entry(void *context)
{
    maage_server_runtime *runtime = (maage_server_runtime *)context;
    maage_status result, waited;
    maage_status_trace trace;
    double now, deadline, remaining, last_published;
    unsigned int caught_up;
    unsigned long sleep_ms, last_published_tick = 0UL;
    int center_x, center_z, spawn_ready;
    maage_status_trace_init(&trace);
    result = server_open_world(runtime, &trace);
    if (maage_status_failed(result)) {
        server_capture_failure(runtime, result, &trace);
        goto shutdown;
    }
    now = maage_time_monotonic_seconds();
    if (now < 0.0) {
        server_capture_failure(runtime, server_status(MAAGE_STATUS_ERROR,
            MAAGE_OPERATION_INITIALIZE), NULL);
        goto shutdown;
    }
    deadline = now + MAAGE_SERVER_STEP_SECONDS;
    last_published = -1.0;
    while (!server_stop_requested(runtime)) {
        /* A stream pump runs even when paused or awaiting the initial spawn
         * column; job completions precede all collision queries each step. */
        maage_status_trace_init(&trace);
        result = server_take_commands(runtime);
        if (maage_status_failed(result)) {
            server_capture_failure(runtime, result, NULL);
            break;
        }
        result = maage_world_stream(runtime->world, runtime->player.position[0],
            runtime->player.position[2], runtime->yaw, &trace);
        if (maage_status_failed(result)) {
            server_capture_failure(runtime, result, &trace);
            break;
        }
        if (maage_world_failed(runtime->world)) {
            server_capture_failure(runtime, maage_world_status(runtime->world),
                maage_world_trace(runtime->world));
            break;
        }
        if (maage_world_status(runtime->world) != runtime->last_world_notice) {
            runtime->last_world_notice = maage_world_status(runtime->world);
            server_status_event(runtime, runtime->last_world_notice,
                maage_world_trace(runtime->world));
        }
        server_offer_meshes(runtime);
        server_publish_status(runtime);
        if (maage_status_failed(runtime->result)) break;
        now = maage_time_monotonic_seconds();
        if (now < 0.0) {
            server_capture_failure(runtime, server_status(MAAGE_STATUS_ERROR,
                MAAGE_OPERATION_QUERY), NULL);
            break;
        }
        center_x = maage_world_chunk_coordinate((int)floor(runtime->player.position[0]));
        center_z = maage_world_chunk_coordinate((int)floor(runtime->player.position[2]));
        spawn_ready = maage_world_chunk_loaded(runtime->world, center_x, center_z);
        if (runtime->paused || !spawn_ready) {
            /* Do not run delayed gameplay when resuming from pause/loading. */
            deadline = now + MAAGE_SERVER_STEP_SECONDS;
        } else {
            caught_up = 0;
            while (now >= deadline && caught_up < MAAGE_SERVER_MAX_CATCHUP &&
                !runtime->paused && !server_stop_requested(runtime)) {
                /* Sample bounded commands for every ordered step, not merely
                 * once per render-independent scheduler wake. */
                result = server_take_commands(runtime);
                if (maage_status_failed(result)) {
                    server_capture_failure(runtime, result, NULL);
                    break;
                }
                if (runtime->paused || server_stop_requested(runtime)) {
                    deadline = now + MAAGE_SERVER_STEP_SECONDS;
                    break;
                }
                maage_status_trace_init(&trace);
                result = server_tick(runtime, &trace);
                if (maage_status_failed(result)) {
                    server_capture_failure(runtime, result, &trace);
                    break;
                }
                ++caught_up;
                deadline += MAAGE_SERVER_STEP_SECONDS;
                now = maage_time_monotonic_seconds();
                if (now < 0.0) {
                    server_capture_failure(runtime, server_status(MAAGE_STATUS_ERROR,
                        MAAGE_OPERATION_QUERY), NULL);
                    break;
                }
            }
            if (maage_status_failed(runtime->result)) break;
            if (now >= deadline && caught_up == MAAGE_SERVER_MAX_CATCHUP) {
                ++runtime->overloads;
                /* Preserve tick order; skip backlog wall-time, never fabricate
                 * thousands of gameplay ticks or fast-forward World time. */
                deadline = now + MAAGE_SERVER_STEP_SECONDS;
                server_status_event(runtime, server_status(MAAGE_STATUS_WARNING,
                    MAAGE_OPERATION_TICK), NULL);
                if (runtime->overloads == 1UL || runtime->overloads % 100UL == 0UL)
                    maage_log_warning("maage.server.tick",
                        "20 TPS scheduler overloaded (%lu bounded catch-ups); simulation time was not accelerated.",
                        runtime->overloads);
            }
        }
        if (last_published < 0.0 || now - last_published >= 0.1 ||
            runtime->tick != last_published_tick) {
            server_publish_snapshot(runtime, now);
            last_published = now;
            last_published_tick = runtime->tick;
        }
        server_publish_status(runtime);
        remaining = deadline - now;
        if (remaining < 0.001) remaining = 0.001;
        sleep_ms = (unsigned long)(remaining * 1000.0);
        if (!sleep_ms) sleep_ms = 1UL;
        if (sleep_ms > MAAGE_SERVER_WAIT_MS) sleep_ms = MAAGE_SERVER_WAIT_MS;
        maage_mutex_lock(runtime->mutex);
        if (!runtime->stop) {
            waited = maage_condition_timed_wait(runtime->wake, runtime->mutex, sleep_ms);
            if (maage_status_failed(waited) &&
                maage_status_code(waited) != MAAGE_STATUS_TIMEOUT)
                server_capture_failure(runtime, waited, NULL);
        }
        maage_mutex_unlock(runtime->mutex);
        if (maage_status_failed(runtime->result)) break;
    }
shutdown:
    server_close_world(runtime, &trace);
    server_publish_status(runtime);
    if (runtime->config.session) {
        maage_session_event disconnected;
        memset(&disconnected, 0, sizeof(disconnected));
        disconnected.protocol_version = MAAGE_SESSION_PROTOCOL_VERSION;
        disconnected.kind = MAAGE_EVENT_DISCONNECTED;
        disconnected.session_epoch = disconnected.world_epoch = 1UL;
        disconnected.tick = runtime->tick;
        disconnected.sequence = ++runtime->event_sequence;
        disconnected.status = runtime->result;
        disconnected.has_trace = maage_status_failed(runtime->result);
        if (disconnected.has_trace) disconnected.trace = runtime->trace;
        (void)maage_local_session_publish_event(runtime->config.session, &disconnected);
        /* Terminal status remains drainable; new commands and publications
         * are now rejected by the transport's native shutdown barrier. */
        (void)maage_local_session_close(runtime->config.session);
    }
    maage_mutex_lock(runtime->mutex);
    runtime->finished = 1;
    maage_condition_broadcast(runtime->wake);
    maage_mutex_unlock(runtime->mutex);
}

maage_status maage_server_runtime_create(const maage_server_config *config,
    maage_server_runtime **output, maage_status_trace *trace)
{
    maage_server_runtime *runtime;
    maage_status result;
    if (output) *output = NULL;
    if (!config || !output || !config->instance_root ||
        !*config->instance_root || config->render_distance < 1 ||
        config->render_distance > MAAGE_WORLD_MAX_RADIUS ||
        (config->initial_game_mode != -1 &&
         config->initial_game_mode != MAAGE_GAME_MODE_SURVIVAL &&
         config->initial_game_mode != MAAGE_GAME_MODE_CREATIVE) ||
        (config->execution_mode != MAAGE_SERVER_INTEGRATED &&
         config->execution_mode != MAAGE_SERVER_DEDICATED) ||
        (config->interaction_range != 0.0 &&
         (!server_finite(config->interaction_range) ||
          config->interaction_range <= 0.0 || config->interaction_range > 64.0)) ||
        (config->execution_mode == MAAGE_SERVER_INTEGRATED && !config->session) ||
        (config->generation_policy &&
         !maage_generation_policy_valid(config->generation_policy)))
        return MAAGE_STATUS_TRACE(trace, server_status(MAAGE_STATUS_INVALID_ARGUMENT,
            MAAGE_OPERATION_CREATE), MAAGE_CONTEXT_SERVER, MAAGE_OPERATION_CREATE);
    runtime = (maage_server_runtime *)calloc(1, sizeof(*runtime));
    if (!runtime) return server_status(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_CREATE);
    runtime->config = *config;
    runtime->instance_root = server_copy(config->instance_root);
    runtime->world_id = server_copy(config->world_id);
    runtime->world_name = server_copy(config->world_name);
    if (!runtime->instance_root ||
        (config->world_id && !runtime->world_id) ||
        (config->world_name && !runtime->world_name)) {
        result = server_status(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_CREATE);
        goto failed;
    }
    runtime->config.instance_root = runtime->instance_root;
    runtime->config.world_id = runtime->world_id;
    runtime->config.world_name = runtime->world_name;
    if (config->generation_policy) {
        runtime->policy = *config->generation_policy;
        runtime->config.generation_policy = &runtime->policy;
    }
    runtime->result = server_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_INITIALIZE);
    maage_status_trace_init(&runtime->trace);
    result = maage_mutex_create(&runtime->mutex);
    if (maage_status_failed(result)) goto failed;
    result = maage_condition_create(&runtime->wake);
    if (maage_status_failed(result)) goto failed;
    *output = runtime;
    return server_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_CREATE);
failed:
    (void)maage_condition_destroy(runtime->wake);
    (void)maage_mutex_destroy(runtime->mutex);
    free(runtime->world_name);
    free(runtime->world_id);
    free(runtime->instance_root);
    free(runtime);
    return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_SERVER, MAAGE_OPERATION_CREATE);
}

maage_status maage_server_runtime_start(maage_server_runtime *runtime,
    maage_status_trace *trace)
{
    maage_status result;
    if (!runtime) return server_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_START);
    maage_mutex_lock(runtime->mutex);
    if (runtime->started || runtime->stop) {
        maage_mutex_unlock(runtime->mutex);
        return server_status(MAAGE_STATUS_INVALID_STATE, MAAGE_OPERATION_START);
    }
    runtime->started = 1;
    maage_mutex_unlock(runtime->mutex);
    result = maage_thread_start(server_thread_entry, runtime, &runtime->thread);
    if (maage_status_failed(result)) {
        maage_mutex_lock(runtime->mutex);
        runtime->started = 0;
        maage_mutex_unlock(runtime->mutex);
        return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_SERVER, MAAGE_OPERATION_START);
    }
    return server_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_START);
}

void maage_server_runtime_request_stop(maage_server_runtime *runtime)
{
    if (!runtime || !runtime->mutex) return;
    maage_mutex_lock(runtime->mutex);
    runtime->stop = 1;
    maage_condition_broadcast(runtime->wake);
    maage_mutex_unlock(runtime->mutex);
}

int maage_server_runtime_finished(maage_server_runtime *runtime)
{
    int finished;
    if (!runtime) return 1;
    maage_mutex_lock(runtime->mutex);
    finished = runtime->finished;
    maage_mutex_unlock(runtime->mutex);
    return finished;
}

maage_status maage_server_runtime_join(maage_server_runtime *runtime,
    maage_status_trace *trace)
{
    maage_status result;
    if (!runtime) return server_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_WAIT);
    if (!runtime->started) return server_status(MAAGE_STATUS_INVALID_STATE, MAAGE_OPERATION_WAIT);
    if (!runtime->joined) {
        result = maage_thread_join(runtime->thread);
        if (maage_status_failed(result))
            return MAAGE_STATUS_PROPAGATE(trace, result,
                MAAGE_CONTEXT_SERVER, MAAGE_OPERATION_WAIT);
        runtime->joined = 1;
        runtime->thread = NULL;
    }
    if (trace) *trace = runtime->trace;
    return runtime->result;
}

maage_status maage_server_runtime_destroy(maage_server_runtime *runtime)
{
    maage_status result;
    if (!runtime) return server_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_DESTROY);
    if (runtime->started && !runtime->joined) {
        maage_server_runtime_request_stop(runtime);
        result = maage_server_runtime_join(runtime, NULL);
        if (!runtime->joined) return result;
    }
    if (runtime->world) return server_status(MAAGE_STATUS_INVALID_STATE,
        MAAGE_OPERATION_DESTROY); /* borrowed workers/resources intentionally retained */
    result = maage_condition_destroy(runtime->wake);
    if (maage_status_failed(result)) return result;
    result = maage_mutex_destroy(runtime->mutex);
    if (maage_status_failed(result)) return result;
    free(runtime->world_name);
    free(runtime->world_id);
    free(runtime->instance_root);
    free(runtime);
    return server_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_DESTROY);
}
