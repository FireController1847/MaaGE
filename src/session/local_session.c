#include "maage/local_session.h"
#include "maage/thread.h"
#include <stdlib.h>
#include <string.h>

/* These are nonblocking mailboxes. One mutex guards only bounded copies,
 * counters and CPU-mesh pointer swaps; no World owner lock is ever taken. */
struct maage_local_session {
    maage_mutex *mutex;
    maage_condition *changed;
    maage_session_command *commands;
    maage_session_event *events;
    maage_local_mesh_update *meshes;
    size_t command_capacity, event_capacity, mesh_capacity;
    size_t command_head, command_count;
    size_t event_head, event_count;
    size_t mesh_count;
    unsigned long mesh_epoch;
    int has_mesh_epoch;
    maage_session_snapshot snapshot;
    int has_snapshot;
    int closed;
};

static maage_status session_status(unsigned char code, unsigned char operation)
{
    return maage_status_make(code, MAAGE_CONTEXT_SERVER, operation, 0);
}

static maage_status session_argument(unsigned char operation, unsigned char argument)
{
    return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT,
        MAAGE_CONTEXT_SERVER, operation, argument);
}

static size_t session_next(size_t index, size_t capacity)
{
    ++index;
    return index == capacity ? 0 : index;
}

static int session_capacity_valid(unsigned int capacity, size_t element_size)
{
    return capacity != 0U && (size_t)capacity <= ((size_t)-1) / element_size;
}

static int session_command_valid(const maage_session_command *command)
{
    if (!command->sequence || command->protocol_version != MAAGE_SESSION_PROTOCOL_VERSION ||
        command->kind < MAAGE_CMD_INPUT || command->kind > MAAGE_CMD_DISCONNECT)
        return 0;
    switch (command->kind) {
    case MAAGE_CMD_HOTBAR:
        return command->hotbar_slot < MAAGE_HOTBAR_SLOTS;
    case MAAGE_CMD_INVENTORY:
        return command->inventory_slot < MAAGE_INVENTORY_SLOTS &&
            (command->secondary_click == 0 || command->secondary_click == 1);
    case MAAGE_CMD_GAME_MODE:
        return command->game_mode == MAAGE_GAME_MODE_SURVIVAL ||
            command->game_mode == MAAGE_GAME_MODE_CREATIVE;
    case MAAGE_CMD_PAUSE:
        return command->paused == 0 || command->paused == 1;
    case MAAGE_CMD_VIEW:
        return command->render_radius > 0 &&
            command->render_radius <= MAAGE_WORLD_MAX_RADIUS &&
            command->leaves_mode <= MAAGE_LEAVES_FANCY;
    default:
        return 1;
    }
}

static int session_event_valid(const maage_session_event *event)
{
    if (event->protocol_version != MAAGE_SESSION_PROTOCOL_VERSION ||
        event->kind < MAAGE_EVENT_SOUND || event->kind > MAAGE_EVENT_DISCONNECTED ||
        (event->has_trace != 0 && event->has_trace != 1)) return 0;
    if (event->kind == MAAGE_EVENT_SOUND)
        return event->sound.event_id >= MAAGE_SOUND_FOOTSTEP &&
            event->sound.event_id <= MAAGE_SOUND_LAVA &&
            event->sound.material <= MAAGE_MATERIAL_FOLIAGE;
    return 1;
}

/* Equality is a duplicate publication (NOOP); older publications cannot roll
 * the client backward. Session/world epochs permit counters to restart. */
static int session_snapshot_newer(const maage_session_snapshot *next,
    const maage_session_snapshot *old)
{
    if (next->session_epoch != old->session_epoch)
        return next->session_epoch > old->session_epoch;
    if (next->world_epoch != old->world_epoch)
        return next->world_epoch > old->world_epoch;
    if (next->tick != old->tick) return next->tick > old->tick;
    return next->sequence > old->sequence;
}

maage_status maage_local_session_create(unsigned int command_capacity,
    unsigned int event_capacity, unsigned int mesh_capacity,
    maage_local_session **output)
{
    maage_local_session *session;
    maage_status result;
    if (!output) return session_argument(MAAGE_OPERATION_CREATE, 3);
    *output = NULL;
    if (!session_capacity_valid(command_capacity, sizeof(maage_session_command)))
        return session_argument(MAAGE_OPERATION_CREATE, 0);
    if (!session_capacity_valid(event_capacity, sizeof(maage_session_event)))
        return session_argument(MAAGE_OPERATION_CREATE, 1);
    if (!session_capacity_valid(mesh_capacity, sizeof(maage_local_mesh_update)))
        return session_argument(MAAGE_OPERATION_CREATE, 2);
    session = (maage_local_session *)calloc(1, sizeof(*session));
    if (!session) return session_status(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_CREATE);
    session->command_capacity = (size_t)command_capacity;
    session->event_capacity = (size_t)event_capacity;
    session->mesh_capacity = (size_t)mesh_capacity;
    session->commands = (maage_session_command *)calloc(session->command_capacity,
        sizeof(*session->commands));
    session->events = (maage_session_event *)calloc(session->event_capacity,
        sizeof(*session->events));
    session->meshes = (maage_local_mesh_update *)calloc(session->mesh_capacity,
        sizeof(*session->meshes));
    if (!session->commands || !session->events || !session->meshes) {
        free(session->meshes);
        free(session->events);
        free(session->commands);
        free(session);
        return session_status(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_CREATE);
    }
    result = maage_mutex_create(&session->mutex);
    if (maage_status_failed(result)) {
        free(session->meshes);
        free(session->events);
        free(session->commands);
        free(session);
        return result;
    }
    result = maage_condition_create(&session->changed);
    if (maage_status_failed(result)) {
        (void)maage_mutex_destroy(session->mutex);
        free(session->meshes);
        free(session->events);
        free(session->commands);
        free(session);
        return result;
    }
    *output = session;
    return session_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_CREATE);
}

maage_status maage_local_session_send(maage_local_session *session,
    const maage_session_command *command)
{
    size_t tail;
    maage_status result;
    if (!session) return session_argument(MAAGE_OPERATION_SUBMIT, 0);
    if (!command || !session_command_valid(command))
        return session_argument(MAAGE_OPERATION_SUBMIT, 1);
    maage_mutex_lock(session->mutex);
    if (session->closed)
        result = session_status(MAAGE_STATUS_INVALID_STATE, MAAGE_OPERATION_SUBMIT);
    else if (session->command_count == session->command_capacity)
        result = session_status(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_OPERATION_SUBMIT);
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

maage_status maage_local_session_take_command(maage_local_session *session,
    maage_session_command *command)
{
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
        result = session_status(session->closed ? MAAGE_STATUS_INVALID_STATE :
            MAAGE_STATUS_NOOP, MAAGE_OPERATION_READ);
    maage_mutex_unlock(session->mutex);
    return result;
}

maage_status maage_local_session_publish_snapshot(maage_local_session *session,
    const maage_session_snapshot *snapshot)
{
    maage_status result;
    if (!session) return session_argument(MAAGE_OPERATION_SNAPSHOT, 0);
    if (!snapshot || snapshot->protocol_version != MAAGE_SESSION_PROTOCOL_VERSION ||
        (snapshot->has_observation != 0 && snapshot->has_observation != 1))
        return session_argument(MAAGE_OPERATION_SNAPSHOT, 1);
    maage_mutex_lock(session->mutex);
    if (session->closed)
        result = session_status(MAAGE_STATUS_INVALID_STATE, MAAGE_OPERATION_SNAPSHOT);
    else if (session->has_snapshot && !session_snapshot_newer(snapshot, &session->snapshot))
        result = session_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_SNAPSHOT);
    else {
        session->snapshot = *snapshot;
        session->has_snapshot = 1;
        maage_condition_signal(session->changed);
        result = session_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_SNAPSHOT);
    }
    maage_mutex_unlock(session->mutex);
    return result;
}

maage_status maage_local_session_read_snapshot(maage_local_session *session,
    maage_session_snapshot *snapshot)
{
    maage_status result;
    if (!session) return session_argument(MAAGE_OPERATION_READ, 0);
    if (!snapshot) return session_argument(MAAGE_OPERATION_READ, 1);
    maage_mutex_lock(session->mutex);
    if (session->has_snapshot) {
        *snapshot = session->snapshot;
        result = session_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_READ);
    } else
        result = session_status(session->closed ? MAAGE_STATUS_INVALID_STATE :
            MAAGE_STATUS_NOOP, MAAGE_OPERATION_READ);
    maage_mutex_unlock(session->mutex);
    return result;
}

maage_status maage_local_session_publish_event(maage_local_session *session,
    const maage_session_event *event)
{
    size_t tail;
    maage_status result;
    if (!session) return session_argument(MAAGE_OPERATION_SUBMIT, 0);
    if (!event || !session_event_valid(event))
        return session_argument(MAAGE_OPERATION_SUBMIT, 1);
    maage_mutex_lock(session->mutex);
    if (session->closed)
        result = session_status(MAAGE_STATUS_INVALID_STATE, MAAGE_OPERATION_SUBMIT);
    else if (session->event_count == session->event_capacity)
        result = session_status(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_OPERATION_SUBMIT);
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

maage_status maage_local_session_take_event(maage_local_session *session,
    maage_session_event *event)
{
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
        result = session_status(session->closed ? MAAGE_STATUS_INVALID_STATE :
            MAAGE_STATUS_NOOP, MAAGE_OPERATION_READ);
    maage_mutex_unlock(session->mutex);
    return result;
}

maage_status maage_local_session_offer_mesh(maage_local_session *session,
    maage_local_mesh_update *update)
{
    size_t i;
    maage_world_mesh *replaced = NULL;
    maage_status result;
    if (!session) return session_argument(MAAGE_OPERATION_SUBMIT, 0);
    if (!update || (update->removed != 0 && update->removed != 1) ||
        (update->removed && update->mesh) || (!update->removed && !update->mesh))
        return session_argument(MAAGE_OPERATION_SUBMIT, 1);
    maage_mutex_lock(session->mutex);
    if (session->closed) {
        result = session_status(MAAGE_STATUS_INVALID_STATE, MAAGE_OPERATION_SUBMIT);
    } else if (session->has_mesh_epoch && update->world_epoch < session->mesh_epoch) {
        /* Caller retains the obsolete detached mesh to dispose of. */
        result = session_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_SUBMIT);
    } else {
        if (!session->has_mesh_epoch || update->world_epoch > session->mesh_epoch) {
            /* A world switch invalidates the entire old CPU mailbox; the
             * Client separately invalidates old GPU meshes by world_epoch. */
            for (i = 0; i < session->mesh_count; ++i)
                (void)maage_world_mesh_destroy(session->meshes[i].mesh);
            session->mesh_count = 0;
            session->mesh_epoch = update->world_epoch;
            session->has_mesh_epoch = 1;
        }
        for (i = 0; i < session->mesh_count; ++i)
            if (session->meshes[i].x == update->x && session->meshes[i].z == update->z)
                break;
        if (i < session->mesh_count) {
            if (update->revision < session->meshes[i].revision ||
                (update->revision == session->meshes[i].revision &&
                 (session->meshes[i].removed || !update->removed))) {
                result = session_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_SUBMIT);
            } else {
                replaced = session->meshes[i].mesh;
                session->meshes[i] = *update;
                update->mesh = NULL;
                maage_condition_signal(session->changed);
                result = session_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_SUBMIT);
            }
        } else if (session->mesh_count == session->mesh_capacity) {
            /* Never consume a distinct column update on saturation. The World
             * owner can retry, ensuring new columns cannot disappear silently. */
            result = session_status(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_OPERATION_SUBMIT);
        } else {
            session->meshes[session->mesh_count++] = *update;
            update->mesh = NULL;
            maage_condition_signal(session->changed);
            result = session_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_SUBMIT);
        }
    }
    maage_mutex_unlock(session->mutex);
    /* CPU allocation release is outside the synchronization hot section. */
    if (replaced) (void)maage_world_mesh_destroy(replaced);
    return result;
}

maage_status maage_local_session_take_mesh(maage_local_session *session,
    maage_local_mesh_update *update)
{
    maage_status result;
    if (!session) return session_argument(MAAGE_OPERATION_READ, 0);
    if (!update) return session_argument(MAAGE_OPERATION_READ, 1);
    maage_mutex_lock(session->mutex);
    if (session->mesh_count) {
        *update = session->meshes[0];
        --session->mesh_count;
        if (session->mesh_count)
            memmove(session->meshes, session->meshes + 1,
                session->mesh_count * sizeof(*session->meshes));
        memset(&session->meshes[session->mesh_count], 0, sizeof(*session->meshes));
        maage_condition_signal(session->changed);
        result = session_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_READ);
    } else
        result = session_status(session->closed ? MAAGE_STATUS_INVALID_STATE :
            MAAGE_STATUS_NOOP, MAAGE_OPERATION_READ);
    maage_mutex_unlock(session->mutex);
    return result;
}

maage_status maage_local_session_close(maage_local_session *session)
{
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

maage_status maage_local_session_destroy(maage_local_session *session)
{
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
    for (i = 0; i < session->mesh_count; ++i)
        (void)maage_world_mesh_destroy(session->meshes[i].mesh);
    free(session->meshes);
    free(session->events);
    free(session->commands);
    free(session);
    return session_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_DESTROY);
}
