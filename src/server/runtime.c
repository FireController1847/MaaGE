#include "creative.h"
#include "../application/content_modules.h"
#include "maage/server_runtime.h"
#include "maage/content.h"
#include "maage/containers.h"
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
#define MAAGE_SERVER_COLUMN_BUDGET 16U
#define MAAGE_SERVER_WAIT_MS 25UL

struct maage_server_runtime {
    maage_server_config config;
    char *instance_root, *world_id, *world_name;
    maage_generation_policy policy;
    maage_entity_policy entity_policy;
    maage_mutex *mutex;
    maage_condition *wake;
    maage_thread *thread;
    int started, joined, stop, finished, ready, paused;
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
    maage_session_command deferred_command;
    int has_deferred_command;
    int pending_jump, pending_attack, pending_use, pending_forward;
    unsigned long tick, publication_sequence, event_sequence, entity_sequence;
    unsigned long gameplay_command_sequence, last_break_tick;
    double next_tick_at, input_received_at, deferred_received_at;
    unsigned long input_sequence, consumed_input_sequence;
    maage_status entity_publication_failure;
    unsigned long last_command_sequence, overloads, dropped_sound_events;
    unsigned long effect_random;
    maage_status last_world_notice, crafting_failure;
    int publication_dirty;
    int sneaking, diagnostics;
    unsigned long last_prediction;
    struct {
        int active, resolved, placing;
        maage_block_state_id replacement;
        unsigned long base_revision, admitted_tick;
        maage_world_target target;
        maage_session_event event;
    } predictions[32];
    /* Bounded completed actions bridge previews delayed across owner steps.
     * A record is consumed once and must be newer than the replica baseline. */
    struct {
        int available, placing;
        maage_world_target target;
        maage_block_state_id replacement;
        unsigned long tick, command_sequence, revisions[9];
        double completed_at;
    } actions[32];
    unsigned int action_cursor;
    maage_session_event pending_status;
    int has_pending_status;
    maage_world_stats stats;
    double last_stats_time;
};

static maage_status server_status(unsigned char code, unsigned char operation) {
    return maage_status_make(code, MAAGE_CONTEXT_SERVER, operation, 0);
}

static char *server_copy(const char *value) {
    char *copy;
    size_t length;
    if (!value) return NULL;
    length = strlen(value);
    if (length == (size_t) - 1) return NULL;
    copy = (char *) malloc(length + 1);
    if (copy) memcpy(copy, value, length + 1);
    return copy;
}

static int server_finite(double value) {
    return value == value && value >= -DBL_MAX && value <= DBL_MAX;
}

static void server_capture_failure(maage_server_runtime *runtime, maage_status result, const maage_status_trace *source) {
    if (!maage_status_failed(result) || maage_status_failed(runtime->result)) return;
    runtime->result = result;
    if (source) runtime->trace = *source;
    else {
        maage_status_trace_init(&runtime->trace);
        MAAGE_STATUS_TRACE(&runtime->trace, result, MAAGE_CONTEXT_SERVER, MAAGE_OPERATION_UPDATE);
    }
}

static int server_stop_requested(maage_server_runtime *runtime) {
    int stop;
    maage_mutex_lock(runtime->mutex);
    stop = runtime->stop;
    maage_mutex_unlock(runtime->mutex);
    return stop;
}

static void server_status_event(maage_server_runtime *runtime, maage_status result, const maage_status_trace *source) {
    maage_session_event *event = &runtime->pending_status;
    if (!runtime->config.session || maage_status_code(result) == MAAGE_STATUS_SUCCESS || maage_status_code(result) == MAAGE_STATUS_NOOP) return;
    /* A single bounded pending status retains its causal trace across
     * backpressure; never block the simulation on a slow presentation reader. */
    if (runtime->has_pending_status && maage_status_failed(event->status) && !maage_status_failed(result)) return;
    memset(event, 0, sizeof(*event));
    event->protocol_version = MAAGE_SESSION_PROTOCOL_VERSION;
    event->kind = MAAGE_EVENT_STATUS;
    event->session_epoch = event->world_epoch = 1UL;
    event->tick = runtime->tick;
    event->sequence = ++runtime->event_sequence;
    event->status = result;
    if (source) {
        event->trace = *source;
        event->has_trace = 1;
    }
    runtime->has_pending_status = 1;
}

static void server_publish_status(maage_server_runtime *runtime) {
    maage_status result;
    if (!runtime->config.session || !runtime->has_pending_status) return;
    result = maage_local_session_publish_event(runtime->config.session, &runtime->pending_status);
    if (!maage_status_failed(result)) runtime->has_pending_status = 0;
    /* BUFFER_OVERFLOW is backpressure, not a gameplay failure. */
    else if (maage_status_code(result) != MAAGE_STATUS_BUFFER_OVERFLOW && maage_status_code(result) != MAAGE_STATUS_INVALID_STATE) server_capture_failure(runtime, result, NULL);
}

static void server_emit_effect(maage_server_runtime *runtime, const maage_sound_event *sound, const maage_entity_pickup *pickups, size_t pickup_count) {
    maage_session_event event;
    maage_status result;
    if (!runtime->config.session || !sound) return;
    memset(&event, 0, sizeof(event));
    event.protocol_version = MAAGE_SESSION_PROTOCOL_VERSION;
    event.kind = pickup_count ? MAAGE_EVENT_PICKUP : MAAGE_EVENT_SOUND;
    event.pickup_count = (unsigned int) pickup_count;
    if (pickup_count) memcpy(event.pickups, pickups, pickup_count * sizeof(*pickups));
    event.session_epoch = event.world_epoch = 1UL;
    event.tick = sound->tick;
    event.sequence = ++runtime->event_sequence;
    event.sound = *sound;
    event.sound.sequence = event.sequence;
    event.emitted_at = maage_time_monotonic_seconds();
    result = maage_local_session_publish_event(runtime->config.session, &event);
    if (maage_status_code(result) == MAAGE_STATUS_BUFFER_OVERFLOW) {
        /* Audio is best effort; never block or replay an obsolete positional
         * effect when Client consumption is slower than the simulation. */
        ++runtime->dropped_sound_events;
    } else if (maage_status_failed(result) && maage_status_code(result) != MAAGE_STATUS_INVALID_STATE) server_capture_failure(runtime, result, NULL);
}

static void server_emit_sound(maage_server_runtime *runtime, const maage_sound_event *sound) {
    server_emit_effect(runtime, sound, NULL, 0);
}
static void server_entity_sound(void *context, unsigned long sound_id, const double position[3], double intensity, double pitch) {
    maage_server_runtime *runtime = (maage_server_runtime *) context;
    maage_sound_event sound;
    memset(&sound, 0, sizeof(sound));
    sound.event_id = sound_id;
    sound.tick = runtime->tick + 1;
    sound.intensity = intensity;
    sound.pitch = pitch;
    memcpy(sound.position, position, sizeof(sound.position));
    server_emit_sound(runtime, &sound);
}
static void server_player_motion(const maage_server_runtime *runtime, maage_session_player_motion *motion) {
    memcpy(motion->velocity, runtime->player.velocity, sizeof(motion->velocity));
    motion->walk_distance = runtime->game.walk_distance;
    motion->life = runtime->game.life;
    motion->grounded = runtime->player.grounded;
    motion->alive = runtime->game.health != 0;
    motion->sneaking = runtime->sneaking;
}
static maage_status server_emit_motion(maage_server_runtime *runtime, maage_status_trace *trace) {
    maage_session_event event;
    maage_status result;
    if (!runtime->config.session) return server_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_SNAPSHOT);
    memset(&event, 0, sizeof(event));
    event.protocol_version = MAAGE_SESSION_PROTOCOL_VERSION;
    event.kind = MAAGE_EVENT_PLAYER_MOTION;
    event.session_epoch = event.world_epoch = 1;
    event.tick = runtime->tick;
    event.sequence = ++runtime->event_sequence;
    server_player_motion(runtime, &event.motion);
    result = maage_local_session_publish_event(runtime->config.session, &event);
    /* Presentation can rebase from a snapshot after a dropped observation;
     * transport pressure never blocks or rolls back authoritative movement. */
    if (maage_status_code(result) == MAAGE_STATUS_BUFFER_OVERFLOW) {
        MAAGE_STATUS_TRACE(trace, result, MAAGE_CONTEXT_SERVER, MAAGE_OPERATION_SUBMIT);
        result = server_status(MAAGE_STATUS_PARTIAL, MAAGE_OPERATION_SNAPSHOT);
        maage_status_trace_recover(trace, result, MAAGE_CONTEXT_SERVER, MAAGE_OPERATION_SNAPSHOT, __FILE__, __LINE__);
    } else if (maage_status_code(result) == MAAGE_STATUS_INVALID_STATE) return server_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_SNAPSHOT);
    return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_SERVER, MAAGE_OPERATION_SNAPSHOT);
}

/* Gameplay effect pitch never changes terrain, behavior or weather RNG. */
static unsigned long server_effect_random(maage_server_runtime *runtime, unsigned long bound) {
    runtime->effect_random = (runtime->effect_random * 1664525UL + 1013904223UL) & 0xffffffffUL;
    return (runtime->effect_random >> 8) % bound;
}

static maage_status server_reject(maage_server_runtime *runtime, unsigned char code, unsigned char operation) {
    maage_status result = server_status(code, operation);
    maage_status_trace trace;
    maage_status_trace_init(&trace);
    MAAGE_STATUS_TRACE(&trace, result, MAAGE_CONTEXT_SERVER, operation);
    server_status_event(runtime, result, &trace);
    return result;
}

static maage_status server_close_container(maage_server_runtime *runtime) {
    maage_status result;
    const maage_container_definition *definition = maage_container_get(maage_content_containers(runtime->content), runtime->game.crafting.kind);
    runtime->publication_dirty = 1;
    if (definition && definition->creative_catalog && runtime->game.mode == MAAGE_GAME_MODE_CREATIVE && runtime->game.health && runtime->game.inventory.cursor.count) {
        runtime->game.crafting.closing = 1;
        result = maage_gameplay_drop(&runtime->game, &runtime->player, runtime->world, maage_content_items(runtime->content), runtime->yaw, runtime->pitch, MAAGE_INVENTORY_SLOTS, 1);
        if (maage_status_failed(result)) return result;
    }
    result = maage_container_close(maage_content_containers(runtime->content), runtime->world, &runtime->game, &runtime->player);
    runtime->publication_dirty = 1;
    return result;
}
static void server_service_container(maage_server_runtime *runtime) {
    maage_crafting_container *container = &runtime->game.crafting;
    maage_status result;
    if (!container->closing && (!container->kind || (runtime->game.health && maage_container_accessible(maage_content_containers(runtime->content), container, &runtime->player, runtime->world)))) return;
    result = server_close_container(runtime);
    if (maage_status_failed(result) && result != runtime->crafting_failure) server_status_event(runtime, result, NULL);
    runtime->crafting_failure = result;
}

static void server_record_action(maage_server_runtime *runtime, int placing) {
    const maage_registry_bindings *ids = maage_block_registry_bindings(maage_content_blocks(runtime->content));
    unsigned int i = runtime->action_cursor, n;
    int dx, dz;
    maage_world_target *target = &runtime->actions[i].target;
    memset(&runtime->actions[i], 0, sizeof(runtime->actions[i]));
    runtime->actions[i].available = 1;
    runtime->actions[i].placing = placing;
    *target = placing ? runtime->game.last_place : runtime->game.last_break;
    runtime->actions[i].replacement = placing ? runtime->game.last_placed_state : ids->state_air;
    runtime->actions[i].tick = runtime->game.ticks;
    runtime->actions[i].command_sequence = runtime->gameplay_command_sequence;
    runtime->actions[i].completed_at = runtime->diagnostics ? maage_time_monotonic_seconds() : -1;
    for (dz = -1; dz <= 1; ++dz)
        for (dx = -1; dx <= 1; ++dx) {
            n = (unsigned int) (dx + 1 + 3 * (dz + 1));
            runtime->actions[i].revisions[n] = maage_world_column_revision(runtime->world, maage_world_chunk_coordinate(target->x + dx), maage_world_chunk_coordinate(target->z + dz));
        }
    if (runtime->diagnostics) MAAGE_LOG_VERBOSE_EVENT("maage.server.latency", maage_log_verbose("maage.server.latency", "mutation_tick=%lu command_sequence=%lu placing=%d block=%d,%d,%d revision=%lu completed_at=%.6f", runtime->actions[i].tick, runtime->actions[i].command_sequence, placing, target->x, target->y, target->z, runtime->actions[i].revisions[4], runtime->actions[i].completed_at));
    runtime->action_cursor = (i + 1U) % 32U;
}

static void server_predictions(maage_server_runtime *runtime) {
    unsigned int i, n, action;
    maage_status result;
    for (i = 0; i < 32; ++i) {
        maage_session_event *event = &runtime->predictions[i].event;
        maage_world_target *target = &runtime->predictions[i].target;
        if (!runtime->predictions[i].active) continue;
        if (!runtime->predictions[i].resolved) {
            /* Search oldest first. A newer-than-baseline mutation proves the
             * visual result even when held gameplay outruns its preview input.
             * Current target/input cannot invalidate that completed result. */
            for (n = 0; n < 32; ++n) {
                action = (runtime->action_cursor + n) % 32U;
                if (!runtime->actions[action].available || runtime->actions[action].placing != runtime->predictions[i].placing ||
                    runtime->actions[action].revisions[4] <= runtime->predictions[i].base_revision ||
                    runtime->actions[action].target.x != target->x || runtime->actions[action].target.y != target->y || runtime->actions[action].target.z != target->z || runtime->actions[action].target.state_id != target->state_id ||
                    (runtime->predictions[i].placing && runtime->actions[action].replacement != runtime->predictions[i].replacement)) continue;
                event->accepted = 1;
                event->status = server_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_MUTATE);
                event->tick = runtime->actions[action].tick;
                memcpy(event->column_revisions, runtime->actions[action].revisions, sizeof(event->column_revisions));
                if (runtime->diagnostics) MAAGE_LOG_VERBOSE_EVENT("maage.server.latency", maage_log_verbose("maage.server.latency", "action=%lu mutation_tick=%lu completed_at=%.6f confirmation_ready_at=%.6f", event->prediction, event->tick, runtime->actions[action].completed_at, maage_time_monotonic_seconds()));
                runtime->actions[action].available = 0;
                runtime->predictions[i].resolved = 1;
                break;
            }
            if (!runtime->predictions[i].resolved && (!runtime->game.health || runtime->paused || runtime->game.crafting.kind || runtime->game.crafting.closing || (runtime->game.ticks != runtime->predictions[i].admitted_tick && (runtime->predictions[i].placing ? (!runtime->game.use_held || runtime->game.use_delay == 4 || runtime->game.ticks - runtime->predictions[i].admitted_tick >= 5) : (!runtime->game.attack_held || !runtime->game.has_target || runtime->game.target.x != target->x || runtime->game.target.y != target->y || runtime->game.target.z != target->z || maage_world_get(runtime->world, target->x, target->y, target->z) != target->state_id))))) {
                event->status = maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_MUTATE, MAAGE_DETAIL_STALE);
                event->tick = runtime->game.ticks;
                runtime->predictions[i].resolved = 1;
            }
        }
    }
    /* Resolve every pending action even when the event FIFO is full. Its
     * immutable result must survive later mutations and journal reuse. */
    for (i = 0; i < 32; ++i) {
        maage_session_event *event = &runtime->predictions[i].event;
        if (!runtime->predictions[i].active || !runtime->predictions[i].resolved || !runtime->config.session) continue;
        if (!event->sequence) {
            event->protocol_version = MAAGE_SESSION_PROTOCOL_VERSION;
            event->kind = MAAGE_EVENT_BLOCK_ACTION;
            event->session_epoch = event->world_epoch = 1;
            event->sequence = ++runtime->event_sequence;
            event->emitted_at = maage_time_monotonic_seconds();
        }
        result = maage_local_session_publish_event(runtime->config.session, event);
        if (maage_status_code(result) == MAAGE_STATUS_BUFFER_OVERFLOW) break;
        if (maage_status_failed(result) && maage_status_code(result) != MAAGE_STATUS_INVALID_STATE) {
            server_capture_failure(runtime, result, NULL);
            break;
        }
        if (runtime->diagnostics) MAAGE_LOG_VERBOSE_EVENT("maage.server.latency", maage_log_verbose("maage.server.latency", "action=%lu accepted=%d revision=%lu owner_tick=%lu acknowledgment_offered_at=%.6f event_queue_ms=%.3f", event->prediction, event->accepted, event->column_revisions[4], event->tick, maage_time_monotonic_seconds(), (maage_time_monotonic_seconds() - event->emitted_at) * 1000));
        runtime->predictions[i].active = 0;
    }
}
static maage_status server_predict_request(maage_server_runtime *runtime, const maage_session_command *command) {
    unsigned int i;
    if (!command->prediction || command->prediction <= runtime->last_prediction || command->prediction > 4294967295UL || !command->prediction_revision || !command->prediction_command_sequence || command->prediction_command_sequence >= command->sequence) return server_reject(runtime, MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_VALIDATE);
    if (runtime->diagnostics) MAAGE_LOG_VERBOSE_EVENT("maage.server.latency", maage_log_verbose("maage.server.latency", "action=%lu sequence=%lu input_sequence=%lu received_at=%.6f tick=%lu base_revision=%lu", command->prediction, command->sequence, command->prediction_command_sequence, maage_time_monotonic_seconds(), runtime->tick, command->prediction_revision));
    runtime->last_prediction = command->prediction;
    for (i = 0; i < 32 && runtime->predictions[i].active; ++i) {
    }
    if (i == 32) return server_reject(runtime, MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_OPERATION_SUBMIT);
    memset(&runtime->predictions[i], 0, sizeof(runtime->predictions[i]));
    runtime->predictions[i].active = 1;
    runtime->predictions[i].target = command->predicted_target;
    runtime->predictions[i].placing = command->kind == MAAGE_CMD_PLACE_PREVIEW;
    runtime->predictions[i].replacement = command->predicted_state;
    runtime->predictions[i].base_revision = command->prediction_revision;
    runtime->predictions[i].admitted_tick = runtime->game.ticks;
    runtime->predictions[i].event.prediction = command->prediction;
    runtime->predictions[i].event.block.x = command->predicted_target.x;
    runtime->predictions[i].event.block.y = command->predicted_target.y;
    runtime->predictions[i].event.block.z = command->predicted_target.z;
    /* Observe the next gameplay step or a recorded result. A command-service
     * raycast uses a different player/input age and can falsely reject a valid
     * preview before that input has ever been consumed by gameplay. */
    return server_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_SUBMIT);
}

static maage_status server_apply_command(maage_server_runtime *runtime, const maage_session_command *command) {
    maage_status result;
    const maage_item_registry *items = maage_content_items(runtime->content);
    const maage_block_registry *blocks = maage_content_blocks(runtime->content);
    if (command->protocol_version != MAAGE_SESSION_PROTOCOL_VERSION) return server_reject(runtime, MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_VALIDATE);
    /* Reject missing, stale or replayed command identities. */
    if (!command->sequence || command->sequence <= runtime->last_command_sequence) return server_reject(runtime, MAAGE_STATUS_INVALID_STATE, MAAGE_OPERATION_VALIDATE);
    runtime->last_command_sequence = command->sequence;
    if (command->kind != MAAGE_CMD_INPUT && command->kind != MAAGE_CMD_VIEW && command->kind != MAAGE_CMD_HOTBAR) runtime->publication_dirty = 1;
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
            (command->intent.attack_pressed != 0 && command->intent.attack_pressed != 1) ||
            (command->intent.use_pressed != 0 && command->intent.use_pressed != 1) ||
            (command->intent.jump_pressed != 0 && command->intent.jump_pressed != 1) ||
            (command->intent.movement.jump != 0 && command->intent.movement.jump != 1) ||
            (command->intent.movement.sprint != 0 && command->intent.movement.sprint != 1) ||
            (command->intent.forward_pressed != 0 && command->intent.forward_pressed != 1) ||
            (command->intent.descend != 0 && command->intent.descend != 1))
            return server_reject(runtime, MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_VALIDATE);
        runtime->pending_jump |= command->intent.jump_pressed;
        runtime->pending_forward |= command->intent.forward_pressed;
        runtime->pending_attack |= command->intent.attack_pressed;
        runtime->pending_use |= command->intent.use_pressed;
        runtime->sampled = command->intent;
        runtime->input_sequence = command->sequence;
        runtime->input_received_at = runtime->diagnostics ? maage_time_monotonic_seconds() : -1;
        /* jump_pressed is an edge, unlike held movement.jump/attack/use.
         * Only the pending latch may survive to the next simulation step. */
        runtime->sampled.jump_pressed = runtime->sampled.forward_pressed = 0;
        runtime->sampled.attack_pressed = runtime->sampled.use_pressed = 0;
        runtime->yaw = command->intent.movement.yaw;
        runtime->pitch = command->intent.pitch;
        return server_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_UPDATE);
    case MAAGE_CMD_BREAK_PREVIEW:
    case MAAGE_CMD_PLACE_PREVIEW:
        return server_predict_request(runtime, command);
    case MAAGE_CMD_HOTBAR:
        if (command->hotbar_slot >= MAAGE_HOTBAR_SLOTS) return server_reject(runtime, MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_SELECT);
        runtime->game.inventory.selected = command->hotbar_slot;
        return server_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_SELECT);
    case MAAGE_CMD_CONTAINER_OPEN:
        if (!runtime->game.health || runtime->game.crafting.kind || runtime->game.crafting.closing) return server_reject(runtime, MAAGE_STATUS_INVALID_STATE, MAAGE_OPERATION_OPEN);
        result = maage_container_open(maage_content_containers(runtime->content), &runtime->game.crafting, maage_container_inventory(maage_content_containers(runtime->content)), 0, 0, 0, runtime->world);
        runtime->publication_dirty = 1;
        break;
    case MAAGE_CMD_CONTAINER_PAGE:
        if (!command->container_identity || command->container_identity != runtime->game.crafting.identity || !runtime->game.health || !maage_container_accessible(maage_content_containers(runtime->content), &runtime->game.crafting, &runtime->player, runtime->world)) return server_reject(runtime, MAAGE_STATUS_ACCESS_DENIED, MAAGE_OPERATION_SET);
        result = maage_container_page(maage_content_containers(runtime->content), &runtime->game.crafting, command->container_view_first);
        runtime->publication_dirty = 1;
        break;
    case MAAGE_CMD_CONTAINER_CLOSE:
    case MAAGE_CMD_CONTAINER_SLOT:
        if (!command->container_identity || command->container_identity != runtime->game.crafting.identity || !runtime->game.crafting.kind) return server_reject(runtime, MAAGE_STATUS_INVALID_STATE, MAAGE_OPERATION_VALIDATE);
        if (command->kind == MAAGE_CMD_CONTAINER_CLOSE) result = server_close_container(runtime);
        else {
            if (!runtime->game.health || !maage_container_accessible(maage_content_containers(runtime->content), &runtime->game.crafting, &runtime->player, runtime->world)) {
                server_service_container(runtime);
                return server_reject(runtime, MAAGE_STATUS_ACCESS_DENIED, MAAGE_OPERATION_MUTATE);
            }
            if (runtime->game.mode == MAAGE_GAME_MODE_CREATIVE && maage_container_get(maage_content_containers(runtime->content), runtime->game.crafting.kind)->creative_catalog) return server_reject(runtime, MAAGE_STATUS_ACCESS_DENIED, MAAGE_OPERATION_MUTATE);
            result = maage_container_action(maage_content_containers(runtime->content), runtime->world, &runtime->game, &runtime->player, maage_content_recipes(runtime->content), command->container_revision, command->slot_group, command->container_slot, command->slot_action);
        }
        runtime->publication_dirty = 1;
        break;
    case MAAGE_CMD_DROP:
        if (runtime->game.crafting.closing || (command->inventory_slot == MAAGE_INVENTORY_SLOTS && (!runtime->game.crafting.kind || command->container_identity != runtime->game.crafting.identity))) return server_reject(runtime, MAAGE_STATUS_INVALID_STATE, MAAGE_OPERATION_MUTATE);
        result = maage_gameplay_drop(&runtime->game, &runtime->player, runtime->world, items, runtime->yaw, runtime->pitch, command->inventory_slot, command->secondary_click);
        if (!maage_status_failed(result) && runtime->game.crafting.kind) ++runtime->game.crafting.revision;
        runtime->publication_dirty = 1;
        break;
    case MAAGE_CMD_CREATIVE_ACTION:
        result = maage_server_creative_action(runtime->content, runtime->world, &runtime->game, &runtime->player, command, runtime->yaw, runtime->pitch);
        break;
    case MAAGE_CMD_GAME_MODE:
        if (command->game_mode != MAAGE_GAME_MODE_SURVIVAL && command->game_mode != MAAGE_GAME_MODE_CREATIVE) return server_reject(runtime, MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_SET);
        result = server_close_container(runtime);
        if (maage_status_failed(result)) break;
        result = maage_gameplay_set_mode(&runtime->game, command->game_mode);
        runtime->player.sprinting = 0;
        runtime->pending_forward = 0;
        break;
    case MAAGE_CMD_RESPAWN:
        result = server_close_container(runtime);
        if (maage_status_failed(result)) break;
        result = maage_gameplay_respawn(&runtime->game, &runtime->player, runtime->world);
        runtime->pending_jump = runtime->pending_attack = runtime->pending_use = runtime->pending_forward = 0;
        break;
    case MAAGE_CMD_PAUSE:
        if (runtime->config.execution_mode != MAAGE_SERVER_INTEGRATED) return server_reject(runtime, MAAGE_STATUS_ACCESS_DENIED, MAAGE_OPERATION_SET);
        if (command->paused != 0 && command->paused != 1) return server_reject(runtime, MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_VALIDATE);
        runtime->paused = command->paused;
        runtime->player.sprinting = 0;
        runtime->game.sprint_window = runtime->game.sprint_ticks = 0;
        runtime->game.forward_down = 0;
        /* Modal input is not allowed to reappear as an action on resume. */
        memset(&runtime->sampled, 0, sizeof(runtime->sampled));
        runtime->pending_jump = runtime->pending_attack = runtime->pending_use = runtime->pending_forward = 0;
        return server_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_SET);
    case MAAGE_CMD_VIEW:
        if (command->render_radius < 1 ||
            command->render_radius > MAAGE_WORLD_MAX_RADIUS ||
            !server_finite(command->view_yaw) ||
            fabs(command->view_yaw) > 1000000000.0)
            return server_reject(runtime, MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_VALIDATE);
        result = maage_world_set_render_distance(runtime->world, command->render_radius, 0);
        if (!maage_status_failed(result)) {
            if (command->render_radius != runtime->config.render_distance) {
                MAAGE_LOG_VERBOSE_EVENT("maage.server.view", maage_log_verbose("maage.server.view", "Render distance updated from %d to %d chunks.", runtime->config.render_distance, command->render_radius));
                runtime->publication_dirty = 1;
            }
            runtime->config.render_distance = command->render_radius;
            runtime->yaw = command->view_yaw;
        }
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
static maage_status server_take_commands(maage_server_runtime *runtime) {
    maage_session_command command;
    maage_status result;
    unsigned int count;
    int pending;
    double received_at;
    if (!runtime->config.session) return server_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_POLL);
    for (count = 0; count < MAAGE_SERVER_COMMAND_BUDGET; ++count) {
        pending = !runtime->paused && (runtime->pending_attack || runtime->pending_use) && maage_world_chunk_loaded(runtime->world, maage_world_chunk_coordinate((int) floor(runtime->player.position[0])), maage_world_chunk_coordinate((int) floor(runtime->player.position[2])));
        if (runtime->has_deferred_command) {
            if (pending) break;
            command = runtime->deferred_command;
            received_at = runtime->deferred_received_at;
            runtime->has_deferred_command = 0;
            result = server_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_READ);
        } else {
            result = maage_local_session_take_command(runtime->config.session, &command);
            received_at = runtime->diagnostics ? maage_time_monotonic_seconds() : -1;
        }
        if (maage_status_code(result) == MAAGE_STATUS_NOOP) break;
        if (maage_status_code(result) == MAAGE_STATUS_INVALID_STATE) {
            /* A closed Client mailbox is a disconnected session, not an
             * invitation to keep advancing orphaned integrated gameplay. */
            maage_server_runtime_request_stop(runtime);
            break;
        }
        if (maage_status_failed(result)) return result;
        /* Consume each press with its sampled view/selection on one owner
         * step. OR-ing several FIFO presses into one latch loses clicks and
         * can replace a press's target before gameplay ever observes it. */
        if (pending && command.kind != MAAGE_CMD_BREAK_PREVIEW && command.kind != MAAGE_CMD_PLACE_PREVIEW) {
            runtime->deferred_command = command;
            runtime->deferred_received_at = received_at;
            runtime->has_deferred_command = 1;
            break;
        }
        {
            double began = runtime->diagnostics ? maage_time_monotonic_seconds() : -1;
            if (runtime->diagnostics && (command.prediction || command.intent.attack_pressed || command.intent.use_pressed)) MAAGE_LOG_VERBOSE_EVENT("maage.server.latency", maage_log_verbose("maage.server.latency", "action=%lu command_sequence=%lu kind=%u received_at=%.6f applied_at=%.6f tick=%lu command_queue_ms=%.3f fifo_wait_ms=%.3f input_age_ms=%.3f", command.prediction, command.sequence, command.kind, received_at, began, runtime->tick, command.sent_at >= 0 ? (received_at - command.sent_at) * 1000 : -1, (began - received_at) * 1000, command.sampled_at >= 0 ? (began - command.sampled_at) * 1000 : -1));
            (void) server_apply_command(runtime, &command); /* rejected action is not fatal */
            if (runtime->diagnostics && command.prediction) MAAGE_LOG_VERBOSE_EVENT("maage.server.latency", maage_log_verbose("maage.server.latency", "action=%lu command_ms=%.3f", command.prediction, (maage_time_monotonic_seconds() - began) * 1000));
        }
        if (command.kind == MAAGE_CMD_DISCONNECT) break;
    }
    return server_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_POLL);
}

static int server_entity_visible(const maage_server_runtime *runtime, const maage_entity *entity) {
    double dx = entity->position[0] - runtime->player.position[0];
    double dy = entity->position[1] - runtime->player.position[1];
    double dz = entity->position[2] - runtime->player.position[2];
    return dx * dx + dy * dy + dz * dz <= 4096 && maage_world_chunk_loaded(runtime->world, maage_world_chunk_coordinate((int) floor(entity->position[0])), maage_world_chunk_coordinate((int) floor(entity->position[2])));
}

static void server_publish_entities(maage_server_runtime *runtime, unsigned long tick) {
    maage_session_entities view;
    const maage_entities *entities = maage_world_entities(runtime->world);
    const maage_entity *entity;
    size_t i, visible = 0;
    maage_status result;
    memset(&view, 0, sizeof(view));
    /* Allocate only the actual near-player interest set, with one pending
     * publication. No pointers into mutable World storage cross threads. */
    for (i = 0; i < maage_entities_count(entities); ++i)
        if (server_entity_visible(runtime, maage_entities_at(entities, i))) ++visible;
    if (visible) view.entities = (maage_entity_view *) malloc(visible * sizeof(*view.entities));
    if (visible && !view.entities) {
        result = server_status(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_SNAPSHOT);
        if (runtime->entity_publication_failure != result) server_status_event(runtime, result, NULL);
        runtime->entity_publication_failure = result;
    } else {
        runtime->entity_publication_failure = 0;
        for (i = 0; i < maage_entities_count(entities); ++i) {
            entity = maage_entities_at(entities, i);
            if (server_entity_visible(runtime, entity)) maage_entity_copy_view(entity, &view.entities[view.count++]);
        }
        view.protocol_version = MAAGE_SESSION_PROTOCOL_VERSION;
        view.session_epoch = 1;
        view.world_epoch = 1;
        view.tick = tick;
        view.sequence = ++runtime->entity_sequence;
        view.offered_at = runtime->diagnostics ? maage_time_monotonic_seconds() : -1;
        result = maage_local_session_offer_entities(runtime->config.session, &view);
        if (maage_status_failed(result)) server_status_event(runtime, result, NULL);
        if (runtime->diagnostics) MAAGE_LOG_VERBOSE_EVENT("maage.server.latency", maage_log_verbose("maage.server.latency", "entity_sequence=%lu entity_tick=%lu count=%lu offered_at=%.6f", view.sequence, tick, (unsigned long) visible, view.offered_at));
        free(view.entities);
    }
}

static void server_publish_snapshot(maage_server_runtime *runtime, double now) {
    maage_session_snapshot snapshot;
    int cell_x, cell_y, cell_z;
    maage_status result;
    if (!runtime->config.session || !runtime->world || !runtime->game_ready) return;
    memset(&snapshot, 0, sizeof(snapshot));
    snapshot.protocol_version = MAAGE_SESSION_PROTOCOL_VERSION;
    snapshot.session_epoch = snapshot.world_epoch = 1UL;
    snapshot.tick = runtime->tick;
    snapshot.paused = runtime->paused;
    snapshot.sequence = ++runtime->publication_sequence;
    snapshot.applied_command_sequence = runtime->last_command_sequence;
    snapshot.gameplay_command_sequence = runtime->gameplay_command_sequence;
    snapshot.next_tick_at = runtime->next_tick_at;
    snapshot.player.player = runtime->player;
    snapshot.player.yaw = runtime->yaw;
    snapshot.player.pitch = runtime->pitch;
    snapshot.player.inventory = runtime->game.inventory;
    snapshot.player.crafting = runtime->game.crafting;
    snapshot.player.experience = runtime->game.experience;
    result = maage_container_sample(maage_content_containers(runtime->content), runtime->world, &snapshot.player.crafting);
    if (maage_status_failed(result)) {
        server_status_event(runtime, result, NULL);
        return;
    }
    snapshot.player.food = runtime->game.food;
    runtime->publication_dirty = 0;
    snapshot.player.game_mode = runtime->game.mode;
    snapshot.player.health = runtime->game.health;
    snapshot.player.air = runtime->game.air;
    snapshot.player.hunger = runtime->game.hunger;
    snapshot.player.saturation = runtime->game.saturation;
    snapshot.player.flying = runtime->game.flying;
    snapshot.player.sneaking = runtime->sneaking;
    server_player_motion(runtime, &snapshot.player.motion);
    snapshot.player.damage_sequence = runtime->game.damage_sequence;
    snapshot.player.damage_tick = runtime->game.damage_tick;
    snapshot.player.death_tick = runtime->game.death_tick;
    snapshot.player.damage_yaw = runtime->game.damage_yaw;
    snapshot.player.has_target = runtime->game.has_target;
    snapshot.player.breaking_active = runtime->game.breaking_active;
    snapshot.player.reach = runtime->game.reach > 0 ? runtime->game.reach : (runtime->game.mode == MAAGE_GAME_MODE_CREATIVE ? 5.0 : 4.5);
    snapshot.player.break_delay = runtime->game.break_delay;
    snapshot.player.attack_miss_delay = runtime->game.attack_miss_delay;
    snapshot.player.submerged = maage_player_submerged(&runtime->player, runtime->world);
    snapshot.player.target = runtime->game.target;
    snapshot.player.breaking = runtime->game.breaking;
    snapshot.player.break_progress = runtime->game.break_progress;
    snapshot.player.breaking_item = runtime->game.breaking_item;
    snapshot.player.breaking_metadata = runtime->game.breaking_metadata;
    snapshot.player.breaking_damage = runtime->game.breaking_damage;
    snapshot.player.blocks_broken = runtime->game.blocks_broken;
    snapshot.player.last_break = runtime->game.last_break;
    snapshot.player.last_break_tick = runtime->last_break_tick;
    snapshot.player.swing = runtime->game.swing;
    snapshot.player.swing_tick = runtime->game.swing_tick;
    snapshot.player.swing_kind = runtime->game.swing_kind;
    maage_world_environment(runtime->world, &snapshot.environment);
    cell_x = (int) floor(runtime->player.position[0]);
    cell_y = (int) floor(runtime->player.position[1] + MAAGE_PLAYER_EYE_HEIGHT);
    cell_z = (int) floor(runtime->player.position[2]);
    if (cell_y >= 0 && cell_y < MAAGE_WORLD_HEIGHT) snapshot.has_observation = maage_world_observe(runtime->world, cell_x, cell_y, cell_z, &snapshot.observation);
    snapshot.observation.light = 0;
    snapshot.observation.light_available = 0;
    if (runtime->last_stats_time == 0.0 || now - runtime->last_stats_time >= 0.25) {
        maage_world_get_stats(runtime->world, &runtime->stats);
        runtime->last_stats_time = now;
    }
    snapshot.world_stats = runtime->stats;
    (void) maage_local_session_publish_snapshot(runtime->config.session, &snapshot);
    server_publish_entities(runtime, snapshot.tick);
}

static void server_offer_columns(maage_server_runtime *runtime) {
    unsigned int n;
    size_t available;
    maage_local_session_backlog backlog;
    double began;
    maage_column_update *update, accepted;
    maage_local_column_update outgoing;
    maage_status result;
    if (!runtime->world || !runtime->config.session) return;
    result = maage_local_session_get_backlog(runtime->config.session, &backlog);
    if (maage_status_failed(result)) {
        server_capture_failure(runtime, result, NULL);
        return;
    }
    available = backlog.column_capacity - backlog.columns;
    began = maage_time_monotonic_seconds();
    for (n = 0; n < MAAGE_SERVER_COLUMN_BUDGET && n < available; ++n) {
        result = maage_world_next_column(runtime->world, &update);
        if (maage_status_code(result) == MAAGE_STATUS_NOOP) return;
        if (maage_status_failed(result)) {
            server_capture_failure(runtime, result, maage_world_trace(runtime->world));
            return;
        }
        memset(&outgoing, 0, sizeof(outgoing));
        outgoing.x = update->x;
        outgoing.z = update->z;
        outgoing.revision = update->revision;
        outgoing.world_epoch = 1UL;
        outgoing.offered_at = runtime->diagnostics ? maage_time_monotonic_seconds() : -1;
        outgoing.column = update;
        accepted = *update; /* Copy identity before queue ownership can reach the Client. */
        result = maage_local_session_offer_column(runtime->config.session, &outgoing);
        if (maage_status_code(result) == MAAGE_STATUS_SUCCESS) {
            if (runtime->diagnostics) MAAGE_LOG_VERBOSE_EVENT("maage.server.latency", maage_log_verbose("maage.server.latency", "phase=column_offered initial=%d unload=%d column=%d,%d revision=%lu offered_at=%.6f tick=%lu", accepted.base_revision == 0, accepted.unload, accepted.x, accepted.z, accepted.revision, outgoing.offered_at, runtime->tick));
            result = maage_world_ack_column(runtime->world, &accepted);
        }
        maage_column_update_destroy(outgoing.column);
        if (maage_status_failed(result)) {
            if (maage_status_code(result) != MAAGE_STATUS_BUFFER_OVERFLOW && maage_status_code(result) != MAAGE_STATUS_INVALID_STATE) server_capture_failure(runtime, result, NULL);
            return; /* Retain the unsent revision and retry after backpressure. */
        }
        if (maage_time_monotonic_seconds() - began >= 0.002) break;
    }
}

static maage_status server_open_world(maage_server_runtime *runtime, maage_status_trace *trace) {
    maage_world_config world_config;
    maage_content_config content_config;
    const maage_item_registry *items;
    maage_status result;
    char error[256];
    maage_application_content_config(&content_config);
    content_config.execution.side = runtime->config.execution_mode == MAAGE_SERVER_INTEGRATED ? MAAGE_EXECUTION_INTEGRATED_SERVER : MAAGE_EXECUTION_DEDICATED_SERVER;
    content_config.execution.capabilities = MAAGE_EXECUTION_AUTHORITY | MAAGE_EXECUTION_HEADLESS;
    if (runtime->config.content_peer) {
        result = maage_content_composition(runtime->config.content_peer, &content_config.modules, &content_config.module_count);
        if (maage_status_failed(result)) return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_SERVER, MAAGE_OPERATION_INITIALIZE);
        content_config.identity_peer = runtime->config.content_peer;
    }
    result = maage_content_create(&content_config, &runtime->owned_content, trace);
    if (maage_status_failed(result)) return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_SERVER, MAAGE_OPERATION_INITIALIZE);
    runtime->content = runtime->owned_content;
    if (!maage_block_registry_is_finalized(maage_content_blocks(runtime->content)) || !maage_item_registry_is_finalized(maage_content_items(runtime->content))) return MAAGE_STATUS_TRACE(trace, server_status(MAAGE_STATUS_INVALID_STATE, MAAGE_OPERATION_INITIALIZE), MAAGE_CONTEXT_SERVER, MAAGE_OPERATION_INITIALIZE);
    items = maage_content_items(runtime->content);
    memset(&runtime->record, 0, sizeof(runtime->record));
    error[0] = 0;
    result = maage_world_store_open(runtime->config.instance_root, runtime->config.world_id, runtime->config.create_world, runtime->config.world_name, runtime->config.has_seed ? &runtime->config.seed : NULL, runtime->config.generation_policy, runtime->content, &runtime->store, &runtime->record, error, trace);
    if (maage_status_failed(result)) {
        maage_log_trace_status(MAAGE_LOG_ERROR, "maage.server.world", result, trace, "%s", error);
        return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_SERVER, MAAGE_OPERATION_OPEN);
    }
    result = maage_world_config_init(&world_config);
    if (maage_status_failed(result)) return result;
    world_config.execution = *maage_content_execution(runtime->content);
    world_config.registry = maage_content_blocks(runtime->content);
    world_config.items = items;
    world_config.entity_types = maage_content_entity_types(runtime->content);
    world_config.entity_policy = runtime->config.entity_policy;
    world_config.smelting = maage_content_smelting(runtime->content);
    world_config.fuels = maage_content_fuels(runtime->content);
    world_config.biomes = maage_content_biomes(runtime->content);
    world_config.seed = runtime->record.seed;
    runtime->effect_random = (runtime->record.seed.low ^ runtime->record.seed.high ^ runtime->record.environment.ticks_low ^ 0x9e3779b9UL) & 0xffffffffUL;
    world_config.build_meshes = 0;
    world_config.store = runtime->store;
    world_config.worker_count = runtime->config.world_workers;
    world_config.render_distance = runtime->config.render_distance;
    world_config.new_world = runtime->record.new_world;
    memcpy(world_config.position, runtime->record.position, sizeof(world_config.position));
    world_config.generation_policy = runtime->record.generation_policy;
    world_config.initial_environment = &runtime->record.environment;
    world_config.initial_random_ticks = &runtime->record.random_ticks;
    result = maage_world_create(&world_config, &runtime->world, trace);
    if (maage_status_failed(result)) return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_SERVER, MAAGE_OPERATION_CREATE);
    result = maage_entities_restore(maage_world_entities(runtime->world), items, &runtime->record.entities);
    if (maage_status_failed(result)) return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_SERVER, MAAGE_OPERATION_LOAD);
    result = maage_player_spawn(&runtime->player, runtime->world);
    if (maage_status_failed(result)) return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_SERVER, MAAGE_OPERATION_INITIALIZE);
    runtime->player_ready = 1;
    result = maage_gameplay_init(&runtime->game, items);
    if (maage_status_failed(result)) return result;
    runtime->game_ready = 1;
    if (!runtime->record.new_world) {
        result = maage_gameplay_restore(&runtime->game, items, &runtime->record.gameplay);
        if (maage_status_failed(result)) return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_SERVER, MAAGE_OPERATION_LOAD);
    }
    if (runtime->config.initial_game_mode >= 0) {
        result = maage_gameplay_set_mode(&runtime->game, (unsigned int) runtime->config.initial_game_mode);
        if (maage_status_failed(result)) return result;
    }
    runtime->game.reach = runtime->config.interaction_range;
    runtime->yaw = runtime->record.yaw;
    runtime->pitch = runtime->record.pitch;
    runtime->sampled.movement.yaw = runtime->yaw;
    runtime->sampled.pitch = runtime->pitch;
    return server_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_INITIALIZE);
}

static void server_close_world(maage_server_runtime *runtime, maage_status_trace *trace) {
    maage_status result;
    maage_environment_state environment;
    maage_status_trace cleanup_trace;
    char error[256];
    if (runtime->world && runtime->game_ready && runtime->player_ready) {
        result = server_close_container(runtime);
        if (maage_status_failed(result)) {
            /* The current checkpoint retains unreturned input ownership; a
             * later load closes it before accepting gameplay commands. */
            server_capture_failure(runtime, result, NULL);
            server_status_event(runtime, result, NULL);
        }
    }
    if (runtime->world) {
        maage_status_trace_init(&cleanup_trace);
        result = maage_world_flush(runtime->world, &cleanup_trace);
        if (maage_status_failed(result)) {
            server_capture_failure(runtime, result, &cleanup_trace);
            server_status_event(runtime, result, &cleanup_trace);
            runtime->player_ready = 0; /* preserve previous metadata */
        }
    }
    if (runtime->store && runtime->world && runtime->player_ready && runtime->game_ready) {
        maage_world_environment(runtime->world, &environment);
        maage_status_trace_init(&cleanup_trace);
        error[0] = 0;
        result = maage_world_store_save(runtime->store, runtime->player.position, runtime->yaw, runtime->pitch, &runtime->game, runtime->world, &environment, error, &cleanup_trace);
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
    (void) trace;
}

static maage_status server_tick(maage_server_runtime *runtime, double scheduled_at, maage_status_trace *trace) {
    maage_gameplay_intent intent = runtime->sampled;
    maage_sound_event sounds[32];
    size_t sound_count = 0, i;
    maage_status result;
    unsigned long broken = runtime->game.blocks_broken, placed = runtime->game.blocks_placed;
    double began = runtime->diagnostics ? maage_time_monotonic_seconds() : -1, gameplay_finished, world_began;
    size_t entities_before = runtime->diagnostics ? maage_entities_count(maage_world_entities(runtime->world)) : 0;
    /* A pulse sampled between steps survives latest-state replacement, but
     * every pulse is consumed once. Held attack/use continue per tick. */
    intent.jump_pressed |= runtime->pending_jump;
    intent.forward_pressed |= runtime->pending_forward;
    intent.movement.jump |= runtime->pending_jump;
    intent.attack |= runtime->pending_attack;
    intent.use |= runtime->pending_use;
    intent.attack_pressed |= runtime->pending_attack;
    intent.use_pressed |= runtime->pending_use;
    if (runtime->game.crafting.kind || runtime->game.crafting.closing) memset(&intent, 0, sizeof(intent));
    intent.movement.yaw = runtime->yaw;
    intent.pitch = runtime->pitch;
    runtime->pending_jump = runtime->pending_attack = runtime->pending_use = runtime->pending_forward = 0;
    runtime->gameplay_command_sequence = runtime->last_command_sequence;
    result = maage_gameplay_tick(&runtime->game, &runtime->player, runtime->world, maage_content_items(runtime->content), &intent, sounds, sizeof(sounds) / sizeof(sounds[0]), &sound_count, trace);
    gameplay_finished = runtime->diagnostics ? maage_time_monotonic_seconds() : -1;
    if (runtime->game.blocks_broken != broken) {
        runtime->last_break_tick = runtime->game.ticks;
        server_record_action(runtime, 0);
        if (runtime->diagnostics)
            for (i = entities_before; i < maage_entities_count(maage_world_entities(runtime->world)); ++i)
                MAAGE_LOG_VERBOSE_EVENT("maage.server.latency", maage_log_verbose("maage.server.latency", "drop_entity=%lu mutation_tick=%lu block=%d,%d,%d created_by=%.6f", maage_entities_at(maage_world_entities(runtime->world), i)->id, runtime->game.ticks, runtime->game.last_break.x, runtime->game.last_break.y, runtime->game.last_break.z, gameplay_finished));
    }
    if (runtime->game.blocks_placed != placed) server_record_action(runtime, 1);
    if (runtime->diagnostics && (intent.attack || intent.attack_pressed)) MAAGE_LOG_VERBOSE_EVENT("maage.server.latency", maage_log_verbose("maage.server.latency", "gameplay_tick=%lu command_sequence=%lu input_sequence=%lu scheduled_at=%.6f began_at=%.6f schedule_late_ms=%.3f input_schedule_ms=%.3f gameplay_ms=%.3f progress=%.6f cooldown=%u active=%d target=%d,%d,%d grounded=%d submerged=%d", runtime->game.ticks, runtime->gameplay_command_sequence, runtime->input_sequence, scheduled_at, began, (began - scheduled_at) * 1000, runtime->input_sequence != runtime->consumed_input_sequence && runtime->input_received_at >= 0 ? (began - runtime->input_received_at) * 1000 : -1, (gameplay_finished - began) * 1000, runtime->game.break_progress, runtime->game.break_delay, runtime->game.breaking_active, runtime->game.target.x, runtime->game.target.y, runtime->game.target.z, runtime->player.grounded, maage_player_submerged(&runtime->player, runtime->world)));
    runtime->consumed_input_sequence = runtime->input_sequence;
    runtime->sneaking = intent.descend && !runtime->game.flying && runtime->game.health != 0;
    /* Effects describe outcomes already resolved by authoritative gameplay.
     * Transport saturation may omit a sound, never roll back block mutations. */
    for (i = 0; i < sound_count; ++i) {
        sounds[i].tick = runtime->tick + 1UL;
        server_emit_sound(runtime, &sounds[i]);
    }
    server_predictions(runtime);
    /* Drops already exist. Publish before entity physics, World updates,
     * streaming, or another catch-up step can delay their first view. */
    if (runtime->config.session && runtime->game.blocks_broken != broken) server_publish_entities(runtime, runtime->game.ticks);
    if (maage_status_failed(result)) return result;
    if (maage_status_is_warning(result)) {
        server_status_event(runtime, result, trace);
        maage_status_trace_init(trace);
    }
    {
        unsigned int collected = 0;
        maage_sound_event pickup;
        maage_entity_pickup pickups[32];
        size_t pickup_count = 0;
        result = maage_entities_tick(maage_world_entities(runtime->world), runtime->world, maage_content_items(runtime->content), &runtime->player, &runtime->game.inventory, runtime->game.health != 0, &collected, pickups, 32, &pickup_count);
        if (maage_status_failed(result)) return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_SERVER, MAAGE_OPERATION_TICK);
        if (pickup_count) {
            result = maage_gameplay_build_sound(&runtime->game, maage_content_sounds(runtime->content), &pickup, MAAGE_SOUND_ITEM_PICKUP, 0, runtime->player.position, 0.2, 1.6 + server_effect_random(runtime, 800) / 1000.0);
            if (!maage_status_failed(result)) {
                pickup.tick = runtime->tick + 1;
                server_emit_effect(runtime, &pickup, pickups, pickup_count);
            }
        }
    }
    result = maage_entities_living_tick(maage_world_entities(runtime->world), runtime->world, maage_content_items(runtime->content), &runtime->player, &runtime->game, server_entity_sound, runtime, trace);
    if (maage_status_failed(result)) return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_SERVER, MAAGE_OPERATION_TICK);
    server_service_container(runtime);
    server_predictions(runtime);
    world_began = runtime->diagnostics ? maage_time_monotonic_seconds() : -1;
    result = maage_world_tick(runtime->world);
    if (runtime->diagnostics && intent.attack) MAAGE_LOG_VERBOSE_EVENT("maage.server.latency", maage_log_verbose("maage.server.latency", "gameplay_tick=%lu post_gameplay_ms=%.3f world_tick_ms=%.3f", runtime->game.ticks, (world_began - gameplay_finished) * 1000, (maage_time_monotonic_seconds() - world_began) * 1000));
    if (maage_status_failed(result)) return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_SERVER, MAAGE_OPERATION_TICK);
    ++runtime->tick;
    result = server_emit_motion(runtime, trace);
    if (maage_status_failed(result)) return result;
    if (maage_status_is_warning(result)) server_status_event(runtime, result, trace);
    return server_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_TICK);
}

static void server_thread_entry(void *context) {
    maage_server_runtime *runtime = (maage_server_runtime *) context;
    maage_status result, waited;
    maage_status_trace trace;
    double now, deadline, remaining, last_published, next_stream;
    unsigned int caught_up;
    unsigned long sleep_ms, last_published_tick = 0UL;
    int center_x, center_z, spawn_ready, ready_logged = 0, last_published_paused = -1;
    maage_status_trace_init(&trace);
    result = server_open_world(runtime, &trace);
    if (maage_status_failed(result)) {
        server_capture_failure(runtime, result, &trace);
        goto shutdown;
    }
    now = maage_time_monotonic_seconds();
    if (now < 0.0) {
        server_capture_failure(runtime, server_status(MAAGE_STATUS_ERROR, MAAGE_OPERATION_INITIALIZE), NULL);
        goto shutdown;
    }
    deadline = now + MAAGE_SERVER_STEP_SECONDS;
    last_published = -1.0;
    next_stream = now;
    while (!server_stop_requested(runtime)) {
        /* Streaming continues while paused or awaiting spawn. Due simulation
         * uses already published columns before servicing background work. */
        maage_status_trace_init(&trace);
        server_service_container(runtime);
        result = server_take_commands(runtime);
        if (maage_status_failed(result)) {
            server_capture_failure(runtime, result, NULL);
            break;
        }
        server_predictions(runtime);
        now = maage_time_monotonic_seconds();
        if (now < 0.0) {
            server_capture_failure(runtime, server_status(MAAGE_STATUS_ERROR, MAAGE_OPERATION_QUERY), NULL);
            break;
        }
        server_publish_status(runtime);
        if (maage_status_failed(runtime->result)) break;
        now = maage_time_monotonic_seconds();
        if (now < 0.0) {
            server_capture_failure(runtime, server_status(MAAGE_STATUS_ERROR, MAAGE_OPERATION_QUERY), NULL);
            break;
        }
        center_x = maage_world_chunk_coordinate((int) floor(runtime->player.position[0]));
        center_z = maage_world_chunk_coordinate((int) floor(runtime->player.position[2]));
        spawn_ready = maage_world_chunk_loaded(runtime->world, center_x, center_z);
        if (spawn_ready && !ready_logged) {
            maage_log_info("maage.server", "%s server ready.", runtime->config.execution_mode == MAAGE_SERVER_INTEGRATED ? "Integrated" : "Dedicated");
            maage_mutex_lock(runtime->mutex);
            runtime->ready = 1;
            maage_mutex_unlock(runtime->mutex);
            ready_logged = 1;
        }
        if (runtime->paused || !spawn_ready) {
            /* Do not run delayed gameplay when resuming from pause/loading. */
            deadline = now + MAAGE_SERVER_STEP_SECONDS;
        } else {
            caught_up = 0;
            while (now >= deadline && caught_up < MAAGE_SERVER_MAX_CATCHUP && !runtime->paused && !server_stop_requested(runtime)) {
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
                result = server_tick(runtime, deadline, &trace);
                if (maage_status_failed(result)) {
                    server_capture_failure(runtime, result, &trace);
                    break;
                }
                ++caught_up;
                deadline += MAAGE_SERVER_STEP_SECONDS;
                now = maage_time_monotonic_seconds();
                if (now < 0.0) {
                    server_capture_failure(runtime, server_status(MAAGE_STATUS_ERROR, MAAGE_OPERATION_QUERY), NULL);
                    break;
                }
            }
            if (maage_status_failed(runtime->result)) break;
            if (now >= deadline && caught_up == MAAGE_SERVER_MAX_CATCHUP) {
                ++runtime->overloads;
                /* Preserve tick order; skip backlog wall-time, never fabricate
                 * thousands of gameplay ticks or fast-forward World time. */
                deadline = now + MAAGE_SERVER_STEP_SECONDS;
                server_status_event(runtime, server_status(MAAGE_STATUS_WARNING, MAAGE_OPERATION_TICK), NULL);
                if (runtime->overloads == 1UL || runtime->overloads % 100UL == 0UL) maage_log_warning("maage.server.tick", "20 TPS scheduler overloaded (%lu bounded catch-ups); simulation time was not accelerated.", runtime->overloads);
            }
        }
        /* Due authoritative ticks run before bounded streaming maintenance.
         * Collision uses published columns and conservatively rejects missing
         * terrain; it never waits for a worker or Client presentation. */
        /* Command wakeups can exceed this cadence; mouse input alone does
         * not force another World scan. */
        if (now >= next_stream || now >= deadline) {
            result = maage_world_stream(runtime->world, runtime->player.position[0], runtime->player.position[2], runtime->yaw, &trace);
            if (maage_status_failed(result)) {
                server_capture_failure(runtime, result, &trace);
                break;
            }
            if (maage_world_failed(runtime->world)) {
                server_capture_failure(runtime, maage_world_status(runtime->world), maage_world_trace(runtime->world));
                break;
            }
            if (maage_world_status(runtime->world) != runtime->last_world_notice) {
                runtime->last_world_notice = maage_world_status(runtime->world);
                server_status_event(runtime, runtime->last_world_notice, maage_world_trace(runtime->world));
            }
            next_stream = maage_time_monotonic_seconds() + MAAGE_SERVER_WAIT_MS / 1000.0;
        }
        server_service_container(runtime);
        server_predictions(runtime);
        runtime->next_tick_at = deadline;
        if (runtime->publication_dirty || last_published < 0.0 || now - last_published >= 0.1 || runtime->tick != last_published_tick || runtime->paused != last_published_paused) {
            server_publish_snapshot(runtime, now);
            last_published = now;
            last_published_tick = runtime->tick;
            last_published_paused = runtime->paused;
        }
        server_offer_columns(runtime);
        server_publish_status(runtime);
        if (maage_status_failed(runtime->result)) break;
        now = maage_time_monotonic_seconds();
        if (now < 0.0) {
            server_capture_failure(runtime, server_status(MAAGE_STATUS_ERROR, MAAGE_OPERATION_QUERY), NULL);
            break;
        }
        remaining = deadline - now;
        if (next_stream - now < remaining) remaining = next_stream - now;
        if (remaining < 0.001) remaining = 0.001;
        sleep_ms = (unsigned long) (remaining * 1000.0);
        if (!sleep_ms) sleep_ms = 1UL;
        if (sleep_ms > MAAGE_SERVER_WAIT_MS) sleep_ms = MAAGE_SERVER_WAIT_MS;
        if (runtime->has_deferred_command && !runtime->pending_attack && !runtime->pending_use) continue;
        if (runtime->config.session && !runtime->has_deferred_command) {
            waited = maage_local_session_wait_commands(runtime->config.session, sleep_ms);
            if (maage_status_failed(waited) && maage_status_code(waited) != MAAGE_STATUS_TIMEOUT && maage_status_code(waited) != MAAGE_STATUS_INVALID_STATE) server_capture_failure(runtime, waited, NULL);
        } else {
            maage_mutex_lock(runtime->mutex);
            if (!runtime->stop) {
                waited = maage_condition_timed_wait(runtime->wake, runtime->mutex, sleep_ms);
                if (maage_status_failed(waited) && maage_status_code(waited) != MAAGE_STATUS_TIMEOUT) server_capture_failure(runtime, waited, NULL);
            }
            maage_mutex_unlock(runtime->mutex);
        }
        if (maage_status_failed(runtime->result)) break;
    }
shutdown:
    maage_log_info("maage.server", "%s server shutting down...", runtime->config.execution_mode == MAAGE_SERVER_INTEGRATED ? "Integrated" : "Dedicated");
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
        (void) maage_local_session_publish_event(runtime->config.session, &disconnected);
        /* Terminal status remains drainable; new commands and publications
         * are now rejected by the transport's native shutdown barrier. */
        (void) maage_local_session_close(runtime->config.session);
    }
    maage_mutex_lock(runtime->mutex);
    runtime->finished = 1;
    maage_condition_broadcast(runtime->wake);
    maage_mutex_unlock(runtime->mutex);
}

maage_status maage_server_runtime_create(const maage_server_config *config, maage_server_runtime **output, maage_status_trace *trace) {
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
        (config->execution_mode == MAAGE_SERVER_INTEGRATED && (!config->session || !config->content_peer)) ||
        (config->session && !config->content_peer) ||
        (config->generation_policy &&
         !maage_generation_policy_valid(config->generation_policy)))
        return MAAGE_STATUS_TRACE(trace, server_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_CREATE), MAAGE_CONTEXT_SERVER, MAAGE_OPERATION_CREATE);
    runtime = (maage_server_runtime *) calloc(1, sizeof(*runtime));
    if (!runtime) return server_status(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_CREATE);
    runtime->diagnostics = maage_log_enabled("maage.server.latency", MAAGE_LOG_VERBOSE) && getenv("MAAGE_WORLD_DIAGNOSTICS") && !strcmp(getenv("MAAGE_WORLD_DIAGNOSTICS"), "1");
    runtime->config = *config;
    if (config->entity_policy) {
        runtime->entity_policy = *config->entity_policy;
        runtime->config.entity_policy = &runtime->entity_policy;
    }
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
    (void) maage_condition_destroy(runtime->wake);
    (void) maage_mutex_destroy(runtime->mutex);
    free(runtime->world_name);
    free(runtime->world_id);
    free(runtime->instance_root);
    free(runtime);
    return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_SERVER, MAAGE_OPERATION_CREATE);
}

maage_status maage_server_runtime_start(maage_server_runtime *runtime, maage_status_trace *trace) {
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

void maage_server_runtime_request_stop(maage_server_runtime *runtime) {
    if (!runtime || !runtime->mutex) return;
    maage_mutex_lock(runtime->mutex);
    runtime->stop = 1;
    maage_condition_broadcast(runtime->wake);
    maage_mutex_unlock(runtime->mutex);
}

int maage_server_runtime_ready(maage_server_runtime *runtime) {
    int ready;
    if (!runtime) return 0;
    maage_mutex_lock(runtime->mutex);
    ready = runtime->ready && !runtime->stop && !runtime->finished;
    maage_mutex_unlock(runtime->mutex);
    return ready;
}

int maage_server_runtime_finished(maage_server_runtime *runtime) {
    int finished;
    if (!runtime) return 1;
    maage_mutex_lock(runtime->mutex);
    finished = runtime->finished;
    maage_mutex_unlock(runtime->mutex);
    return finished;
}

maage_status maage_server_runtime_join(maage_server_runtime *runtime, maage_status_trace *trace) {
    maage_status result;
    if (!runtime) return server_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_WAIT);
    if (!runtime->started) return server_status(MAAGE_STATUS_INVALID_STATE, MAAGE_OPERATION_WAIT);
    if (!runtime->joined) {
        result = maage_thread_join(runtime->thread);
        if (maage_status_failed(result)) return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_SERVER, MAAGE_OPERATION_WAIT);
        runtime->joined = 1;
        runtime->thread = NULL;
    }
    if (trace) *trace = runtime->trace;
    return runtime->result;
}

maage_status maage_server_runtime_destroy(maage_server_runtime *runtime) {
    maage_status result;
    if (!runtime) return server_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_DESTROY);
    if (runtime->started && !runtime->joined) {
        maage_server_runtime_request_stop(runtime);
        result = maage_server_runtime_join(runtime, NULL);
        if (!runtime->joined) return result;
    }
    if (runtime->world) return server_status(MAAGE_STATUS_INVALID_STATE, MAAGE_OPERATION_DESTROY); /* borrowed workers/resources intentionally retained */
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
