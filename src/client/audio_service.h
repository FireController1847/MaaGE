#ifndef MAAGE_CLIENT_AUDIO_SERVICE_H
#define MAAGE_CLIENT_AUDIO_SERVICE_H
#include "options.h"
#include "maage/status.h"
#include "maage/session_protocol.h"

/* All event and snapshot records use the numeric local-session ABI.
 * Client UI events are synthesized locally with the same numeric IDs. */
typedef struct maage_client_audio_service maage_client_audio_service;

/* The component adapter is installed once before create, by the aggregate
 * Client build. It binds the independently owned Audio, Bank, Catalog and
 * audio_scene APIs; this API deliberately makes no new Audio-core ABI claims.
 * Event playback and listener callbacks never acquire decoder resources.
 * Environmental effects are decoded at scene binding; playback performs no
 * resource acquisition. Music uses a separate streaming producer.
 * Nonfatal warnings/partial results may accompany usable output handles.
 * All handles are owned by the service, not the adapter or the caller.
 * set_volume's bus is the numeric MAAGE_CLIENT_AUDIO_BUS_* value.
 * stop_voices must synchronously quiesce every voice before catalog release. */
#define MAAGE_CLIENT_AUDIO_COMPONENTS_VERSION 2U
typedef struct maage_client_audio_components {
    unsigned int version;
    maage_status (*bank_create)(const char *data_root, void **bank, maage_status_trace *trace);
    maage_status (*engine_create)(void **engine, maage_status_trace *trace);
    maage_status (*catalog_create)(void *bank, void *engine, void **catalog, maage_status_trace *trace);
    maage_status (*scene_create)(void *bank, void *catalog, void *engine, void **scene, maage_status_trace *trace);
    maage_status (*set_volume)(void *engine, unsigned int bus, int percent, maage_status_trace *trace);
    maage_status (*set_listener)(void *engine, const double position[3], const double forward[3], maage_status_trace *trace);
    maage_status (*play_event)(void *catalog, void *engine, const maage_sound_event *event, maage_status_trace *trace);
    maage_status (*scene_update)(void *scene, const maage_session_snapshot *snapshot,
                                 const double position[3], const double forward[3], double now, maage_status_trace *trace);
    maage_status (*scene_stop)(void *scene, maage_status_trace *trace);
    /* Optional nonblocking device health check; never opens devices or files. */
    maage_status (*poll)(void *engine, maage_status_trace *trace);
    /* Optional: avoid starting background tracks while Music is muted. */
    maage_status (*scene_music_enabled)(void *scene, int enabled);
    maage_status (*stop_voices)(void *engine, maage_status_trace *trace);
    /* Quiesce World effects while preserving music and local interface audio. */
    maage_status (*reset_world)(void *engine, maage_status_trace *trace);
    maage_status (*engine_stop)(void *engine, maage_status_trace *trace);
    maage_status (*scene_destroy)(void *scene, maage_status_trace *trace);
    maage_status (*catalog_destroy)(void *catalog, maage_status_trace *trace);
    maage_status (*engine_destroy)(void *engine, maage_status_trace *trace);
    maage_status (*bank_destroy)(void *bank, maage_status_trace *trace);
} maage_client_audio_components;

/* Install once before the first create on the Client owner thread. NULL
 * removes the registration only before creation. Never re-register live. */
maage_status maage_client_audio_service_components_install(const maage_client_audio_components *components);

maage_status maage_client_audio_service_create(const char *data_root,
                                               const maage_client_options *options, maage_client_audio_service **output,
                                               maage_status_trace *trace);
maage_status maage_client_audio_service_apply_options(maage_client_audio_service *service,
                                                      const maage_client_options *options);
maage_status maage_client_audio_service_event(maage_client_audio_service *service,
                                              const maage_sound_event *sound_event, maage_status_trace *trace);
/* Uses MAAGE_SOUND_GUI_CLICK through the same semantic numeric event contract. */
maage_status maage_client_audio_service_ui_click(maage_client_audio_service *service,
                                                 maage_status_trace *trace);
/* Stop world effects, discard their queued events and flush queued local GUI
 * activations before a caller begins blocking world teardown. */
maage_status maage_client_audio_service_reset_world(maage_client_audio_service *service,
                                                    maage_status_trace *trace);
maage_status maage_client_audio_service_update(maage_client_audio_service *service,
                                               const maage_session_snapshot *session_snapshot, const double listener_position[3],
                                               const double listener_forward[3], double now, maage_status_trace *trace);
/* Stops active voices before releasing dependent resources. On a failed
 * quiesce or scene teardown, ownership is retained and destroy may be retried;
 * a failed return does not guarantee that the service pointer was released. */
maage_status maage_client_audio_service_destroy(maage_client_audio_service *service,
                                                maage_status_trace *trace);

#endif
