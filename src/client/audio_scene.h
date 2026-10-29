#ifndef MAAGE_CLIENT_AUDIO_SCENE_H
#define MAAGE_CLIENT_AUDIO_SCENE_H

#include <stddef.h>
#include "maage/status.h"
#include "maage/session_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Presentation-thread environmental audio. Catalog and device are borrowed;
 * destroy the scene before either. World observations read only the Client-owned replica during update.
 */
typedef struct maage_client_audio_scene maage_client_audio_scene;

/* Numeric categories; adapter maps them to Audio's category volumes. */
enum {
    MAAGE_AUDIO_SCENE_WEATHER = 1,
    MAAGE_AUDIO_SCENE_AMBIENT = 2,
    MAAGE_AUDIO_SCENE_MUSIC = 3,
    MAAGE_AUDIO_SCENE_BLOCKS = 4
};

/* Bounded source candidates sampled from the Client replica.
 * Flowing water requires metadata 1..7; lava requires an exposed air surface.
 * These are observations, never Client-authoritative world mutations. */
typedef struct maage_client_audio_scene_observation {
    double rain_intensity;
    double rain_position[3], water_position[3], lava_position[3];
    int rain_available, rain_above;
    int flowing_water_available, lava_available;
    unsigned int water_samples, lava_samples;
} maage_client_audio_scene_observation;

typedef unsigned long maage_client_audio_scene_sound_id;
typedef unsigned long maage_client_audio_scene_voice;

/*
 * Adapter callbacks run only on the Client presentation thread; none run in
 * an audio device callback. resolve_sound resolves logical paths to NUMERIC
 * catalog IDs only at bind time (no decoding or I/O there).
 * voice 0 is reserved as "no voice". play_sound owns at most one active voice
 * per scene channel, and loops only when sound_can_loop explicitly permits it.
 * music_prepare returns a selected track's owned streaming handle and the
 * retained ring-buffer allocation size, bounded by max_bytes; it does not
 * decode the entire track. music_play borrows the handle until stopped or
 * naturally complete; music_release stops/joins its decoder producer first.
 * The Audio service owns final category/mute settings.
 */
typedef struct maage_client_audio_scene_ports {
    maage_status (*play_event)(void *catalog, void *audio, const maage_sound_event *event, maage_status_trace *trace);
    maage_status (*resolve_sound)(void *context, const void *catalog, const char *logical_asset_path, maage_client_audio_scene_sound_id *sound);
    int (*sound_can_loop)(void *context, const void *catalog, maage_client_audio_scene_sound_id sound);
    maage_status (*play_sound)(void *context, void *audio, maage_client_audio_scene_sound_id sound, unsigned int category, const double *position, double gain, double pitch, int loop, maage_client_audio_scene_voice *voice);
    maage_status (*music_prepare)(void *context, const void *catalog, maage_client_audio_scene_sound_id sound, size_t max_bytes, void **pcm, size_t *bytes);
    maage_status (*music_play)(void *context, void *audio, void *pcm, double gain, maage_client_audio_scene_voice *voice);
    void (*music_release)(void *context, void *pcm);
    int (*voice_running)(void *context, void *audio, maage_client_audio_scene_voice voice);
    maage_status (*voice_gain)(void *context, void *audio, maage_client_audio_scene_voice voice, double gain);
    maage_status (*voice_pause)(void *context, void *audio, maage_client_audio_scene_voice voice, int paused);
    maage_status (*voice_stop)(void *context, void *audio, maage_client_audio_scene_voice voice);
} maage_client_audio_scene_ports;

#define MAAGE_AUDIO_SCENE_MAX_PCM_BYTES ((size_t) 96U * 1024U * 1024U)

maage_status maage_client_audio_scene_create(const void *catalog, void *audio, maage_client_audio_scene **output, maage_status_trace *trace);
/* Resolves logical 1.7.10 assets without decoding them. Exactly once. */
maage_status maage_client_audio_scene_bind(maage_client_audio_scene *scene, const maage_client_audio_scene_ports *ports, void *context, maage_status_trace *trace);
maage_status maage_client_audio_scene_update(maage_client_audio_scene *scene, const maage_session_snapshot *snapshot, const maage_world *visual, const double listener_position[3], double now, maage_status_trace *trace);
maage_status maage_client_audio_scene_pause(maage_client_audio_scene *scene, int paused);
/* False avoids loading/playing music when its category is disabled. */
maage_status maage_client_audio_scene_music_pause(maage_client_audio_scene *scene, int paused);
maage_status maage_client_audio_scene_music_enabled(maage_client_audio_scene *scene, int enabled);
maage_status maage_client_audio_scene_destroy(maage_client_audio_scene *scene);

#ifdef __cplusplus
}
#endif
#endif
