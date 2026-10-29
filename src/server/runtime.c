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
#define MAAGE_SERVER_COLUMN_BUDGET 16U
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
    int pending_jump, pending_attack, pending_use, pending_forward;
    unsigned long tick, publication_sequence, event_sequence;
    maage_status entity_publication_failure;
    unsigned long last_command_sequence, overloads, dropped_sound_events;
    unsigned long audio_random, cave_ticks;
    maage_session_audio_observation audio_observation;
    maage_status last_world_notice, crafting_failure;
    int publication_dirty;
    maage_session_collision collision;
    int sneaking;
    unsigned long last_prediction;
    struct {
        int active, resolved;
        unsigned long counter, admitted_tick;
        maage_world_target target;
        maage_session_event event;
    } predictions[32];
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

/* Private bounded PRNG: soundscape sampling cannot change terrain/weather RNG. */
static unsigned long server_audio_random(maage_server_runtime *runtime, unsigned long bound) {
    runtime->audio_random = (runtime->audio_random * 1664525UL + 1013904223UL) & 0xffffffffUL;
    return (runtime->audio_random >> 8) % bound;
}

static void server_audio_observe(maage_server_runtime *runtime) {
    maage_session_audio_observation *audio = &runtime->audio_observation;
    const maage_block_registry *registry = maage_world_registry(runtime->world);
    const maage_block_state_definition *state, *above;
    const maage_block_definition *block;
    maage_world_observation observation, local;
    maage_environment_state environment;
    maage_sound_event sound;
    double position[3], dx, dy, dz, rain;
    int px, py, pz, x, y, z, cx, cz, i, samples;
    unsigned long water_count = 0, lava_count = 0, rain_count = 0;
    memset(audio, 0, sizeof(*audio));
    px = (int) floor(runtime->player.position[0]);
    py = (int) floor(runtime->player.position[1]);
    pz = (int) floor(runtime->player.position[2]);
    /* Copy bounded local display trial counts and reservoir-selected
     * environmental positions for Client playback. */
    for (i = 0; i < 1000; ++i) {
        x = px + (int) server_audio_random(runtime, 16) - (int) server_audio_random(runtime, 16);
        y = py + (int) server_audio_random(runtime, 16) - (int) server_audio_random(runtime, 16);
        z = pz + (int) server_audio_random(runtime, 16) - (int) server_audio_random(runtime, 16);
        if (y < 0 || y >= MAAGE_WORLD_HEIGHT) continue;
        state = maage_block_registry_state(registry, maage_world_get(runtime->world, x, y, z));
        if (!state) continue;
        if (state->fluid_kind == MAAGE_FLUID_WATER && state->fluid_level > 0 && state->fluid_level < 8) {
            if (server_audio_random(runtime, ++water_count) == 0) {
                audio->flowing_water_available = 1;
                audio->water_position[0] = x + 0.5;
                audio->water_position[1] = y + 0.5;
                audio->water_position[2] = z + 0.5;
            }
        } else if (state->fluid_kind == MAAGE_FLUID_LAVA) {
            above = maage_block_registry_state(registry, maage_world_get(runtime->world, x, y + 1, z));
            if (above && above->block_id == MAAGE_BLOCK_AIR && server_audio_random(runtime, ++lava_count) == 0) {
                audio->lava_available = 1;
                audio->lava_position[0] = x;
                audio->lava_position[1] = y;
                audio->lava_position[2] = z;
            }
        }
    }
    audio->water_samples = (unsigned int) water_count;
    audio->lava_samples = (unsigned int) lava_count;
    maage_world_environment(runtime->world, &environment);
    rain = environment.rain_intensity;
    samples = (int) (100.0 * rain * rain);
    memset(&local, 0, sizeof(local));
    (void) maage_world_observe(runtime->world, px, py, pz, &local);
    /* Sample real precipitation surfaces,
     * reject snow/desert/lava/air, and reservoir-select one impact. */
    for (i = 0; i < samples; ++i) {
        x = px + (int) server_audio_random(runtime, 10) - (int) server_audio_random(runtime, 10);
        z = pz + (int) server_audio_random(runtime, 10) - (int) server_audio_random(runtime, 10);
        if (!maage_world_observe(runtime->world, x, py, z, &observation)) continue;
        y = observation.precipitation_height;
        if (y < 1 || y >= MAAGE_WORLD_HEIGHT || y < py - 10 || y > py + 10) continue;
        if (!maage_world_observe(runtime->world, x, y, z, &observation) || observation.rainfall <= 0.0 || observation.temperature < 0.15) continue;
        state = maage_block_registry_state(registry, maage_world_get(runtime->world, x, y - 1, z));
        block = state ? maage_block_registry_block(registry, state->block_id) : NULL;
        if (!block || state->block_id == MAAGE_BLOCK_AIR || state->fluid_kind == MAAGE_FLUID_LAVA) continue;
        if (server_audio_random(runtime, ++rain_count) != 0) continue;
        audio->rain_available = 1;
        audio->rain_position[0] = x + server_audio_random(runtime, 65536) / 65536.0;
        audio->rain_position[1] = y + 0.1 - block->selection_shape.minimum[1];
        audio->rain_position[2] = z + server_audio_random(runtime, 65536) / 65536.0;
    }
    audio->rain_above = audio->rain_available && audio->rain_position[1] > runtime->player.position[1] + 1.0 && local.precipitation_height > py;
    /* Cave ambience requires real dark air, 2..8 blocks from the
     * player, and a 6000..17999 tick cooldown. Only nearby columns can pass
     * that distance predicate, so no scan of all loaded World columns. */
    if (runtime->cave_ticks) {
        --runtime->cave_ticks;
        return;
    }
    for (cx = -1; cx <= 1; ++cx)
        for (cz = -1; cz <= 1; ++cz) {
            x = (maage_world_chunk_coordinate(px) + cx) * 16 + (int) server_audio_random(runtime, 16);
            y = (int) server_audio_random(runtime, 256);
            z = (maage_world_chunk_coordinate(pz) + cz) * 16 + (int) server_audio_random(runtime, 16);
            position[0] = x + 0.5;
            position[1] = y + 0.5;
            position[2] = z + 0.5;
            dx = position[0] - runtime->player.position[0];
            dy = position[1] - runtime->player.position[1];
            dz = position[2] - runtime->player.position[2];
            if (dx * dx + dy * dy + dz * dz <= 4.0 || dx * dx + dy * dy + dz * dz > 64.0) continue;
            if (maage_world_get(runtime->world, x, y, z) != MAAGE_STATE_AIR ||
                !maage_world_observe(runtime->world, x, y, z, &observation) ||
                !observation.light_available || (observation.light >> 4) != 0 ||
                (observation.light & 15) > server_audio_random(runtime, 8)) continue;
            if (!maage_status_failed(maage_gameplay_build_sound(&runtime->game, &sound, MAAGE_SOUND_CAVE_AMBIENT, MAAGE_MATERIAL_DEFAULT, position, 1.0, 0.8 + server_audio_random(runtime, 65536) / 65536.0 * 0.2))) {
                sound.tick = runtime->tick + 1UL;
                server_emit_sound(runtime, &sound);
            }
            runtime->cave_ticks = 6000UL + server_audio_random(runtime, 12000);
            return;
        }
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
    result = maage_crafting_close(&runtime->game.crafting, &runtime->game.inventory, maage_content_items(runtime->content), &runtime->player, runtime->world);
    runtime->publication_dirty = 1;
    return result;
}
static void server_service_container(maage_server_runtime *runtime) {
    maage_crafting_container *container = &runtime->game.crafting;
    maage_status result;
    if (!container->closing && (!container->kind || (runtime->game.health && maage_crafting_accessible(container, &runtime->player, runtime->world)))) return;
    result = server_close_container(runtime);
    if (maage_status_failed(result) && result != runtime->crafting_failure) server_status_event(runtime, result, NULL);
    runtime->crafting_failure = result;
}

static void server_predictions(maage_server_runtime *runtime) {
    unsigned int i, n;
    int dx, dz, confirmed;
    maage_status result;
    for (i = 0; i < 32; ++i) {
        maage_session_event *event = &runtime->predictions[i].event;
        maage_world_target *target = &runtime->predictions[i].target;
        if (!runtime->predictions[i].active) continue;
        if (!runtime->predictions[i].resolved) {
            confirmed = runtime->game.blocks_broken > runtime->predictions[i].counter &&
                        runtime->game.last_break.x == target->x && runtime->game.last_break.y == target->y &&
                        runtime->game.last_break.z == target->z && runtime->game.last_break.state_id == target->state_id;
            if (confirmed) {
                event->accepted = 1;
                event->status = server_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_MUTATE);
                for (dz = -1; dz <= 1; ++dz)
                    for (dx = -1; dx <= 1; ++dx) {
                        n = (unsigned int) (dx + 1 + 3 * (dz + 1));
                        event->column_revisions[n] = maage_world_column_revision(runtime->world, maage_world_chunk_coordinate(target->x + dx), maage_world_chunk_coordinate(target->z + dz));
                    }
                runtime->predictions[i].resolved = 1;
            } else if (!runtime->game.health || runtime->paused || runtime->game.crafting.kind || runtime->game.crafting.closing || (runtime->game.ticks != runtime->predictions[i].admitted_tick && (!runtime->game.attack_held || !runtime->game.has_target || runtime->game.target.x != target->x || runtime->game.target.y != target->y || runtime->game.target.z != target->z || maage_world_get(runtime->world, target->x, target->y, target->z) != target->state_id))) {
                event->status = maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_MUTATE, MAAGE_DETAIL_STALE);
                runtime->predictions[i].resolved = 1;
            }
        }
        if (!runtime->predictions[i].resolved || !runtime->config.session) continue;
        if (!event->sequence) {
            event->protocol_version = MAAGE_SESSION_PROTOCOL_VERSION;
            event->kind = MAAGE_EVENT_BLOCK_ACTION;
            event->session_epoch = event->world_epoch = 1;
            event->tick = runtime->game.ticks;
            event->sequence = ++runtime->event_sequence;
            event->emitted_at = maage_time_monotonic_seconds();
        }
        result = maage_local_session_publish_event(runtime->config.session, event);
        if (maage_status_code(result) == MAAGE_STATUS_BUFFER_OVERFLOW) break;
        if (maage_status_failed(result) && maage_status_code(result) != MAAGE_STATUS_INVALID_STATE) {
            server_capture_failure(runtime, result, NULL);
            break;
        }
        runtime->predictions[i].active = 0;
    }
}
static maage_status server_predict_request(maage_server_runtime *runtime, const maage_session_command *command) {
    maage_world_target *target;
    unsigned int i;
    maage_status result;
    if (!command->prediction || command->prediction <= runtime->last_prediction || command->prediction > 4294967295UL) return server_reject(runtime, MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_VALIDATE);
    runtime->last_prediction = command->prediction;
    for (i = 0; i < 32 && runtime->predictions[i].active; ++i) {
    }
    if (i == 32) return server_reject(runtime, MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_OPERATION_SUBMIT);
    memset(&runtime->predictions[i], 0, sizeof(runtime->predictions[i]));
    runtime->predictions[i].active = 1;
    runtime->predictions[i].target = command->predicted_target;
    runtime->predictions[i].counter = runtime->game.blocks_broken;
    runtime->predictions[i].admitted_tick = runtime->game.ticks;
    runtime->predictions[i].event.prediction = command->prediction;
    target = &runtime->predictions[i].target;
    runtime->predictions[i].event.block.x = target->x;
    runtime->predictions[i].event.block.y = target->y;
    runtime->predictions[i].event.block.z = target->z;
    /* A legitimate preview may trail the owner by one transport cycle. The
     * recorded owner mutation is sufficient confirmation even after retargeting. */
    if (runtime->game.blocks_broken && runtime->game.last_break.x == target->x &&
        runtime->game.last_break.y == target->y && runtime->game.last_break.z == target->z &&
        runtime->game.last_break.state_id == target->state_id &&
        maage_world_get(runtime->world, target->x, target->y, target->z) == MAAGE_STATE_AIR) {
        runtime->predictions[i].counter = runtime->game.blocks_broken - 1;
        return server_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_SUBMIT);
    }
    result = maage_gameplay_target(&runtime->game, &runtime->player, runtime->world, runtime->yaw, runtime->pitch);
    if (!maage_status_is_success(result) || !runtime->game.has_target || !runtime->game.health ||
        runtime->paused || runtime->game.crafting.kind || runtime->game.crafting.closing ||
        !(runtime->sampled.attack || runtime->pending_attack) ||
        runtime->game.target.x != target->x || runtime->game.target.y != target->y ||
        runtime->game.target.z != target->z || runtime->game.target.state_id != target->state_id) {
        runtime->predictions[i].resolved = 1;
        runtime->predictions[i].event.status = server_status(MAAGE_STATUS_ACCESS_DENIED, MAAGE_OPERATION_VALIDATE);
    }
    return server_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_SUBMIT);
}

static maage_status server_apply_command(maage_server_runtime *runtime, const maage_session_command *command) {
    maage_status result;
    const maage_item_registry *items = maage_content_items(runtime->content);
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
        /* jump_pressed is an edge, unlike held movement.jump/attack/use.
         * Only the pending latch may survive to the next simulation step. */
        runtime->sampled.jump_pressed = runtime->sampled.forward_pressed = 0;
        runtime->sampled.attack_pressed = runtime->sampled.use_pressed = 0;
        runtime->yaw = command->intent.movement.yaw;
        runtime->pitch = command->intent.pitch;
        return server_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_UPDATE);
    case MAAGE_CMD_BREAK_PREVIEW:
        return server_predict_request(runtime, command);
    case MAAGE_CMD_HOTBAR:
        if (command->hotbar_slot >= MAAGE_HOTBAR_SLOTS) return server_reject(runtime, MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_SELECT);
        runtime->game.inventory.selected = command->hotbar_slot;
        return server_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_SELECT);
    case MAAGE_CMD_CONTAINER_OPEN:
        if (!runtime->game.health || runtime->game.crafting.kind || runtime->game.crafting.closing) return server_reject(runtime, MAAGE_STATUS_INVALID_STATE, MAAGE_OPERATION_OPEN);
        result = maage_crafting_open(&runtime->game.crafting, MAAGE_CONTAINER_PLAYER, 0, 0, 0);
        runtime->publication_dirty = 1;
        break;
    case MAAGE_CMD_CONTAINER_CLOSE:
    case MAAGE_CMD_CONTAINER_SLOT:
        if (!command->container_identity || command->container_identity != runtime->game.crafting.identity || !runtime->game.crafting.kind) return server_reject(runtime, MAAGE_STATUS_INVALID_STATE, MAAGE_OPERATION_VALIDATE);
        if (command->kind == MAAGE_CMD_CONTAINER_CLOSE) result = server_close_container(runtime);
        else {
            if (!runtime->game.health || !maage_crafting_accessible(&runtime->game.crafting, &runtime->player, runtime->world)) {
                server_service_container(runtime);
                return server_reject(runtime, MAAGE_STATUS_ACCESS_DENIED, MAAGE_OPERATION_MUTATE);
            }
            result = maage_crafting_click(&runtime->game.crafting, &runtime->game.inventory, items, maage_content_recipes(runtime->content), command->container_revision, command->slot_group, command->container_slot, command->slot_action);
        }
        runtime->publication_dirty = 1;
        break;
    case MAAGE_CMD_DROP:
        if (runtime->game.crafting.closing || (command->inventory_slot == MAAGE_INVENTORY_SLOTS && (!runtime->game.crafting.kind || command->container_identity != runtime->game.crafting.identity))) return server_reject(runtime, MAAGE_STATUS_INVALID_STATE, MAAGE_OPERATION_MUTATE);
        result = maage_gameplay_drop(&runtime->game, &runtime->player, runtime->world, items, runtime->yaw, runtime->pitch, command->inventory_slot, command->secondary_click);
        if (!maage_status_failed(result) && runtime->game.crafting.kind) ++runtime->game.crafting.revision;
        runtime->publication_dirty = 1;
        break;
    case MAAGE_CMD_CREATIVE_PICK:
        if (runtime->game.mode != MAAGE_GAME_MODE_CREATIVE || !runtime->game.health ||
            runtime->game.crafting.kind != MAAGE_CONTAINER_PLAYER || runtime->game.crafting.closing ||
            command->container_identity != runtime->game.crafting.identity)
            return server_reject(runtime, MAAGE_STATUS_ACCESS_DENIED, MAAGE_OPERATION_MUTATE);
        result = maage_inventory_creative(&runtime->game.inventory, items, command->item_id, command->item_metadata);
        if (!maage_status_failed(result)) ++runtime->game.crafting.revision;
        runtime->publication_dirty = 1;
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
        /* World currently freezes the streaming radius at create; do not
         * claim a changed radius took effect until World exposes its setter. */
        if (command->render_radius != runtime->config.render_distance) return server_reject(runtime, MAAGE_STATUS_NOT_IMPLEMENTED, MAAGE_OPERATION_SET);
        result = server_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_SET);
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
static maage_status server_take_commands(maage_server_runtime *runtime) {
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
        (void) server_apply_command(runtime, &command); /* rejected action is not fatal */
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

static void server_publish_snapshot(maage_server_runtime *runtime, double now) {
    maage_session_snapshot snapshot;
    int cell_x, cell_y, cell_z;
    if (!runtime->config.session || !runtime->world || !runtime->game_ready) return;
    memset(&snapshot, 0, sizeof(snapshot));
    snapshot.protocol_version = MAAGE_SESSION_PROTOCOL_VERSION;
    snapshot.session_epoch = snapshot.world_epoch = 1UL;
    snapshot.tick = runtime->tick;
    snapshot.paused = runtime->paused;
    snapshot.sequence = ++runtime->publication_sequence;
    snapshot.applied_command_sequence = runtime->last_command_sequence;
    snapshot.player.player = runtime->player;
    snapshot.player.yaw = runtime->yaw;
    snapshot.player.pitch = runtime->pitch;
    snapshot.player.inventory = runtime->game.inventory;
    snapshot.player.crafting = runtime->game.crafting;
    runtime->publication_dirty = 0;
    snapshot.player.game_mode = runtime->game.mode;
    snapshot.player.health = runtime->game.health;
    snapshot.player.hunger = runtime->game.hunger;
    snapshot.player.saturation = runtime->game.saturation;
    snapshot.player.flying = runtime->game.flying;
    snapshot.player.sneaking = runtime->sneaking;
    snapshot.player.has_target = runtime->game.has_target;
    snapshot.player.breaking_active = runtime->game.breaking_active;
    snapshot.player.reach = runtime->game.reach > 0 ? runtime->game.reach : (runtime->game.mode == MAAGE_GAME_MODE_CREATIVE ? 5.0 : 4.5);
    snapshot.player.break_delay = runtime->game.break_delay;
    snapshot.player.attack_miss_delay = runtime->game.attack_miss_delay;
    {
        const maage_block_state_definition *eye_state = maage_block_registry_state(maage_content_blocks(runtime->content), maage_world_get(runtime->world, (int) floor(runtime->player.position[0]), (int) floor(runtime->player.position[1] + MAAGE_PLAYER_EYE_HEIGHT), (int) floor(runtime->player.position[2])));
        snapshot.player.submerged = eye_state && eye_state->fluid_kind == MAAGE_FLUID_WATER;
    }
    snapshot.player.target = runtime->game.target;
    snapshot.player.breaking = runtime->game.breaking;
    snapshot.player.break_progress = runtime->game.break_progress;
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
    snapshot.audio_observation = runtime->audio_observation;
    {
        maage_status result = maage_world_sample_camera(runtime->world, cell_x, cell_y, cell_z, &runtime->collision.sample);
        if (maage_status_failed(result)) server_status_event(runtime, result, NULL);
        else if (maage_status_code(result) == MAAGE_STATUS_SUCCESS) {
            runtime->collision.protocol_version = MAAGE_SESSION_PROTOCOL_VERSION;
            runtime->collision.session_epoch = runtime->collision.world_epoch = 1UL;
            ++runtime->collision.sequence;
            result = maage_local_session_publish_collision(runtime->config.session, &runtime->collision);
            if (maage_status_failed(result)) server_status_event(runtime, result, NULL);
        }
    }
    (void) maage_local_session_publish_snapshot(runtime->config.session, &snapshot);
    {
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
        if (visible) view.entities = (maage_entity *) malloc(visible * sizeof(*view.entities));
        if (visible && !view.entities) {
            result = server_status(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_SNAPSHOT);
            if (runtime->entity_publication_failure != result) server_status_event(runtime, result, NULL);
            runtime->entity_publication_failure = result;
        } else {
            runtime->entity_publication_failure = 0;
            for (i = 0; i < maage_entities_count(entities); ++i) {
                entity = maage_entities_at(entities, i);
                if (server_entity_visible(runtime, entity)) view.entities[view.count++] = *entity;
            }
            view.protocol_version = MAAGE_SESSION_PROTOCOL_VERSION;
            view.session_epoch = snapshot.session_epoch;
            view.world_epoch = snapshot.world_epoch;
            view.tick = snapshot.tick;
            view.sequence = snapshot.sequence;
            result = maage_local_session_offer_entities(runtime->config.session, &view);
            if (maage_status_failed(result)) server_status_event(runtime, result, NULL);
            free(view.entities);
        }
    }
}

static void server_offer_columns(maage_server_runtime *runtime) {
    unsigned int n;
    maage_column_update *update;
    maage_local_column_update outgoing;
    maage_status result;
    if (!runtime->world || !runtime->config.session) return;
    for (n = 0; n < MAAGE_SERVER_COLUMN_BUDGET; ++n) {
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
        outgoing.column = update;
        result = maage_local_session_offer_column(runtime->config.session, &outgoing);
        if (maage_status_code(result) == MAAGE_STATUS_SUCCESS) result = maage_world_ack_column(runtime->world, outgoing.x, outgoing.z, outgoing.revision);
        maage_column_update_destroy(outgoing.column);
        if (maage_status_failed(result)) {
            if (maage_status_code(result) != MAAGE_STATUS_BUFFER_OVERFLOW && maage_status_code(result) != MAAGE_STATUS_INVALID_STATE) server_capture_failure(runtime, result, NULL);
            return; /* Retain the unsent revision and retry after backpressure. */
        }
    }
}

static maage_status server_open_world(maage_server_runtime *runtime, maage_status_trace *trace) {
    maage_world_config world_config;
    const maage_item_registry *items;
    maage_status result;
    char error[256];
    if (!runtime->config.content) {
        result = maage_content_create(NULL, &runtime->owned_content, trace);
        if (maage_status_failed(result)) return result;
    }
    runtime->content = runtime->config.content ? runtime->config.content : runtime->owned_content;
    if (!maage_block_registry_is_finalized(maage_content_blocks(runtime->content)) || !maage_item_registry_is_finalized(maage_content_items(runtime->content))) return MAAGE_STATUS_TRACE(trace, server_status(MAAGE_STATUS_INVALID_STATE, MAAGE_OPERATION_INITIALIZE), MAAGE_CONTEXT_SERVER, MAAGE_OPERATION_INITIALIZE);
    items = maage_content_items(runtime->content);
    memset(&runtime->record, 0, sizeof(runtime->record));
    error[0] = 0;
    result = maage_world_store_open(runtime->config.instance_root, runtime->config.world_id, runtime->config.create_world, runtime->config.world_name, runtime->config.has_seed ? &runtime->config.seed : NULL, runtime->config.generation_policy, items, &runtime->store, &runtime->record, error, trace);
    if (maage_status_failed(result)) {
        maage_log_trace_status(MAAGE_LOG_ERROR, "maage.server.world", result, trace, "%s", error);
        return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_SERVER, MAAGE_OPERATION_OPEN);
    }
    result = maage_world_config_init(&world_config);
    if (maage_status_failed(result)) return result;
    world_config.registry = maage_content_blocks(runtime->content);
    world_config.items = items;
    world_config.biomes = maage_content_biomes(runtime->content);
    world_config.seed = runtime->record.seed;
    runtime->audio_random = (runtime->record.seed.low ^ runtime->record.seed.high ^ runtime->record.environment.ticks_low ^ 0x9e3779b9UL) & 0xffffffffUL;
    runtime->cave_ticks = server_audio_random(runtime, 12000);
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
        if (runtime->record.new_world && runtime->config.initial_game_mode == MAAGE_GAME_MODE_SURVIVAL) memset(&runtime->game.inventory, 0, sizeof(runtime->game.inventory));
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

static maage_status server_tick(maage_server_runtime *runtime, maage_status_trace *trace) {
    maage_gameplay_intent intent = runtime->sampled;
    maage_sound_event sounds[32];
    size_t sound_count = 0, i;
    maage_status result;
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
    result = maage_gameplay_tick(&runtime->game, &runtime->player, runtime->world, maage_content_items(runtime->content), &intent, sounds, sizeof(sounds) / sizeof(sounds[0]), &sound_count, trace);
    runtime->sneaking = intent.movement.descend && !runtime->game.flying && runtime->game.health != 0;
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
    {
        unsigned int collected = 0;
        maage_sound_event pickup;
        maage_entity_pickup pickups[32];
        size_t pickup_count = 0;
        result = maage_entities_tick(maage_world_entities(runtime->world), runtime->world, maage_content_items(runtime->content), &runtime->player, &runtime->game.inventory, runtime->game.health != 0, &collected, pickups, 32, &pickup_count);
        if (maage_status_failed(result)) return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_SERVER, MAAGE_OPERATION_TICK);
        if (pickup_count) {
            result = maage_gameplay_build_sound(&runtime->game, &pickup, MAAGE_SOUND_ITEM_PICKUP, MAAGE_MATERIAL_DEFAULT, runtime->player.position, 0.2, 1.6 + server_audio_random(runtime, 800) / 1000.0);
            if (!maage_status_failed(result)) {
                pickup.tick = runtime->tick + 1;
                server_emit_effect(runtime, &pickup, pickups, pickup_count);
            }
        }
    }
    server_service_container(runtime);
    server_predictions(runtime);
    result = maage_world_tick(runtime->world);
    if (maage_status_failed(result)) return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_SERVER, MAAGE_OPERATION_TICK);
    server_audio_observe(runtime);
    ++runtime->tick;
    return server_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_TICK);
}

static void server_thread_entry(void *context) {
    maage_server_runtime *runtime = (maage_server_runtime *) context;
    maage_status result, waited;
    maage_status_trace trace;
    double now, deadline, remaining, last_published;
    unsigned int caught_up;
    unsigned long sleep_ms, last_published_tick = 0UL;
    int center_x, center_z, spawn_ready, last_published_paused = -1;
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
    while (!server_stop_requested(runtime)) {
        /* A stream pump runs even when paused or awaiting the initial spawn
         * column; job completions precede all collision queries each step. */
        maage_status_trace_init(&trace);
        server_service_container(runtime);
        result = server_take_commands(runtime);
        if (maage_status_failed(result)) {
            server_capture_failure(runtime, result, NULL);
            break;
        }
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
        server_offer_columns(runtime);
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
                result = server_tick(runtime, &trace);
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
        server_service_container(runtime);
        server_predictions(runtime);
        if (runtime->publication_dirty || last_published < 0.0 || now - last_published >= 0.1 || runtime->tick != last_published_tick || runtime->paused != last_published_paused) {
            server_publish_snapshot(runtime, now);
            last_published = now;
            last_published_tick = runtime->tick;
            last_published_paused = runtime->paused;
        }
        server_publish_status(runtime);
        remaining = deadline - now;
        if (remaining < 0.001) remaining = 0.001;
        sleep_ms = (unsigned long) (remaining * 1000.0);
        if (!sleep_ms) sleep_ms = 1UL;
        if (sleep_ms > MAAGE_SERVER_WAIT_MS) sleep_ms = MAAGE_SERVER_WAIT_MS;
        maage_mutex_lock(runtime->mutex);
        if (!runtime->stop) {
            waited = maage_condition_timed_wait(runtime->wake, runtime->mutex, sleep_ms);
            if (maage_status_failed(waited) && maage_status_code(waited) != MAAGE_STATUS_TIMEOUT) server_capture_failure(runtime, waited, NULL);
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
        (config->execution_mode == MAAGE_SERVER_INTEGRATED && !config->session) ||
        (config->generation_policy &&
         !maage_generation_policy_valid(config->generation_policy)))
        return MAAGE_STATUS_TRACE(trace, server_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_CREATE), MAAGE_CONTEXT_SERVER, MAAGE_OPERATION_CREATE);
    runtime = (maage_server_runtime *) calloc(1, sizeof(*runtime));
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
