#include "audio_service.h"
#include "maage/log.h"
#include <stdlib.h>
#include <string.h>
#include <float.h>

/* An upper bound independent of frame rate and server-burst size. The caller
 * owns transport batching; this owner-thread queue never allocates on submit. */
#define MAAGE_CLIENT_AUDIO_QUEUE_SIZE 128U
#define MAAGE_CLIENT_AUDIO_NOTICE_COMPONENTS 1U
#define MAAGE_CLIENT_AUDIO_NOTICE_BANK 2U
#define MAAGE_CLIENT_AUDIO_NOTICE_DEVICE 4U
#define MAAGE_CLIENT_AUDIO_NOTICE_CATALOG 8U
#define MAAGE_CLIENT_AUDIO_NOTICE_SCENE 16U
#define MAAGE_CLIENT_AUDIO_NOTICE_VOLUME 32U
#define MAAGE_CLIENT_AUDIO_NOTICE_LISTENER 64U
#define MAAGE_CLIENT_AUDIO_NOTICE_EVENT 128U
#define MAAGE_CLIENT_AUDIO_NOTICE_UPDATE 256U
#define MAAGE_CLIENT_AUDIO_NOTICE_OVERFLOW 512U
#define MAAGE_CLIENT_AUDIO_NOTICE_DESTROY 1024U
#define MAAGE_CLIENT_AUDIO_NOTICE_POLL 2048U

struct maage_client_audio_service {
    maage_client_audio_components api;
    void *bank;
    void *engine;
    void *catalog;
    void *scene;
    maage_sound_event queue[MAAGE_CLIENT_AUDIO_QUEUE_SIZE];
    unsigned int first, count, notice_mask;
    int volume[MAAGE_CLIENT_AUDIO_BUS_COUNT];
    int accepting, playable;
};

static maage_client_audio_components maage_client_audio_registered;
static int maage_client_audio_has_registration;
static unsigned int maage_client_audio_live;

static maage_status maage_client_audio_result(unsigned char severity, unsigned char operation) {
    return maage_status_make(severity, severity < 0x40 ? 0 : MAAGE_CONTEXT_CLIENT, operation, 0);
}

static maage_status maage_client_audio_invalid(unsigned char operation) {
    return maage_client_audio_result(MAAGE_STATUS_INVALID_ARGUMENT, operation);
}

static int maage_client_audio_valid_options(const maage_client_options *options) {
    int bus;
    if (!options) return 0;
    for (bus = 0; bus < MAAGE_CLIENT_AUDIO_BUS_COUNT; ++bus)
        if (options->audio_volume[bus] < MAAGE_CLIENT_AUDIO_VOLUME_MIN ||
            options->audio_volume[bus] > MAAGE_CLIENT_AUDIO_VOLUME_MAX) return 0;
    return 1;
}

static void maage_client_audio_notice(maage_client_audio_service *service,
                                      unsigned int notice, maage_status failure, maage_status_trace *trace, const char *message) {
    if (service->notice_mask & notice) return;
    service->notice_mask |= notice;
    maage_log_trace_status(MAAGE_LOG_WARNING, "maage.client.audio", failure, trace,
                           "%s", message);
}

static maage_status maage_client_audio_recover(maage_status original,
                                               unsigned char operation, maage_status_trace *trace) {
    maage_status recovered;
    recovered = maage_client_audio_result(MAAGE_STATUS_PARTIAL, operation);
    (void) MAAGE_STATUS_TRACE(trace, original, MAAGE_CONTEXT_CLIENT, operation);
    maage_status_trace_recover(trace, recovered, MAAGE_CONTEXT_CLIENT,
                               operation, __FILE__, (unsigned long) __LINE__);
    return recovered;
}

maage_status maage_client_audio_service_components_install(const maage_client_audio_components *components) {
    if (maage_client_audio_live) return maage_client_audio_result(MAAGE_STATUS_INVALID_STATE, MAAGE_OPERATION_REGISTER);
    if (!components) {
        memset(&maage_client_audio_registered, 0, sizeof(maage_client_audio_registered));
        maage_client_audio_has_registration = 0;
        return maage_client_audio_result(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_REGISTER);
    }
    if (components->version != MAAGE_CLIENT_AUDIO_COMPONENTS_VERSION ||
        !components->bank_create || !components->engine_create || !components->catalog_create ||
        !components->set_volume || !components->set_listener || !components->play_event ||
        !components->stop_voices || !components->reset_world || !components->engine_stop ||
        !components->catalog_destroy || !components->engine_destroy || !components->bank_destroy ||
        (components->scene_create && (!components->scene_update || !components->scene_stop ||
                                      !components->scene_destroy))) return maage_client_audio_invalid(MAAGE_OPERATION_REGISTER);
    maage_client_audio_registered = *components;
    maage_client_audio_has_registration = 1;
    return maage_client_audio_result(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_REGISTER);
}

maage_status maage_client_audio_service_apply_options(maage_client_audio_service *service,
                                                      const maage_client_options *options) {
    maage_status result, first_failure;
    int bus;
    if (!service || !maage_client_audio_valid_options(options))
        return maage_client_audio_invalid(MAAGE_OPERATION_SET);
    first_failure = maage_client_audio_result(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_SET);
    for (bus = 0; bus < MAAGE_CLIENT_AUDIO_BUS_COUNT; ++bus) {
        if (service->volume[bus] == options->audio_volume[bus]) continue;
        /* Cache values even in silent mode so a valid Client options edit does
         * not make the missing-device fallback continually retry I/O. */
        if (service->playable) {
            result = service->api.set_volume(service->engine, (unsigned int) bus,
                                             options->audio_volume[bus], NULL);
            if (maage_status_failed(result)) {
                if (!maage_status_failed(first_failure)) first_failure = result;
                maage_client_audio_notice(service, MAAGE_CLIENT_AUDIO_NOTICE_VOLUME,
                                          result, NULL, "Audio volume change failed; remaining controls stay usable.");
                continue;
            }
        }
        service->volume[bus] = options->audio_volume[bus];
    }
    if (service->scene && service->api.scene_music_enabled) {
        result = service->api.scene_music_enabled(service->scene,
                                                  options->audio_volume[MAAGE_CLIENT_AUDIO_BUS_MUSIC] > 0 &&
                                                      options->audio_volume[MAAGE_CLIENT_AUDIO_BUS_MASTER] > 0);
        if (maage_status_failed(result) && !maage_status_failed(first_failure))
            first_failure = result;
    }
    if (maage_status_failed(first_failure)) return first_failure;
    return maage_client_audio_result(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_SET);
}

/* Cleanup is also used after partial initialization. No callback receives a
 * handle that has not been successfully created by its owning component. */
static maage_status maage_client_audio_release(maage_client_audio_service *service,
                                               maage_status_trace *trace) {
    maage_status result, first_failure;
    first_failure = maage_client_audio_result(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_DESTROY);
    service->accepting = 0;
    service->count = service->first = 0;
    if (service->scene) {
        result = service->api.scene_stop(service->scene, trace);
        if (maage_status_failed(result) && !maage_status_failed(first_failure)) first_failure = result;
    }
    if (service->engine) {
        result = service->api.stop_voices(service->engine, trace);
        if (maage_status_failed(result)) {
            /* Never release catalog PCM while device voices may read it. */
            return result;
        }
    }
    if (service->scene) {
        result = service->api.scene_destroy(service->scene, trace);
        if (maage_status_failed(result)) {
            /* The scene owns a still-live handle; retain all dependencies. */
            return result;
        }
        service->scene = NULL;
    }
    /* All playing references are quiescent before catalog PCM release. */
    if (service->catalog) {
        result = service->api.catalog_destroy(service->catalog, trace);
        if (maage_status_failed(result) && !maage_status_failed(first_failure)) first_failure = result;
        service->catalog = NULL;
    }
    if (service->engine) {
        result = service->api.engine_stop(service->engine, trace);
        if (maage_status_failed(result) && !maage_status_failed(first_failure)) first_failure = result;
        result = service->api.engine_destroy(service->engine, trace);
        if (maage_status_failed(result) && !maage_status_failed(first_failure)) first_failure = result;
        service->engine = NULL;
    }
    if (service->bank) {
        result = service->api.bank_destroy(service->bank, trace);
        if (maage_status_failed(result) && !maage_status_failed(first_failure)) first_failure = result;
        service->bank = NULL;
    }
    service->playable = 0;
    return first_failure;
}

maage_status maage_client_audio_service_create(const char *data_root,
                                               const maage_client_options *options, maage_client_audio_service **output,
                                               maage_status_trace *trace) {
    maage_client_audio_service *service;
    maage_status result, problem, recovered, warning;
    int bus;
    if (output) *output = NULL;
    if (!output || !maage_client_audio_valid_options(options))
        return MAAGE_STATUS_TRACE(trace, maage_client_audio_invalid(MAAGE_OPERATION_CREATE),
                                  MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_CREATE);
    service = (maage_client_audio_service *) calloc(1, sizeof(*service));
    if (!service) return MAAGE_STATUS_TRACE(trace,
                                            maage_client_audio_result(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_CREATE),
                                            MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_CREATE);
    for (bus = 0; bus < MAAGE_CLIENT_AUDIO_BUS_COUNT; ++bus)
        service->volume[bus] = -1;
    service->accepting = 1;
    warning = maage_client_audio_result(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_CREATE);
    ++maage_client_audio_live;
    if (!maage_client_audio_has_registration) {
        problem = maage_client_audio_result(MAAGE_STATUS_NOT_IMPLEMENTED, MAAGE_OPERATION_INITIALIZE);
        recovered = maage_client_audio_recover(problem, MAAGE_OPERATION_CREATE, trace);
        maage_client_audio_notice(service, MAAGE_CLIENT_AUDIO_NOTICE_COMPONENTS, problem, trace,
                                  "Audio components are not linked; the Client continues without sound.");
        maage_client_audio_service_apply_options(service, options);
        *output = service;
        return recovered;
    }
    service->api = maage_client_audio_registered;
    result = service->api.bank_create(data_root, &service->bank, trace);
    if (maage_status_failed(result) || !service->bank) {
        problem = maage_status_failed(result) ? result : maage_client_audio_result(MAAGE_STATUS_NOT_FOUND, MAAGE_OPERATION_INITIALIZE);
        maage_client_audio_notice(service, MAAGE_CLIENT_AUDIO_NOTICE_BANK, problem, trace,
                                  "Sound bank unavailable; the Client continues silently.");
        goto muted;
    }
    if (maage_status_is_warning(result)) {
        warning = result;
        maage_client_audio_notice(service, MAAGE_CLIENT_AUDIO_NOTICE_BANK, result, trace,
                                  "Some sound assets are unavailable; other audio remains usable.");
    }
    result = service->api.engine_create(&service->engine, trace);
    if (maage_status_failed(result) || !service->engine) {
        problem = maage_status_failed(result) ? result : maage_client_audio_result(MAAGE_STATUS_NOT_FOUND, MAAGE_OPERATION_INITIALIZE);
        maage_client_audio_notice(service, MAAGE_CLIENT_AUDIO_NOTICE_DEVICE, problem, trace,
                                  "No usable audio output device; gameplay remains available without sound.");
        goto muted;
    }
    /* MaaGE::Audio documents PARTIAL plus a valid handle as a silent engine
     * without a device. Release it at this startup boundary: do not continue
     * queuing inaudible one-shots or retrying the device per frame. */
    if (maage_status_code(result) == MAAGE_STATUS_PARTIAL) {
        problem = result;
        maage_client_audio_notice(service, MAAGE_CLIENT_AUDIO_NOTICE_DEVICE, result, trace,
                                  "No usable audio output device; gameplay remains available without sound.");
        goto muted;
    }
    if (maage_status_is_warning(result)) {
        if (!maage_status_is_warning(warning)) warning = result;
        maage_client_audio_notice(service, MAAGE_CLIENT_AUDIO_NOTICE_DEVICE, result, trace,
                                  "Audio backend recovered with reduced capability.");
    }
    result = service->api.catalog_create(service->bank, service->engine, &service->catalog, trace);
    if (maage_status_failed(result) || !service->catalog) {
        problem = maage_status_failed(result) ? result : maage_client_audio_result(MAAGE_STATUS_NOT_FOUND, MAAGE_OPERATION_REGISTER);
        maage_client_audio_notice(service, MAAGE_CLIENT_AUDIO_NOTICE_CATALOG, problem, trace,
                                  "Sound catalog unavailable; the Client continues silently.");
        goto muted;
    }
    if (maage_status_is_warning(result)) {
        if (!maage_status_is_warning(warning)) warning = result;
        maage_client_audio_notice(service, MAAGE_CLIENT_AUDIO_NOTICE_CATALOG, result, trace,
                                  "Some optional catalog sounds were skipped.");
    }
    service->playable = 1;
    result = maage_client_audio_service_apply_options(service, options);
    if (maage_status_failed(result)) {
        /* A failed category setter must not discard a working sound device. */
        warning = result;
    }
    if (service->api.scene_create) {
        result = service->api.scene_create(service->bank, service->catalog,
                                           service->engine, &service->scene, trace);
        if (maage_status_failed(result)) {
            maage_client_audio_notice(service, MAAGE_CLIENT_AUDIO_NOTICE_SCENE, result, trace,
                                      "Environmental audio could not start; gameplay sounds remain available.");
            /* A failed callback may still allocate a partial scene. Dispose of
             * it now instead of updating it or leaking it later. */
            if (service->scene) {
                maage_status cleanup = service->api.scene_destroy(service->scene, trace);
                /* A failed destroy preserves this owned handle for shutdown.
                 * The logical scene may still borrow the engine and bank. */
                if (!maage_status_failed(cleanup)) service->scene = NULL;
            }
            problem = result;
            recovered = maage_client_audio_recover(problem, MAAGE_OPERATION_CREATE, trace);
            *output = service;
            return recovered;
        }
        if (maage_status_is_warning(result)) {
            if (!maage_status_is_warning(warning)) warning = result;
            maage_client_audio_notice(service, MAAGE_CLIENT_AUDIO_NOTICE_SCENE, result, trace,
                                      "Some environmental ambience is unavailable.");
        }
        if (service->scene && service->api.scene_music_enabled)
            (void) service->api.scene_music_enabled(service->scene,
                                                    options->audio_volume[MAAGE_CLIENT_AUDIO_BUS_MUSIC] > 0 &&
                                                        options->audio_volume[MAAGE_CLIENT_AUDIO_BUS_MASTER] > 0);
    }
    *output = service;
    if (maage_status_is_warning(warning) || maage_status_failed(warning))
        return maage_client_audio_recover(warning, MAAGE_OPERATION_CREATE, trace);
    return maage_client_audio_result(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_CREATE);
muted:
    (void) maage_client_audio_release(service, NULL);
    maage_client_audio_service_apply_options(service, options);
    recovered = maage_client_audio_recover(problem, MAAGE_OPERATION_CREATE, trace);
    *output = service;
    return recovered;
}

maage_status maage_client_audio_service_event(maage_client_audio_service *service,
                                              const maage_sound_event *sound_event, maage_status_trace *trace) {
    unsigned int tail;
    maage_status result;
    if (!service || !sound_event || !sound_event->event_id ||
        sound_event->event_id > MAAGE_SOUND_MAX || sound_event->material > MAAGE_MATERIAL_MAX ||
        sound_event->intensity != sound_event->intensity ||
        sound_event->pitch != sound_event->pitch ||
        sound_event->intensity < 0.0 || sound_event->intensity > 10000.0 ||
        sound_event->pitch < 0.0 || sound_event->pitch > DBL_MAX ||
        !(sound_event->position[0] >= -DBL_MAX && sound_event->position[0] <= DBL_MAX) ||
        !(sound_event->position[1] >= -DBL_MAX && sound_event->position[1] <= DBL_MAX) ||
        !(sound_event->position[2] >= -DBL_MAX && sound_event->position[2] <= DBL_MAX))
        return MAAGE_STATUS_TRACE(trace, maage_client_audio_invalid(MAAGE_OPERATION_SUBMIT),
                                  MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_SUBMIT);
    if (!service->accepting) return maage_client_audio_result(MAAGE_STATUS_NOOP, MAAGE_OPERATION_SUBMIT);
    if (!service->playable || sound_event->intensity == 0)
        return maage_client_audio_result(MAAGE_STATUS_NOOP, MAAGE_OPERATION_SUBMIT);
    result = maage_client_audio_result(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_SUBMIT);
    if (service->count >= MAAGE_CLIENT_AUDIO_QUEUE_SIZE) {
        result = maage_client_audio_result(MAAGE_STATUS_PARTIAL, MAAGE_OPERATION_SUBMIT);
        maage_client_audio_notice(service, MAAGE_CLIENT_AUDIO_NOTICE_OVERFLOW, result, trace,
                                  "Audio event queue saturated; oldest sounds are dropped.");
        service->first = (service->first + 1U) % MAAGE_CLIENT_AUDIO_QUEUE_SIZE;
        --service->count;
    }
    tail = (service->first + service->count) % MAAGE_CLIENT_AUDIO_QUEUE_SIZE;
    service->queue[tail] = *sound_event;
    ++service->count;
    return result;
}

maage_status maage_client_audio_service_ui_click(maage_client_audio_service *service,
                                                 maage_status_trace *trace) {
    maage_sound_event click;
    memset(&click, 0, sizeof(click));
    click.event_id = MAAGE_SOUND_GUI_CLICK;
    click.material = MAAGE_MATERIAL_DEFAULT;
    click.intensity = 1.0;
    click.pitch = 1.0;
    return maage_client_audio_service_event(service, &click, trace);
}

maage_status maage_client_audio_service_reset_world(maage_client_audio_service *service,
                                                    maage_status_trace *trace) {
    maage_sound_event event;
    maage_status result, first_failure;
    if (!service) return maage_client_audio_result(MAAGE_STATUS_NOOP, MAAGE_OPERATION_STOP);
    if (!service->engine) {
        service->count = service->first = 0;
        return maage_client_audio_result(MAAGE_STATUS_NOOP, MAAGE_OPERATION_STOP);
    }
    first_failure = service->api.reset_world(service->engine, trace);
    if (maage_status_failed(first_failure)) return first_failure;
    /* Navigation can leave the frame before its queued activation is played.
     * Flush local UI effects while discarding obsolete gameplay effects. */
    while (service->count) {
        event = service->queue[service->first];
        service->first = (service->first + 1U) % MAAGE_CLIENT_AUDIO_QUEUE_SIZE;
        --service->count;
        if (event.event_id != MAAGE_SOUND_GUI_CLICK || !service->accepting || !service->playable) continue;
        result = service->api.play_event(service->catalog, service->engine, &event, trace);
        if (maage_status_failed(result)) {
            maage_client_audio_notice(service, MAAGE_CLIENT_AUDIO_NOTICE_EVENT, result, trace,
                                      "A GUI activation sound could not play during navigation.");
            if (!maage_status_failed(first_failure))
                first_failure = result;
        }
    }
    service->first = 0;
    if (maage_status_failed(first_failure))
        return maage_client_audio_recover(first_failure, MAAGE_OPERATION_STOP, trace);
    return first_failure;
}

maage_status maage_client_audio_service_update(maage_client_audio_service *service,
                                               const maage_session_snapshot *session_snapshot, const double listener_position[3],
                                               const double listener_forward[3], double now, maage_status_trace *trace) {
    unsigned int processed;
    maage_sound_event event;
    maage_status result, first_failure;
    if (!service || !listener_position || !listener_forward || now != now)
        return MAAGE_STATUS_TRACE(trace, maage_client_audio_invalid(MAAGE_OPERATION_UPDATE),
                                  MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_UPDATE);
    if (!service->accepting || !service->playable)
        return maage_client_audio_result(MAAGE_STATUS_NOOP, MAAGE_OPERATION_UPDATE);
    first_failure = maage_client_audio_result(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_UPDATE);
    result = service->api.set_listener(service->engine, listener_position, listener_forward, trace);
    if (maage_status_failed(result)) {
        first_failure = result;
        maage_client_audio_notice(service, MAAGE_CLIENT_AUDIO_NOTICE_LISTENER, result, trace,
                                  "Audio listener could not update; this frame's positional sounds may be inaccurate.");
    }
    for (processed = 0; processed < MAAGE_CLIENT_AUDIO_QUEUE_SIZE && service->count; ++processed) {
        event = service->queue[service->first];
        service->first = (service->first + 1U) % MAAGE_CLIENT_AUDIO_QUEUE_SIZE;
        --service->count;
        if (session_snapshot && session_snapshot->paused &&
            event.event_id != MAAGE_SOUND_GUI_CLICK) continue;
        result = service->api.play_event(service->catalog, service->engine, &event, trace);
        if (maage_status_failed(result)) {
            if (!maage_status_failed(first_failure)) first_failure = result;
            maage_client_audio_notice(service, MAAGE_CLIENT_AUDIO_NOTICE_EVENT, result, trace,
                                      "A sound event could not play; remaining events continue.");
        }
    }
    if (service->scene && service->api.scene_update) {
        result = service->api.scene_update(service->scene, session_snapshot,
                                           listener_position, listener_forward, now, trace);
        if (maage_status_failed(result)) {
            if (!maage_status_failed(first_failure)) first_failure = result;
            maage_client_audio_notice(service, MAAGE_CLIENT_AUDIO_NOTICE_UPDATE, result, trace,
                                      "Environmental sound update failed; ordinary sounds continue.");
        }
    }
    if (service->api.poll) {
        result = service->api.poll(service->engine, trace);
        if (maage_status_failed(result) || maage_status_code(result) == MAAGE_STATUS_PARTIAL) {
            if (!maage_status_failed(first_failure)) first_failure = result;
            service->playable = 0; /* Device loss: stop enqueuing inaudible events. */
            service->count = service->first = 0;
            maage_client_audio_notice(service, MAAGE_CLIENT_AUDIO_NOTICE_POLL, result, trace,
                                      "Audio output device stopped; gameplay will continue silently.");
        }
    }
    if (maage_status_failed(first_failure) || !service->playable)
        return maage_client_audio_recover(first_failure, MAAGE_OPERATION_UPDATE, trace);
    return maage_client_audio_result(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_UPDATE);
}

maage_status maage_client_audio_service_destroy(maage_client_audio_service *service,
                                                maage_status_trace *trace) {
    maage_status result;
    if (!service) return maage_client_audio_result(MAAGE_STATUS_NOOP, MAAGE_OPERATION_DESTROY);
    result = maage_client_audio_release(service, trace);
    if (maage_status_failed(result)) {
        maage_client_audio_notice(service, MAAGE_CLIENT_AUDIO_NOTICE_DESTROY, result, trace,
                                  "Audio shutdown reported a cleanup failure; retained resources can be retried.");
        return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_DESTROY);
    }
    --maage_client_audio_live;
    free(service);
    return maage_client_audio_result(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_DESTROY);
}
