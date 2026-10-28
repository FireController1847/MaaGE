#ifndef MAAGE_LOCAL_SESSION_H
#define MAAGE_LOCAL_SESSION_H

#include "maage/session_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct maage_local_session maage_local_session;

/* Only the local in-process channel transfers detached World CPU pointers.
 * Offer consumes update->mesh, setting it to NULL, only on SUCCESS.
 * On NOOP (obsolete update) or failure, the caller still owns the mesh and
 * must destroy or retry it. A removal has removed!=0 and mesh==NULL.
 * Server calls maage_world_take_mesh() on the World owner thread before offer.
 * Client uploads and calls maage_world_mesh_destroy() after take. */
typedef struct maage_local_mesh_update {
    int x, z;
    unsigned long revision;
    unsigned long world_epoch;
    int removed;
    maage_world_mesh *mesh;
} maage_local_mesh_update;

/* Capacities are positive element counts, not bytes. Queues allocate exactly
 * these bounded slots at creation. Commands and events are nonblocking FIFO;
 * full queues return BUFFER_OVERFLOW (backpressure) without consuming input.
 * The producer must retry important commands/events. Snapshots replace older
 * snapshots, and read_snapshot returns the latest copied value (not a borrow).
 * All APIs except create/destroy are thread safe; destroy requires all other
 * callers to have stopped, normally after close and thread join. */
maage_status maage_local_session_create(unsigned int command_capacity,
    unsigned int event_capacity, unsigned int mesh_capacity,
    maage_local_session **output);
maage_status maage_local_session_send(maage_local_session *session,
    const maage_session_command *command);
maage_status maage_local_session_take_command(maage_local_session *session,
    maage_session_command *command);
maage_status maage_local_session_publish_snapshot(maage_local_session *session,
    const maage_session_snapshot *snapshot);
maage_status maage_local_session_read_snapshot(maage_local_session *session,
    maage_session_snapshot *snapshot);
maage_status maage_local_session_publish_event(maage_local_session *session,
    const maage_session_event *event);
maage_status maage_local_session_take_event(maage_local_session *session,
    maage_session_event *event);
maage_status maage_local_session_offer_mesh(maage_local_session *session,
    maage_local_mesh_update *mesh_update);
maage_status maage_local_session_take_mesh(maage_local_session *session,
    maage_local_mesh_update *mesh_update);
/* One latest complete entity publication, with bounded queue depth. Offer
 * consumes array on SUCCESS only; take transfers it to the Client. Pointer
 * swaps under the mailbox mutex never copy an unbounded array under a lock.
 * No Server/World-owned memory is exposed. Caller frees taken arrays. */
maage_status maage_local_session_offer_entities(maage_local_session *session,
    maage_session_entities *entities);
maage_status maage_local_session_take_entities(maage_local_session *session,
    maage_session_entities *entities);
/* Fixed-size latest mailbox. Take copies a publication once; the Client keeps
 * it until a newer epoch/revision arrives. Never a per-frame Server request. */
maage_status maage_local_session_publish_collision(maage_local_session *session,
    const maage_session_collision *collision);
maage_status maage_local_session_take_collision(maage_local_session *session,
    maage_session_collision *collision);
/* Close rejects new writes, broadcasts wakeups and preserves queued data for
 * draining. Empty reads after close return INVALID_STATE, not NOOP. */
maage_status maage_local_session_close(maage_local_session *session);
/* Frees all untaken owned meshes. No concurrent calls during destruction. */
maage_status maage_local_session_destroy(maage_local_session *session);

#ifdef __cplusplus
}
#endif
#endif
