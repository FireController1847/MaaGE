#ifndef MAAGE_CLIENT_AUDIO_SCENE_H
#define MAAGE_CLIENT_AUDIO_SCENE_H
#include <stddef.h>
#include "maage/status.h"
#include "maage/session_protocol.h"
#include "maage/sounds.h"

typedef struct maage_client_audio_scene maage_client_audio_scene;
typedef unsigned long maage_client_audio_scene_sound_id;
typedef unsigned long maage_client_audio_scene_voice;
/* Client-owner adapters borrow registered definitions. Resource resolution
 * binds numeric streaming handles without decoding; only the selected track
 * owns a decoder/ring. Release follows confirmed voice stop and joins decoding.
 * Ambient providers emit ordinary registered events through the shared catalog. */
typedef struct maage_client_audio_scene_ports {
    maage_status (*play_event)(void *, void *, const maage_sound_event *, maage_status_trace *);
    maage_status (*resolve_sound)(void *, const void *, const maage_sound_resource_definition *, maage_client_audio_scene_sound_id *);
    maage_status (*music_prepare)(void *, const void *, maage_client_audio_scene_sound_id, size_t, void **, size_t *);
    maage_status (*music_play)(void *, void *, void *, double, maage_client_audio_scene_voice *);
    maage_status (*music_release)(void *, void *);
    int (*voice_running)(void *, void *, maage_client_audio_scene_voice);
    maage_status (*voice_pause)(void *, void *, maage_client_audio_scene_voice, int);
    maage_status (*voice_stop)(void *, void *, maage_client_audio_scene_voice);
} maage_client_audio_scene_ports;
#define MAAGE_AUDIO_SCENE_MAX_PCM_BYTES ((size_t) 96U * 1024U * 1024U)
/* Definitions, catalog and device outlive scene destruction. Scene callbacks
 * read only the Client-owned replica; mutable provider state belongs here. */
maage_status maage_client_audio_scene_create(const maage_sound_registry *, const void *, void *, maage_client_audio_scene **, maage_status_trace *);
maage_status maage_client_audio_scene_bind(maage_client_audio_scene *, const maage_client_audio_scene_ports *, void *, maage_status_trace *);
maage_status maage_client_audio_scene_update(maage_client_audio_scene *, const maage_session_snapshot *, const maage_world *, const double[3], double, maage_status_trace *);
maage_status maage_client_audio_scene_pause(maage_client_audio_scene *, int);
maage_status maage_client_audio_scene_music_pause(maage_client_audio_scene *, int);
maage_status maage_client_audio_scene_music_enabled(maage_client_audio_scene *, int);
maage_status maage_client_audio_scene_destroy(maage_client_audio_scene *);
#endif
