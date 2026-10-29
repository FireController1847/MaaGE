#ifndef MAAGE_SERVER_RUNTIME_H
#define MAAGE_SERVER_RUNTIME_H
#include "maage/world.h"
#include "maage/status.h"
#include "maage/content.h"
#include "maage/local_session.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Both entry points use one owner-thread simulation and persistence lifecycle. */
typedef struct maage_server_runtime maage_server_runtime;
enum {
    MAAGE_SERVER_INTEGRATED = 1U,
    MAAGE_SERVER_DEDICATED = 2U
};

typedef struct maage_server_config {
    /* Physical UUID instance directory containing instance.json, not a data root. */
    const char *instance_root;
    /* NULL resumes the selected world (or creates one if none is selected). */
    const char *world_id, *world_name;
    int create_world, has_seed;
    maage_world_seed seed;
    /* Optional policy applies only at creation; existing worlds retain theirs. */
    const maage_generation_policy *generation_policy;
    unsigned int world_workers;
    int render_distance;
    /* -1 retains the saved mode, 0 Survival, 1 Creative. */
    int initial_game_mode;
    /* 0 chooses established game defaults; otherwise (0, 64]. */
    double interaction_range;
    unsigned int execution_mode;
    /* Borrowed, immutable/finalized for the full runtime lifetime. NULL requests
     * the shared, graphics-free built-in content bootstrap. */
    const maage_content *content;
    /* Optional bounded copy transport; NULL is valid for dedicated hosting. */
    maage_local_session *session;
} maage_server_config;

/* Create copies string/options inputs. World, storage and workers are created
 * on the authoritative thread after start, never by Client/render threads.
 * The caller retains borrowed content and session through join/destroy. */
maage_status maage_server_runtime_create(const maage_server_config *config,
                                         maage_server_runtime **output, maage_status_trace *trace);
maage_status maage_server_runtime_start(maage_server_runtime *runtime,
                                        maage_status_trace *trace);
/* Safe on a different thread, idempotent, and wakes the tick scheduler. */
void maage_server_runtime_request_stop(maage_server_runtime *runtime);
/* May block; never call on the Server thread or a rendering hot path. Returns
 * the original asynchronous failure and copies its causal trace. */
maage_status maage_server_runtime_join(maage_server_runtime *runtime,
                                       maage_status_trace *trace);
/* A finished runtime remains queryable until destroy; 1 means its worker
 * returned (including early startup failure). */
int maage_server_runtime_finished(maage_server_runtime *runtime);
/* Stops and joins if necessary; preserves borrowed resources on join failure. */
maage_status maage_server_runtime_destroy(maage_server_runtime *runtime);

#ifdef __cplusplus
}
#endif
#endif
