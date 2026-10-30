#include "maage/local_session.h"
#include "maage/thread.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

/* These are nonblocking mailboxes. One mutex guards only bounded copies,
 * counters and owned state-packet pointer swaps; no World owner lock is ever taken. */
struct maage_local_session {
    maage_mutex *mutex;
    maage_condition *changed;
    maage_session_command *commands;
    maage_session_event *events;
    maage_local_column_update *columns;
    size_t command_capacity, event_capacity, column_capacity;
    size_t command_head, command_count;
    size_t event_head, event_count;
    size_t column_count;
    unsigned column_turn;
    maage_session_snapshot snapshot;
    int has_snapshot;
    maage_session_entities entities;
    int has_entities;
    int closed;
};

static maage_status session_status(unsigned char code, unsigned char operation) {
    return maage_status_make(code, MAAGE_CONTEXT_SERVER, operation, 0);
}

static maage_status session_argument(unsigned char operation, unsigned char argument) {
    return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_SERVER, operation, argument);
}

static size_t session_next(size_t index, size_t capacity) {
    ++index;
    return index == capacity ? 0 : index;
}

static int session_capacity_valid(unsigned int capacity, size_t element_size) {
    return capacity != 0U && (size_t) capacity <= ((size_t) - 1) / element_size;
}

static int session_command_valid(const maage_session_command *command) {
    if (!command->sequence || command->protocol_version != MAAGE_SESSION_PROTOCOL_VERSION || command->kind < MAAGE_CMD_INPUT || command->kind > MAAGE_CMD_PLACE_PREVIEW) return 0;
    switch (command->kind) {
    case MAAGE_CMD_HOTBAR:
        return command->hotbar_slot < MAAGE_HOTBAR_SLOTS;
    case MAAGE_CMD_BREAK_PREVIEW:
    case MAAGE_CMD_PLACE_PREVIEW:
        return command->prediction != 0 && command->prediction <= 4294967295UL;
    case MAAGE_CMD_CONTAINER_OPEN:
        return 1;
    case MAAGE_CMD_CONTAINER_CLOSE:
        return command->container_identity != 0 && command->container_identity <= 4294967295UL;
    case MAAGE_CMD_CONTAINER_SLOT:
        return command->container_identity != 0 && command->container_identity <= 4294967295UL &&
               command->container_revision <= 4294967295UL &&
               command->slot_action >= MAAGE_SLOT_PRIMARY && command->slot_action <= MAAGE_SLOT_QUICK_MOVE &&
               ((command->slot_group == MAAGE_SLOT_INVENTORY && command->container_slot < MAAGE_INVENTORY_SLOTS) ||
                (command->slot_group == MAAGE_SLOT_CRAFT_INPUT && command->container_slot < MAAGE_CRAFTING_CELLS) ||
                (command->slot_group == MAAGE_SLOT_CRAFT_RESULT && command->container_slot == 0));
    case MAAGE_CMD_DROP:
        return command->inventory_slot <= MAAGE_INVENTORY_SLOTS && (command->secondary_click == 0 || command->secondary_click == 1);
    case MAAGE_CMD_GAME_MODE:
        return command->game_mode == MAAGE_GAME_MODE_SURVIVAL || command->game_mode == MAAGE_GAME_MODE_CREATIVE;
    case MAAGE_CMD_PAUSE:
        return command->paused == 0 || command->paused == 1;
    case MAAGE_CMD_VIEW:
        return command->render_radius > 0 && command->render_radius <= MAAGE_WORLD_MAX_RADIUS;
    case MAAGE_CMD_INPUT:
    case MAAGE_CMD_CREATIVE_PICK:
    case MAAGE_CMD_RESPAWN:
    case MAAGE_CMD_DISCONNECT:
        return 1;
    default:
        return 0;
    }
}

static int session_event_valid(const maage_session_event *event) {
    if (event->protocol_version != MAAGE_SESSION_PROTOCOL_VERSION ||
        event->kind < MAAGE_EVENT_SOUND || event->kind > MAAGE_EVENT_BLOCK_ACTION || event->kind == 3U ||
        (event->has_trace != 0 && event->has_trace != 1)) return 0;
    if (event->kind == MAAGE_EVENT_PICKUP && (!event->pickup_count || event->pickup_count > 32)) return 0;
    if (event->kind == MAAGE_EVENT_SOUND || event->kind == MAAGE_EVENT_PICKUP)
        return event->sound.event_id >= MAAGE_SOUND_FOOTSTEP &&
               event->sound.event_id <= MAAGE_SOUND_MAX &&
               event->sound.material <= MAAGE_MATERIAL_MAX &&
               event->sequence != 0UL && event->sound.sequence == event->sequence &&
               event->sound.tick == event->tick;
    return 1;
}

/* Equality is a duplicate publication (NOOP); older publications cannot roll
 * the client backward. Session/world epochs permit counters to restart. */
static int session_snapshot_newer(const maage_session_snapshot *next, const maage_session_snapshot *old) {
    if (next->session_epoch != old->session_epoch) return next->session_epoch > old->session_epoch;
    if (next->world_epoch != old->world_epoch) return next->world_epoch > old->world_epoch;
    if (next->tick != old->tick) return next->tick > old->tick;
    return next->sequence > old->sequence;
}

maage_status maage_local_session_create(unsigned int command_capacity, unsigned int event_capacity, unsigned int column_capacity, maage_local_session **output) {
    maage_local_session *session;
    maage_status result;
    if (!output) return session_argument(MAAGE_OPERATION_CREATE, 3);
    *output = NULL;
    if (!session_capacity_valid(command_capacity, sizeof(maage_session_command))) return session_argument(MAAGE_OPERATION_CREATE, 0);
    if (!session_capacity_valid(event_capacity, sizeof(maage_session_event))) return session_argument(MAAGE_OPERATION_CREATE, 1);
    if (!session_capacity_valid(column_capacity, sizeof(maage_local_column_update))) return session_argument(MAAGE_OPERATION_CREATE, 2);
    session = (maage_local_session *) calloc(1, sizeof(*session));
    if (!session) return session_status(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_CREATE);
    session->command_capacity = (size_t) command_capacity;
    session->event_capacity = (size_t) event_capacity;
    session->column_capacity = (size_t) column_capacity;
    session->commands = (maage_session_command *) calloc(session->command_capacity, sizeof(*session->commands));
    session->events = (maage_session_event *) calloc(session->event_capacity, sizeof(*session->events));
    session->columns = (maage_local_column_update *) calloc(session->column_capacity, sizeof(*session->columns));
    if (!session->commands || !session->events || !session->columns) {
        free(session->columns);
        free(session->events);
        free(session->commands);
        free(session);
        return session_status(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_CREATE);
    }
    result = maage_mutex_create(&session->mutex);
    if (maage_status_failed(result)) {
        free(session->columns);
        free(session->events);
        free(session->commands);
        free(session);
        return result;
    }
    result = maage_condition_create(&session->changed);
    if (maage_status_failed(result)) {
        (void) maage_mutex_destroy(session->mutex);
        free(session->columns);
        free(session->events);
        free(session->commands);
        free(session);
        return result;
    }
    *output = session;
    return session_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_CREATE);
}

maage_status maage_local_session_send(maage_local_session *session, const maage_session_command *command) {
    size_t tail;
    maage_status result;
    if (!session) return session_argument(MAAGE_OPERATION_SUBMIT, 0);
    if (!command || !session_command_valid(command)) return session_argument(MAAGE_OPERATION_SUBMIT, 1);
    maage_mutex_lock(session->mutex);
    if (session->closed) result = session_status(MAAGE_STATUS_INVALID_STATE, MAAGE_OPERATION_SUBMIT);
    else if (session->command_count == session->command_capacity) result = session_status(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_OPERATION_SUBMIT);
    else {
        tail = (session->command_head + session->command_count) % session->command_capacity;
        session->commands[tail] = *command;
        ++session->command_count;
        maage_condition_signal(session->changed);
        result = session_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_SUBMIT);
    }
    maage_mutex_unlock(session->mutex);
    return result;
}

maage_status maage_local_session_take_command(maage_local_session *session, maage_session_command *command) {
    maage_status result;
    if (!session) return session_argument(MAAGE_OPERATION_READ, 0);
    if (!command) return session_argument(MAAGE_OPERATION_READ, 1);
    maage_mutex_lock(session->mutex);
    if (session->command_count) {
        *command = session->commands[session->command_head];
        session->command_head = session_next(session->command_head, session->command_capacity);
        --session->command_count;
        maage_condition_signal(session->changed);
        result = session_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_READ);
    } else
        result = session_status(session->closed ? MAAGE_STATUS_INVALID_STATE : MAAGE_STATUS_NOOP, MAAGE_OPERATION_READ);
    maage_mutex_unlock(session->mutex);
    return result;
}

maage_status maage_local_session_publish_snapshot(maage_local_session *session, const maage_session_snapshot *snapshot) {
    maage_status result;
    if (!session) return session_argument(MAAGE_OPERATION_SNAPSHOT, 0);
    if (!snapshot || snapshot->protocol_version != MAAGE_SESSION_PROTOCOL_VERSION ||
        (snapshot->paused != 0 && snapshot->paused != 1) ||
        (snapshot->has_observation != 0 && snapshot->has_observation != 1))
        return session_argument(MAAGE_OPERATION_SNAPSHOT, 1);
    maage_mutex_lock(session->mutex);
    if (session->closed) result = session_status(MAAGE_STATUS_INVALID_STATE, MAAGE_OPERATION_SNAPSHOT);
    else if (session->has_snapshot && !session_snapshot_newer(snapshot, &session->snapshot)) result = session_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_SNAPSHOT);
    else {
        session->snapshot = *snapshot;
        session->has_snapshot = 1;
        maage_condition_signal(session->changed);
        result = session_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_SNAPSHOT);
    }
    maage_mutex_unlock(session->mutex);
    return result;
}

maage_status maage_local_session_read_snapshot(maage_local_session *session, maage_session_snapshot *snapshot) {
    maage_status result;
    if (!session) return session_argument(MAAGE_OPERATION_READ, 0);
    if (!snapshot) return session_argument(MAAGE_OPERATION_READ, 1);
    maage_mutex_lock(session->mutex);
    if (session->has_snapshot) {
        *snapshot = session->snapshot;
        result = session_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_READ);
    } else
        result = session_status(session->closed ? MAAGE_STATUS_INVALID_STATE : MAAGE_STATUS_NOOP, MAAGE_OPERATION_READ);
    maage_mutex_unlock(session->mutex);
    return result;
}

maage_status maage_local_session_publish_event(maage_local_session *session, const maage_session_event *event) {
    size_t tail;
    maage_status result;
    if (!session) return session_argument(MAAGE_OPERATION_SUBMIT, 0);
    if (!event || !session_event_valid(event)) return session_argument(MAAGE_OPERATION_SUBMIT, 1);
    maage_mutex_lock(session->mutex);
    if (session->closed) result = session_status(MAAGE_STATUS_INVALID_STATE, MAAGE_OPERATION_SUBMIT);
    else if (session->event_count == session->event_capacity) result = session_status(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_OPERATION_SUBMIT);
    else {
        tail = (session->event_head + session->event_count) % session->event_capacity;
        session->events[tail] = *event;
        if (!event->has_trace) maage_status_trace_init(&session->events[tail].trace);
        ++session->event_count;
        maage_condition_signal(session->changed);
        result = session_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_SUBMIT);
    }
    maage_mutex_unlock(session->mutex);
    return result;
}

maage_status maage_local_session_take_event(maage_local_session *session, maage_session_event *event) {
    maage_status result;
    if (!session) return session_argument(MAAGE_OPERATION_READ, 0);
    if (!event) return session_argument(MAAGE_OPERATION_READ, 1);
    maage_mutex_lock(session->mutex);
    if (session->event_count) {
        *event = session->events[session->event_head];
        session->event_head = session_next(session->event_head, session->event_capacity);
        --session->event_count;
        maage_condition_signal(session->changed);
        result = session_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_READ);
    } else
        result = session_status(session->closed ? MAAGE_STATUS_INVALID_STATE : MAAGE_STATUS_NOOP, MAAGE_OPERATION_READ);
    maage_mutex_unlock(session->mutex);
    return result;
}

maage_status maage_local_session_offer_column(maage_local_session *session, maage_local_column_update *update) {
    maage_status result;
    if (!session || !update || !update->column || !update->world_epoch || update->x != update->column->x || update->z != update->column->z || update->revision != update->column->revision) return session_argument(MAAGE_OPERATION_SUBMIT, 0);
    maage_mutex_lock(session->mutex);
    if (session->closed) result = session_status(MAAGE_STATUS_INVALID_STATE, MAAGE_OPERATION_SUBMIT);
    else if (session->column_count == session->column_capacity) result = session_status(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_OPERATION_SUBMIT);
    else {
        session->columns[session->column_count++] = *update;
        update->column = NULL;
        maage_condition_signal(session->changed);
        result = session_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_SUBMIT);
    }
    maage_mutex_unlock(session->mutex);
    return result;
}

maage_status maage_local_session_take_column(maage_local_session *session, int center_x, int center_z, maage_local_column_update *update) {
    maage_status result;
    size_t i, selected = 0;
    int dx, dz, distance, best = INT_MAX;
    if (!session) return session_argument(MAAGE_OPERATION_READ, 0);
    if (!update || center_x < -MAAGE_WORLD_MAX_RADIUS || center_x > MAAGE_WORLD_MAX_RADIUS || center_z < -MAAGE_WORLD_MAX_RADIUS || center_z > MAAGE_WORLD_MAX_RADIUS) return session_argument(MAAGE_OPERATION_READ, 1);
    maage_mutex_lock(session->mutex);
    if (session->column_count) {
        if (++session->column_turn % 8U)
            for (i = 0; i < session->column_count; ++i) {
                dx = abs(session->columns[i].x - center_x);
                dz = abs(session->columns[i].z - center_z);
                distance = dx > dz ? dx : dz;
                /* Strict comparison preserves the earliest packet for each
                 * coordinate and therefore every required delta baseline. */
                if (distance < best) {
                    selected = i;
                    best = distance;
                }
            }
        *update = session->columns[selected];
        --session->column_count;
        if (selected < session->column_count) memmove(session->columns + selected, session->columns + selected + 1, (session->column_count - selected) * sizeof(*session->columns));
        memset(&session->columns[session->column_count], 0, sizeof(*session->columns));
        maage_condition_signal(session->changed);
        result = session_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_READ);
    } else
        result = session_status(session->closed ? MAAGE_STATUS_INVALID_STATE : MAAGE_STATUS_NOOP, MAAGE_OPERATION_READ);
    maage_mutex_unlock(session->mutex);
    return result;
}

maage_status maage_local_session_close(maage_local_session *session) {
    maage_status result;
    if (!session) return session_argument(MAAGE_OPERATION_CLOSE, 0);
    maage_mutex_lock(session->mutex);
    if (session->closed) result = session_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_CLOSE);
    else {
        session->closed = 1;
        maage_condition_broadcast(session->changed);
        result = session_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_CLOSE);
    }
    maage_mutex_unlock(session->mutex);
    return result;
}

maage_status maage_local_session_destroy(maage_local_session *session) {
    size_t i;
    maage_status result;
    if (!session) return session_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_DESTROY);
    /* All users have left this object by contract; no destroy races. */
    result = maage_local_session_close(session);
    if (maage_status_failed(result)) return result;
    result = maage_condition_destroy(session->changed);
    if (maage_status_failed(result)) return result;
    session->changed = NULL;
    result = maage_mutex_destroy(session->mutex);
    if (maage_status_failed(result)) return result;
    session->mutex = NULL;
    for (i = 0; i < session->column_count; ++i)
        (void) maage_column_update_destroy(session->columns[i].column);
    free(session->entities.entities);
    free(session->columns);
    free(session->events);
    free(session->commands);
    free(session);
    return session_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_DESTROY);
}

maage_status maage_local_session_offer_entities(maage_local_session *session, maage_session_entities *view) {
    maage_entity *retired = NULL;
    maage_status result;
    if (!session || !view || view->protocol_version != MAAGE_SESSION_PROTOCOL_VERSION ||
        !view->session_epoch || !view->world_epoch || !view->sequence ||
        view->count > (size_t) - 1 / sizeof(*view->entities) || (view->count && !view->entities))
        return session_argument(MAAGE_OPERATION_SUBMIT, 1);
    maage_mutex_lock(session->mutex);
    if (session->closed) result = session_status(MAAGE_STATUS_INVALID_STATE, MAAGE_OPERATION_SUBMIT);
    else if (session->entities.sequence &&
             (view->session_epoch < session->entities.session_epoch ||
              (view->session_epoch == session->entities.session_epoch &&
               (view->world_epoch < session->entities.world_epoch ||
                (view->world_epoch == session->entities.world_epoch &&
                 (view->tick < session->entities.tick || view->sequence <= session->entities.sequence))))))
        result = session_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_SUBMIT);
    else {
        retired = session->entities.entities;
        session->entities = *view;
        view->entities = NULL;
        view->count = 0;
        session->has_entities = 1;
        result = session_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_SUBMIT);
    }
    maage_mutex_unlock(session->mutex);
    free(retired);
    return result;
}
maage_status maage_local_session_take_entities(maage_local_session *session, maage_session_entities *view) {
    maage_status result;
    if (!session || !view) return session_argument(MAAGE_OPERATION_READ, 1);
    maage_mutex_lock(session->mutex);
    if (session->has_entities) {
        *view = session->entities;
        session->entities.entities = NULL;
        session->has_entities = 0;
        result = session_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_READ);
    } else
        result = session_status(session->closed ? MAAGE_STATUS_INVALID_STATE : MAAGE_STATUS_NOOP, MAAGE_OPERATION_READ);
    maage_mutex_unlock(session->mutex);
    return result;
}

maage_status maage_local_session_get_backlog(maage_local_session *session, maage_local_session_backlog *backlog) {
    if (!session || !backlog) return session_argument(MAAGE_OPERATION_QUERY, 0);
    maage_mutex_lock(session->mutex);
    backlog->commands = session->command_count;
    backlog->events = session->event_count;
    backlog->columns = session->column_count;
    backlog->column_capacity = session->column_capacity;
    maage_mutex_unlock(session->mutex);
    return session_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_QUERY);
}
