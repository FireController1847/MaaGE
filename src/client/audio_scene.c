#include "audio_scene.h"
#include "maage/log.h"
#include "maage/time.h"
#include <stdlib.h>
#include <string.h>
#include <float.h>
#include <math.h>

typedef struct scene_ambient {
    void *state;
    double next_update;
} scene_ambient;
struct maage_client_audio_scene {
    const maage_sound_registry *definitions;
    const void *catalog;
    void *audio, *context;
    maage_client_audio_scene_ports ports;
    maage_client_audio_scene_sound_id *sounds;
    unsigned char *available, *disabled, *logged, *event_logged;
    scene_ambient *ambient;
    size_t resource_count, ambient_count;
    maage_client_audio_scene_voice music_voice;
    maage_sound_resource_id music_resource;
    maage_sound_music_id music_mode;
    void *music_pcm;
    size_t music_bytes;
    int bound, paused, clock_ready, music_enabled, music_paused, has_world, device_logged;
    unsigned long random, session_epoch, world_epoch, effect_sequence, effect_tick, failure_count;
    double last_now, next_music, music_paused_since;
};
static maage_status sc_result(unsigned char code, unsigned char operation) {
    return maage_status_make(code, code < MAAGE_STATUS_WARNING ? 0 : MAAGE_CONTEXT_CLIENT, operation, 0);
}
static unsigned long sc_random(maage_client_audio_scene *scene) {
    scene->random = (scene->random * 1664525UL + 1013904223UL) & 0xffffffffUL;
    return scene->random;
}
static int sc_pressure(maage_status result) {
    unsigned int code = maage_status_code(result);
    return code == MAAGE_STATUS_ALLOCATION_FAILED || code == MAAGE_STATUS_BUFFER_OVERFLOW || code == MAAGE_STATUS_TIMEOUT;
}
static void sc_resource_failed(maage_client_audio_scene *scene, maage_sound_resource_id id, maage_status result, maage_status_trace *trace) {
    const maage_sound_resource_definition *resource;
    if (!id || id > scene->resource_count) return;
    scene->disabled[id] = 1;
    if (scene->logged[id]) return;
    scene->logged[id] = 1;
    ++scene->failure_count;
    resource = maage_sound_resource_get(scene->definitions, id);
    MAAGE_STATUS_TRACE(trace, result, MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_LOAD);
    maage_log_trace_status(MAAGE_LOG_WARNING, "maage.client.audio_scene", result, trace, "Optional sound resource unavailable: %s; disabled until scene recreation.", resource->path);
}
static void sc_device_failed(maage_client_audio_scene *scene, maage_status result, maage_status_trace *trace) {
    if (scene->device_logged) return;
    scene->device_logged = 1;
    ++scene->failure_count;
    maage_log_trace_status(MAAGE_LOG_WARNING, "maage.client.audio_scene", result, trace, "Optional music playback failed; other audio remains available.");
}
static maage_status sc_stop_music(maage_client_audio_scene *scene) {
    maage_status result = sc_result(MAAGE_STATUS_NOOP, MAAGE_OPERATION_STOP);
    if (scene->music_voice) {
        result = scene->ports.voice_stop(scene->context, scene->audio, scene->music_voice);
        if (maage_status_failed(result)) return result;
        scene->music_voice = 0;
    }
    if (scene->music_pcm) {
        result = scene->ports.music_release(scene->context, scene->music_pcm);
        if (maage_status_failed(result)) return result;
        scene->music_pcm = NULL;
        scene->music_bytes = 0;
    }
    scene->music_resource = 0;
    return result;
}
static double sc_delay(maage_client_audio_scene *scene, double minimum, double maximum) {
    /* Tick-quantized scheduling keeps existing short/long playlists coherent;
     * registered delays remain independent of the number of registered tracks. */
    double span = maximum - minimum;
    double unit = (double) (sc_random(scene) >> 8) / 16777216.0;
    if (span <= DBL_MAX / 20) return minimum + floor(unit * (floor(span * 20) + 1)) / 20;
    return minimum + unit * span;
}
static maage_sound_resource_id sc_music_select(maage_client_audio_scene *scene, const maage_sound_music_definition *music) {
    size_t start, offset, index;
    maage_sound_resource_id id;
    if (!music || !music->resource_count) return 0;
    start = sc_random(scene) % music->resource_count;
    for (offset = 0; offset < music->resource_count; ++offset) {
        index = start + offset;
        if (index >= music->resource_count) index -= music->resource_count;
        id = music->resources[index];
        if (scene->available[id] && !scene->disabled[id]) return id;
    }
    return 0;
}
static void sc_music_update(maage_client_audio_scene *scene, double now, maage_status_trace *trace) {
    const maage_sound_music_definition *music = maage_sound_music_get(scene->definitions, scene->music_mode);
    maage_client_audio_scene_voice voice = 0;
    maage_sound_resource_id selected;
    maage_status result;
    void *pcm = NULL;
    size_t bytes = 0;
    if (scene->music_paused || !scene->music_enabled || !music) return;
    if (scene->music_pcm && !scene->music_voice) {
        result = sc_stop_music(scene);
        if (maage_status_failed(result)) {
            sc_device_failed(scene, result, trace);
            return;
        }
    }
    if (scene->music_voice) {
        if (scene->ports.voice_running(scene->context, scene->audio, scene->music_voice)) return;
        result = sc_stop_music(scene);
        if (maage_status_failed(result)) {
            sc_device_failed(scene, result, trace);
            return;
        }
        scene->next_music = now + sc_delay(scene, music->minimum_delay, music->maximum_delay);
    }
    if (now < scene->next_music) return;
    selected = sc_music_select(scene, music);
    if (!selected) {
        scene->next_music = now + music->unavailable_delay;
        return;
    }
    result = scene->ports.music_prepare(scene->context, scene->catalog, scene->sounds[selected], MAAGE_AUDIO_SCENE_MAX_PCM_BYTES, &pcm, &bytes);
    if (!maage_status_failed(result) && (!pcm || !bytes || bytes > MAAGE_AUDIO_SCENE_MAX_PCM_BYTES)) result = sc_result(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_OPERATION_DECODE);
    if (!maage_status_failed(result)) result = scene->ports.music_play(scene->context, scene->audio, pcm, 1, &voice);
    if (maage_status_failed(result) || !voice) {
        if (voice) {
            maage_status stopped = scene->ports.voice_stop(scene->context, scene->audio, voice);
            if (maage_status_failed(stopped)) {
                scene->music_voice = voice;
                scene->music_pcm = pcm;
                scene->music_bytes = bytes;
                scene->music_resource = selected;
                sc_device_failed(scene, stopped, trace);
                return;
            }
        }
        if (pcm) {
            maage_status released = scene->ports.music_release(scene->context, pcm);
            if (maage_status_failed(released)) {
                scene->music_pcm = pcm;
                scene->music_bytes = bytes;
                scene->music_resource = selected;
                sc_device_failed(scene, released, trace);
                return;
            }
        }
        if (!maage_status_failed(result)) result = sc_result(MAAGE_STATUS_INVALID_STATE, MAAGE_OPERATION_START);
        if (!sc_pressure(result)) sc_resource_failed(scene, selected, result, trace);
        scene->next_music = now + music->retry_delay;
        return;
    }
    scene->music_voice = voice;
    scene->music_pcm = pcm;
    scene->music_bytes = bytes;
    scene->music_resource = selected;
    scene->next_music = 0;
}
static maage_status sc_ambient_play(void *context, maage_sound_id id, unsigned int action, maage_sound_material_id material, const double position[3], double gain, double pitch, maage_status_trace *trace) {
    maage_client_audio_scene *scene = (maage_client_audio_scene *) context;
    maage_sound_event event;
    maage_status result;
    if (!maage_sound_get(scene->definitions, id) || !position || action >= MAAGE_SOUND_ACTION_COUNT || (material && !maage_sound_material_get(scene->definitions, material))) return sc_result(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_SUBMIT);
    memset(&event, 0, sizeof(event));
    event.event_id = id;
    event.action = action;
    event.material = material;
    event.tick = scene->effect_tick;
    event.sequence = ++scene->effect_sequence;
    memcpy(event.position, position, sizeof(event.position));
    event.intensity = gain;
    event.pitch = pitch;
    result = scene->ports.play_event((void *) scene->catalog, scene->audio, &event, trace);
    if (sc_pressure(result)) return sc_result(MAAGE_STATUS_NOOP, MAAGE_OPERATION_SUBMIT);
    if (maage_status_code(result) == MAAGE_STATUS_NOT_FOUND) {
        if (!scene->event_logged[id]) {
            scene->event_logged[id] = 1;
            ++scene->failure_count;
            maage_log_trace_status(MAAGE_LOG_WARNING, "maage.client.audio_scene", result, trace, "Optional sound event unavailable: %s.", maage_sound_name(scene->definitions, id));
        }
        return sc_result(MAAGE_STATUS_PARTIAL, MAAGE_OPERATION_SUBMIT);
    }
    return result;
}
maage_status maage_client_audio_scene_create(const maage_sound_registry *definitions, const void *catalog, void *audio, maage_client_audio_scene **output, maage_status_trace *trace) {
    maage_client_audio_scene *scene;
    const maage_sound_ambient_definition *ambient;
    size_t i, events;
    if (output) *output = NULL;
    if (!output || !catalog || !audio || !maage_sound_is_finalized(definitions)) return MAAGE_STATUS_PROPAGATE(trace, sc_result(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_CREATE), MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_CREATE);
    scene = (maage_client_audio_scene *) calloc(1, sizeof(*scene));
    if (!scene) return sc_result(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_CREATE);
    scene->definitions = definitions;
    scene->catalog = catalog;
    scene->audio = audio;
    scene->music_enabled = 1;
    scene->random = 0x7f4a7c15UL;
    scene->resource_count = maage_sound_resource_count(definitions);
    scene->ambient_count = maage_sound_ambient_count(definitions);
    events = maage_sound_count(definitions);
    if (scene->resource_count >= (size_t) -1 / sizeof(*scene->sounds) || scene->ambient_count > (size_t) -1 / sizeof(*scene->ambient) || events == (size_t) -1) {
        free(scene);
        return sc_result(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_OPERATION_CREATE);
    }
    scene->sounds = (maage_client_audio_scene_sound_id *) calloc(scene->resource_count + 1, sizeof(*scene->sounds));
    scene->available = (unsigned char *) calloc(scene->resource_count + 1, 1);
    scene->disabled = (unsigned char *) calloc(scene->resource_count + 1, 1);
    scene->logged = (unsigned char *) calloc(scene->resource_count + 1, 1);
    scene->event_logged = (unsigned char *) calloc(events + 1, 1);
    if (scene->ambient_count) scene->ambient = (scene_ambient *) calloc(scene->ambient_count, sizeof(*scene->ambient));
    if (!scene->sounds || !scene->available || !scene->disabled || !scene->logged || !scene->event_logged || (scene->ambient_count && !scene->ambient)) goto failed;
    for (i = 0; i < scene->ambient_count; ++i) {
        ambient = maage_sound_ambient_at(definitions, i);
        if (!ambient->state_bytes) continue;
        scene->ambient[i].state = calloc(1, ambient->state_bytes);
        if (!scene->ambient[i].state) goto failed;
    }
    *output = scene;
    return sc_result(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_CREATE);
failed:
    maage_client_audio_scene_destroy(scene);
    return sc_result(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_CREATE);
}
maage_status maage_client_audio_scene_bind(maage_client_audio_scene *scene, const maage_client_audio_scene_ports *ports, void *context, maage_status_trace *trace) {
    const maage_sound_resource_definition *resource;
    maage_status result, outcome = sc_result(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_INITIALIZE);
    size_t i;
    if (!scene || !ports || !ports->play_event || !ports->resolve_sound || !ports->music_prepare || !ports->music_play || !ports->music_release || !ports->voice_running || !ports->voice_pause || !ports->voice_stop) return sc_result(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_INITIALIZE);
    if (scene->bound) return sc_result(MAAGE_STATUS_INVALID_STATE, MAAGE_OPERATION_INITIALIZE);
    scene->ports = *ports;
    scene->context = context;
    scene->bound = 1;
    for (i = 1; i <= scene->resource_count; ++i) {
        resource = maage_sound_resource_get(scene->definitions, (maage_sound_resource_id) i);
        if (!resource->streamed) continue;
        result = ports->resolve_sound(context, scene->catalog, resource, &scene->sounds[i]);
        if (maage_status_failed(result)) {
            sc_resource_failed(scene, resource->id, result, trace);
            outcome = sc_result(MAAGE_STATUS_PARTIAL, MAAGE_OPERATION_INITIALIZE);
        } else scene->available[i] = 1;
    }
    return outcome;
}
maage_status maage_client_audio_scene_update(maage_client_audio_scene *scene, const maage_session_snapshot *snapshot, const maage_world *visual, const double listener[3], double now, maage_status_trace *trace) {
    maage_sound_scene_query query;
    maage_sound_ambient_sink sink;
    const maage_sound_bindings *bindings;
    const maage_sound_music_definition *music;
    const maage_sound_ambient_definition *ambient;
    maage_sound_music_id selected;
    maage_status result;
    unsigned long failures;
    size_t i;
    int world_changed;
    if (!scene || !scene->bound || !listener || !(now >= 0 && now <= DBL_MAX / 2)) return sc_result(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_UPDATE);
    for (i = 0; i < 3; ++i)
        if (!(listener[i] > -30000000 && listener[i] < 30000000)) return sc_result(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_UPDATE);
    if (scene->clock_ready && now < scene->last_now) return sc_result(MAAGE_STATUS_INVALID_STATE, MAAGE_OPERATION_UPDATE);
    failures = scene->failure_count;
    memset(&query, 0, sizeof(query));
    query.visual = visual;
    query.listener = listener;
    query.now = now;
    query.has_world = snapshot != NULL;
    if (snapshot) {
        query.environment = &snapshot->environment;
        query.tick = snapshot->tick;
        query.game_mode = snapshot->player.game_mode;
    }
    bindings = maage_sound_get_bindings(scene->definitions);
    selected = bindings->music ? bindings->music(bindings->context, &query) : 0;
    music = maage_sound_music_get(scene->definitions, selected);
    if (selected && !music) return sc_result(MAAGE_STATUS_NOT_FOUND, MAAGE_OPERATION_RESOLVE);
    if (!scene->clock_ready) {
        scene->random ^= (unsigned long) (fmod(now, 4294967.296) * 1000.0);
        scene->next_music = now + (music ? music->initial_delay : 0);
        scene->clock_ready = 1;
    }
    if (scene->music_mode != selected && scene->music_voice) {
        result = sc_stop_music(scene);
        if (maage_status_failed(result)) return result;
        scene->next_music = now + (music ? sc_delay(scene, 0, music->minimum_delay * 0.5) : 0);
    }
    scene->music_mode = selected;
    scene->last_now = now;
    world_changed = scene->has_world != query.has_world || (snapshot && (scene->session_epoch != snapshot->session_epoch || scene->world_epoch != snapshot->world_epoch));
    if (world_changed) {
        for (i = 0; i < scene->ambient_count; ++i) {
            ambient = maage_sound_ambient_at(scene->definitions, i);
            if (ambient->state_bytes) memset(scene->ambient[i].state, 0, ambient->state_bytes);
            scene->ambient[i].next_update = now;
        }
        scene->has_world = query.has_world;
        scene->session_epoch = snapshot ? snapshot->session_epoch : 0;
        scene->world_epoch = snapshot ? snapshot->world_epoch : 0;
    }
    if (!scene->paused) {
        sink.context = scene;
        sink.play = sc_ambient_play;
        scene->effect_tick = query.tick;
        for (i = 0; i < scene->ambient_count; ++i) {
            if (now < scene->ambient[i].next_update) continue;
            ambient = maage_sound_ambient_at(scene->definitions, i);
            scene->ambient[i].next_update = now + ambient->interval;
            result = ambient->update(ambient->context, scene->ambient[i].state, &query, &sink, trace);
            if (maage_status_failed(result)) return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_SNAPSHOT);
        }
    }
    sc_music_update(scene, now, trace);
    return sc_result(scene->failure_count != failures ? MAAGE_STATUS_PARTIAL : MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_UPDATE);
}
maage_status maage_client_audio_scene_pause(maage_client_audio_scene *scene, int paused) {
    if (!scene) return sc_result(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_SET);
    if (scene->paused == (paused != 0)) return sc_result(MAAGE_STATUS_NOOP, MAAGE_OPERATION_SET);
    scene->paused = paused != 0;
    return sc_result(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_SET);
}
maage_status maage_client_audio_scene_music_pause(maage_client_audio_scene *scene, int paused) {
    double now;
    maage_status result;
    if (!scene) return sc_result(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_SET);
    paused = paused != 0;
    if (paused == scene->music_paused) return sc_result(MAAGE_STATUS_NOOP, MAAGE_OPERATION_SET);
    if (scene->bound && scene->music_voice) {
        result = scene->ports.voice_pause(scene->context, scene->audio, scene->music_voice, paused);
        if (maage_status_failed(result)) return result;
    }
    now = maage_time_monotonic_seconds();
    if (paused) scene->music_paused_since = now >= 0 ? now : scene->last_now;
    else if (scene->clock_ready && now >= scene->music_paused_since) scene->next_music += now - scene->music_paused_since;
    scene->music_paused = paused;
    return sc_result(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_SET);
}
maage_status maage_client_audio_scene_music_enabled(maage_client_audio_scene *scene, int enabled) {
    const maage_sound_music_definition *music;
    maage_status result;
    if (!scene) return sc_result(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_SET);
    enabled = enabled != 0;
    if (enabled == scene->music_enabled) return sc_result(MAAGE_STATUS_NOOP, MAAGE_OPERATION_SET);
    scene->music_enabled = enabled;
    if (!enabled && scene->bound) {
        result = sc_stop_music(scene);
        if (maage_status_failed(result)) return result;
        music = maage_sound_music_get(scene->definitions, scene->music_mode);
        if (scene->clock_ready && music) scene->next_music = scene->last_now + sc_delay(scene, music->minimum_delay, music->maximum_delay);
    }
    return sc_result(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_SET);
}
maage_status maage_client_audio_scene_destroy(maage_client_audio_scene *scene) {
    maage_status result;
    size_t i;
    if (!scene) return sc_result(MAAGE_STATUS_NOOP, MAAGE_OPERATION_DESTROY);
    if (scene->bound) {
        result = sc_stop_music(scene);
        if (maage_status_failed(result)) return result;
    }
    if (scene->ambient)
        for (i = 0; i < scene->ambient_count; ++i)
            free(scene->ambient[i].state);
    free(scene->ambient);
    free(scene->sounds);
    free(scene->available);
    free(scene->disabled);
    free(scene->logged);
    free(scene->event_logged);
    free(scene);
    return sc_result(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_DESTROY);
}
