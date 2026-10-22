#include "audio_scene.h"
#include "maage/log.h"
#include "maage/time.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

/*
 * Asset paths are resolved ONCE at bind time. These are the reconstructed
 * Minecraft 1.7.10 index paths (not Java sound-event string comparisons).
 * The separate catalog/bank adapter decides whether any optional asset exists.
 */
enum {
    SC_RAIN_BEGIN = 0, SC_RAIN_END = 4,
    SC_THUNDER_BEGIN = 4, SC_THUNDER_END = 7,
    SC_WATER = 7, SC_LAVA = 8, SC_LAVA_POP = 9,
    SC_CAVE_BEGIN = 10, SC_CAVE_END = 18,
    SC_MUSIC_BEGIN = 18, SC_MUSIC_END = 30,
    SC_ASSET_COUNT = 30,
    SC_CHANNEL_RAIN = 0, SC_CHANNEL_WATER = 1,
    SC_CHANNEL_LAVA = 2, SC_CHANNEL_CAVE = 3,
    SC_CHANNEL_THUNDER = 4, SC_CHANNEL_COUNT = 5
};
static const char *const sc_assets[SC_ASSET_COUNT] = {
    "minecraft/sounds/ambient/weather/rain1.ogg",
    "minecraft/sounds/ambient/weather/rain2.ogg",
    "minecraft/sounds/ambient/weather/rain3.ogg",
    "minecraft/sounds/ambient/weather/rain4.ogg",
    "minecraft/sounds/ambient/weather/thunder1.ogg",
    "minecraft/sounds/ambient/weather/thunder2.ogg",
    "minecraft/sounds/ambient/weather/thunder3.ogg",
    "minecraft/sounds/liquid/water.ogg",
    "minecraft/sounds/liquid/lava.ogg",
    "minecraft/sounds/liquid/lavapop.ogg",
    "minecraft/sounds/ambient/cave/cave1.ogg",
    "minecraft/sounds/ambient/cave/cave2.ogg",
    "minecraft/sounds/ambient/cave/cave3.ogg",
    "minecraft/sounds/ambient/cave/cave4.ogg",
    "minecraft/sounds/ambient/cave/cave5.ogg",
    "minecraft/sounds/ambient/cave/cave6.ogg",
    "minecraft/sounds/ambient/cave/cave7.ogg",
    "minecraft/sounds/ambient/cave/cave8.ogg",
    "minecraft/sounds/music/game/calm1.ogg",
    "minecraft/sounds/music/game/calm2.ogg",
    "minecraft/sounds/music/game/calm3.ogg",
    "minecraft/sounds/music/game/hal1.ogg",
    "minecraft/sounds/music/game/hal2.ogg",
    "minecraft/sounds/music/game/hal3.ogg",
    "minecraft/sounds/music/game/hal4.ogg",
    "minecraft/sounds/music/game/nuance1.ogg",
    "minecraft/sounds/music/game/nuance2.ogg",
    "minecraft/sounds/music/game/piano1.ogg",
    "minecraft/sounds/music/game/piano2.ogg",
    "minecraft/sounds/music/game/piano3.ogg"
};
typedef struct sc_channel {
    maage_client_audio_scene_voice voice;
    double gain, applied_gain, next_at, position[3];
    int source, looping;
} sc_channel;
struct maage_client_audio_scene {
    const void *catalog;
    void *audio;
    maage_client_audio_scene_ports ports;
    void *context;
    sc_channel channels[SC_CHANNEL_COUNT];
    maage_client_audio_scene_sound_id sounds[SC_ASSET_COUNT];
    unsigned char available[SC_ASSET_COUNT], disabled[SC_ASSET_COUNT];
    unsigned char logged[SC_ASSET_COUNT];
    unsigned long random;
    maage_client_audio_scene_voice music_voice;
    void *music_pcm;
    size_t music_bytes;
    int music_index, last_music_index, music_enabled;
    int bound, paused, clock_ready, failure_logged;
    unsigned long failure_count;
    double last_now, paused_since, next_music, next_thunder, next_cave;
};

/* C90-safe, reproducible 32-bit random arithmetic on either 32/64-bit ulong. */
static unsigned long sc_random(maage_client_audio_scene *scene)
{
    scene->random = (scene->random * 1664525UL + 1013904223UL) & 0xffffffffUL;
    return scene->random;
}
static double sc_unit(maage_client_audio_scene *scene)
{ return (double)(sc_random(scene) & 0xffffffUL) / 16777216.0; }
static double sc_span(maage_client_audio_scene *scene, double min, double max)
{ return min + (max - min) * sc_unit(scene); }
static double sc_clamp(double value)
{
    if (!(value >= 0.0)) return 0.0; /* Reject NaN as well. */
    if (value > 1.0) return 1.0;
    return value;
}
static int sc_valid_coordinate(double x)
{ return x == x && x > -30000000.0 && x < 30000000.0; }
static maage_status sc_result(unsigned int status, unsigned int operation)
{ return maage_status_make((unsigned char)status, MAAGE_CONTEXT_CLIENT, (unsigned char)operation, 0); }

/* Log only once for each immutable resource generation (the scene's lifetime).
 * Preserve the original result, not an invented success from recovery. */
static void sc_resource_failed(maage_client_audio_scene *scene, int index,
    maage_status result, maage_status_trace *trace)
{
    if (index < 0 || index >= SC_ASSET_COUNT) return;
    scene->disabled[index] = 1;
    if (scene->logged[index]) return;
    ++scene->failure_count;
    scene->logged[index] = 1;
    MAAGE_STATUS_TRACE(trace, result, MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_LOAD);
    maage_log_trace_status(MAAGE_LOG_WARNING, "maage.client.audio_scene",
        result, trace, "Optional 1.7.10 sound unavailable: %s; this sound is disabled until scene recreation",
        sc_assets[index]);
}
static int sc_select(maage_client_audio_scene *scene, int first, int end, int avoid)
{
    int size, start, i, index, fallback = -1;
    size = end - first;
    if (size <= 0) return -1;
    start = (int)(sc_random(scene) % (unsigned long)size);
    for (i = 0; i < size; ++i) {
        index = first + (start + i) % size;
        if (!scene->available[index] || scene->disabled[index]) continue;
        if (index != avoid) return index;
        fallback = index;
    }
    return fallback;
}
static maage_status sc_stop_channel(maage_client_audio_scene *scene, int channel)
{
    sc_channel *c = &scene->channels[channel];
    maage_status r;
    if (!c->voice) return sc_result(MAAGE_STATUS_NOOP, MAAGE_OPERATION_STOP);
    r = scene->ports.voice_stop(scene->context, scene->audio, c->voice);
    c->voice = 0; c->looping = 0; c->source = -1;
    return r;
}
static maage_status sc_stop_music(maage_client_audio_scene *scene)
{
    maage_status result = sc_result(MAAGE_STATUS_NOOP, MAAGE_OPERATION_STOP);
    if (scene->music_voice) {
        result = scene->ports.voice_stop(scene->context, scene->audio, scene->music_voice);
        scene->music_voice = 0;
    }
    /* PCM is held until the mixer no longer references it. */
    if (scene->music_pcm) {
        scene->ports.music_release(scene->context, scene->music_pcm);
        scene->music_pcm = NULL; scene->music_bytes = 0;
    }
    scene->music_index = -1;
    return result;
}
static void sc_record_device_failure(maage_client_audio_scene *scene, maage_status result,
    maage_status_trace *trace)
{
    if (scene->failure_logged) return;
    scene->failure_logged = 1;
    ++scene->failure_count;
    MAAGE_STATUS_TRACE(trace, result, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_UPDATE);
    maage_log_trace_status(MAAGE_LOG_WARNING, "maage.client.audio_scene", result, trace,
        "Optional ambient voice operation failed; environmental audio will continue");
}
/* The backend must report completion of one-shots, not keep phantom voices. */
static void sc_reap(maage_client_audio_scene *scene, int channel)
{
    sc_channel *c = &scene->channels[channel];
    if (c->voice && !scene->ports.voice_running(scene->context, scene->audio, c->voice))
        (void)sc_stop_channel(scene, channel);
}
static void sc_start_channel(maage_client_audio_scene *scene, int channel,
    int begin, int end, unsigned int category, double gain, const double *position,
    int allow_loop, double now, double minimum, double maximum, maage_status_trace *trace)
{
    sc_channel *c = &scene->channels[channel];
    maage_client_audio_scene_voice voice = 0;
    maage_status r;
    int index, loop = 0;
    if (c->voice || gain < 0.025 || now < c->next_at) return;
    index = sc_select(scene, begin, end, c->source);
    if (index < 0) { c->next_at = now + 30.0; return; }
    if (allow_loop && scene->ports.sound_can_loop)
        loop = scene->ports.sound_can_loop(scene->context, scene->catalog, scene->sounds[index]) ? 1 : 0;
    r = scene->ports.play_sound(scene->context, scene->audio,
        scene->sounds[index], category, position, gain, loop, &voice);
    c->next_at = now + sc_span(scene, minimum, maximum);
    if (maage_status_failed(r) || !voice) {
        if (voice) (void)scene->ports.voice_stop(scene->context, scene->audio, voice);
        if (!maage_status_failed(r)) r = sc_result(MAAGE_STATUS_INVALID_STATE, MAAGE_OPERATION_START);
        sc_resource_failed(scene, index, r, trace);
        return;
    }
    c->voice = voice; c->source = index; c->looping = loop;
    c->applied_gain = gain;
}
static void sc_steady(maage_client_audio_scene *scene, int channel,
    int begin, int end, unsigned int category, double desired,
    const double *position, double dt, double now, double minimum,
    double maximum, maage_status_trace *trace)
{
    sc_channel *c = &scene->channels[channel];
    maage_status r;
    double mix;
    desired = sc_clamp(desired);
    mix = dt / (0.35 + dt);
    c->gain += (desired - c->gain) * mix;
    if (c->gain < 0.001) c->gain = 0.0;
    sc_reap(scene, channel);
    if (c->voice && desired == 0.0 && c->gain < 0.02)
        (void)sc_stop_channel(scene, channel);
    if (c->voice && fabs(c->gain - c->applied_gain) >= 0.015) {
        r = scene->ports.voice_gain(scene->context, scene->audio, c->voice, c->gain);
        if (maage_status_failed(r)) {
            sc_record_device_failure(scene, r, trace);
            (void)sc_stop_channel(scene, channel);
        } else c->applied_gain = c->gain;
    }
    if (!c->voice && desired >= 0.025)
        sc_start_channel(scene, channel, begin, end, category, c->gain,
            position, 1, now, minimum, maximum, trace);
}
static void sc_music_update(maage_client_audio_scene *scene, double now,
    maage_status_trace *trace)
{
    maage_client_audio_scene_voice voice = 0;
    void *pcm = NULL;
    size_t bytes = 0;
    maage_status r;
    int selected;
    if (scene->music_voice) {
        if (scene->ports.voice_running(scene->context, scene->audio, scene->music_voice)) return;
        (void)sc_stop_music(scene);
        scene->next_music = now + sc_span(scene, 120.0, 360.0);
    }
    if (!scene->music_enabled || now < scene->next_music) return;
    /* Exactly one choice/prepare at a scheduling boundary, never per frame.
     * The adapter performs sound-bank acquisition outside the device callback. */
    selected = sc_select(scene, SC_MUSIC_BEGIN, SC_MUSIC_END, scene->last_music_index);
    if (selected < 0) { scene->next_music = now + 600.0; return; }
    r = scene->ports.music_prepare(scene->context, scene->catalog,
        scene->sounds[selected], &pcm, &bytes);
    if (!maage_status_failed(r) && (!pcm || !bytes || bytes > MAAGE_AUDIO_SCENE_MAX_PCM_BYTES))
        r = sc_result(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_OPERATION_DECODE);
    if (!maage_status_failed(r))
        r = scene->ports.music_play(scene->context, scene->audio, pcm, 1.0, &voice);
    if (maage_status_failed(r) || !voice) {
        if (voice) (void)scene->ports.voice_stop(scene->context, scene->audio, voice);
        if (pcm) scene->ports.music_release(scene->context, pcm);
        if (!maage_status_failed(r)) r = sc_result(MAAGE_STATUS_INVALID_STATE, MAAGE_OPERATION_START);
        sc_resource_failed(scene, selected, r, trace);
        scene->next_music = now + 7.0; /* Retry a different track; never tight-loop. */
        return;
    }
    scene->music_voice = voice; scene->music_pcm = pcm; scene->music_bytes = bytes;
    scene->music_index = scene->last_music_index = selected;
    scene->next_music = 0.0; /* Re-armed only after completion. */
}
maage_status maage_client_audio_scene_create(const void *catalog, void *audio,
    maage_client_audio_scene **output, maage_status_trace *trace)
{
    maage_client_audio_scene *scene;
    unsigned int i;
    if (output) *output = NULL;
    if (!catalog || !audio || !output)
        return MAAGE_STATUS_PROPAGATE(trace, sc_result(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_CREATE),
            MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_CREATE);
    scene = (maage_client_audio_scene *)calloc(1, sizeof(*scene));
    if (!scene)
        return MAAGE_STATUS_PROPAGATE(trace, sc_result(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_CREATE),
            MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_CREATE);
    scene->catalog = catalog; scene->audio = audio;
    scene->music_index = scene->last_music_index = -1; scene->music_enabled = 1;
    scene->random = 0x7f4a7c15UL;
    for (i = 0; i < SC_CHANNEL_COUNT; ++i) scene->channels[i].source = -1;
    *output = scene;
    return sc_result(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_CREATE);
}
maage_status maage_client_audio_scene_bind(maage_client_audio_scene *scene,
    const maage_client_audio_scene_ports *ports, void *context, maage_status_trace *trace)
{
    maage_status r, first = sc_result(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_RESOLVE);
    int i, missing = 0;
    if (!scene || !ports || !ports->read_snapshot || !ports->resolve_sound ||
        !ports->play_sound || !ports->music_prepare || !ports->music_play ||
        !ports->music_release || !ports->voice_running || !ports->voice_gain ||
        !ports->voice_pause || !ports->voice_stop)
        return MAAGE_STATUS_PROPAGATE(trace, sc_result(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_INITIALIZE),
            MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_INITIALIZE);
    if (scene->bound) return sc_result(MAAGE_STATUS_INVALID_STATE, MAAGE_OPERATION_INITIALIZE);
    scene->ports = *ports; scene->context = context;
    scene->bound = 1;
    for (i = 0; i < SC_ASSET_COUNT; ++i) {
        r = ports->resolve_sound(context, scene->catalog, sc_assets[i], &scene->sounds[i]);
        if (maage_status_failed(r)) {
            if (!missing) first = r;
            ++missing; sc_resource_failed(scene, i, r, trace);
        } else scene->available[i] = 1;
    }
    /* Missing assets are optional; binding still returns a usable scene. */
    if (missing) {
        if (trace) (void)maage_status_trace_recover(trace,
            sc_result(MAAGE_STATUS_PARTIAL, MAAGE_OPERATION_INITIALIZE),
            MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_INITIALIZE, __FILE__, (unsigned long)__LINE__);
        (void)first;
        return sc_result(MAAGE_STATUS_PARTIAL, MAAGE_OPERATION_INITIALIZE);
    }
    return sc_result(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_INITIALIZE);
}
maage_status maage_client_audio_scene_update(maage_client_audio_scene *scene,
    const maage_session_snapshot *snapshot, const double listener_position[3],
    double now, maage_status_trace *trace)
{
    maage_client_audio_scene_observation obs;
    maage_status r;
    double dt, rain, water, lava, radius, bearing, thunder_at[3], cave_at[3];
    unsigned int i;
    unsigned long failures_before;
    sc_channel *c;
    int index;
    if (!scene || !snapshot || !listener_position || !scene->bound ||
        !(now >= 0.0) || now > 1.0e15)
        return MAAGE_STATUS_PROPAGATE(trace, sc_result(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_UPDATE),
            MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_UPDATE);
    for (i = 0; i < 3; ++i)
        if (!sc_valid_coordinate(listener_position[i]))
            return sc_result(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_UPDATE);
    if (scene->paused) return sc_result(MAAGE_STATUS_NOOP, MAAGE_OPERATION_UPDATE);
    failures_before = scene->failure_count;
    if (!scene->clock_ready) {
        scene->clock_ready = 1; scene->last_now = now;
        scene->random ^= ((unsigned long)fmod(now * 1000.0, 4294967296.0)) & 0xffffffffUL;
        scene->next_music = now + sc_span(scene, 12.0, 40.0);
        scene->next_thunder = now + sc_span(scene, 9.0, 27.0);
        scene->next_cave = now + sc_span(scene, 45.0, 130.0);
    }
    if (now < scene->last_now)
        return MAAGE_STATUS_PROPAGATE(trace, sc_result(MAAGE_STATUS_INVALID_STATE, MAAGE_OPERATION_UPDATE),
            MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_UPDATE);
    dt = now - scene->last_now; scene->last_now = now;
    if (dt > 1.0) dt = 1.0; /* Do not jump the envelope after a frame stall. */
    memset(&obs, 0, sizeof(obs));
    r = scene->ports.read_snapshot(scene->context, snapshot, listener_position, &obs);
    if (maage_status_failed(r)) return MAAGE_STATUS_PROPAGATE(trace, r, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_SNAPSHOT);
    rain = sc_clamp(obs.rain_intensity);
    if (obs.biome_rainfall <= 0.01 || obs.biome_temperature < 0.15) rain = 0.0;
    else if (obs.sky_exposed) rain *= 0.85;
    else if (obs.under_cover) rain *= 0.23;
    else rain *= 0.06;
    sc_steady(scene, SC_CHANNEL_RAIN, SC_RAIN_BEGIN, SC_RAIN_END,
        MAAGE_AUDIO_SCENE_WEATHER, rain, NULL, dt, now, 1.2, 2.8, trace);
    water = sc_clamp(obs.water_strength) * 0.62;
    lava = sc_clamp(obs.lava_strength) * 0.67;
    for (i = 0; i < 3; ++i) {
        if (!sc_valid_coordinate(obs.water_position[i])) obs.water_position[i] = listener_position[i];
        if (!sc_valid_coordinate(obs.lava_position[i])) obs.lava_position[i] = listener_position[i];
    }
    sc_steady(scene, SC_CHANNEL_WATER, SC_WATER, SC_WATER + 1,
        MAAGE_AUDIO_SCENE_AMBIENT, water, obs.water_position,
        dt, now, 5.0, 10.0, trace);
    sc_steady(scene, SC_CHANNEL_LAVA, SC_LAVA, SC_LAVA_POP + 1,
        MAAGE_AUDIO_SCENE_AMBIENT, lava, obs.lava_position,
        dt, now, 7.0, 17.0, trace);

    c = &scene->channels[SC_CHANNEL_THUNDER];
    sc_reap(scene, SC_CHANNEL_THUNDER);
    if (now >= scene->next_thunder) {
        scene->next_thunder = now + sc_span(scene, 19.0, 54.0);
        if (sc_clamp(obs.thunder_intensity) >= 0.25 && !c->voice) {
            radius = sc_span(scene, 18.0, 68.0);
            bearing = sc_span(scene, 0.0, 6.283185307179586);
            thunder_at[0] = listener_position[0] + cos(bearing) * radius;
            thunder_at[1] = listener_position[1] + sc_span(scene, 15.0, 35.0);
            thunder_at[2] = listener_position[2] + sin(bearing) * radius;
            sc_start_channel(scene, SC_CHANNEL_THUNDER, SC_THUNDER_BEGIN,
                SC_THUNDER_END, MAAGE_AUDIO_SCENE_WEATHER,
                0.6 + 0.4 * sc_clamp(obs.thunder_intensity), thunder_at,
                0, now, 18.0, 44.0, trace);
        }
    }
    c = &scene->channels[SC_CHANNEL_CAVE];
    sc_reap(scene, SC_CHANNEL_CAVE);
    if (now >= scene->next_cave) {
        scene->next_cave = now + sc_span(scene, 55.0, 195.0);
        if (!c->voice && obs.light_available && !obs.sky_exposed &&
            sc_clamp(obs.cave_darkness) > 0.45) {
            for (i = 0; i < 3; ++i)
                cave_at[i] = listener_position[i] + sc_span(scene, -12.0, 12.0);
            index = sc_select(scene, SC_CAVE_BEGIN, SC_CAVE_END, c->source);
            if (index >= 0) {
                maage_client_audio_scene_voice voice = 0;
                r = scene->ports.play_sound(scene->context, scene->audio,
                    scene->sounds[index], MAAGE_AUDIO_SCENE_AMBIENT,
                    cave_at, 0.25 + 0.32 * sc_clamp(obs.cave_darkness), 0, &voice);
                if (maage_status_failed(r) || !voice) {
                    if (voice) (void)scene->ports.voice_stop(scene->context, scene->audio, voice);
                    if (!maage_status_failed(r)) r = sc_result(MAAGE_STATUS_INVALID_STATE, MAAGE_OPERATION_START);
                    sc_resource_failed(scene, index, r, trace);
                } else { c->voice = voice; c->source = index; }
            }
        }
    }
    sc_music_update(scene, now, trace);
    return sc_result(scene->failure_count != failures_before ? MAAGE_STATUS_PARTIAL : MAAGE_STATUS_SUCCESS,
        MAAGE_OPERATION_UPDATE);
}
maage_status maage_client_audio_scene_pause(maage_client_audio_scene *scene, int paused)
{
    double now, shift;
    unsigned int i;
    maage_status r, first = sc_result(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_SET);
    if (!scene) return sc_result(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_SET);
    paused = paused != 0;
    if (paused == scene->paused) return sc_result(MAAGE_STATUS_NOOP, MAAGE_OPERATION_SET);
    now = maage_time_monotonic_seconds();
    if (paused) scene->paused_since = now >= 0.0 ? now : scene->last_now;
    else if (scene->clock_ready) {
        shift = now >= scene->paused_since ? now - scene->paused_since : 0.0;
        scene->next_music += shift; scene->next_thunder += shift; scene->next_cave += shift;
        for (i = 0; i < SC_CHANNEL_COUNT; ++i) scene->channels[i].next_at += shift;
        scene->last_now = now >= 0.0 ? now : scene->last_now;
    }
    scene->paused = paused;
    if (scene->bound) {
        for (i = 0; i < SC_CHANNEL_COUNT; ++i) if (scene->channels[i].voice) {
            r = scene->ports.voice_pause(scene->context, scene->audio, scene->channels[i].voice, paused);
            if (maage_status_failed(r) && !maage_status_failed(first)) first = r;
        }
        if (scene->music_voice) {
            r = scene->ports.voice_pause(scene->context, scene->audio, scene->music_voice, paused);
            if (maage_status_failed(r) && !maage_status_failed(first)) first = r;
        }
    }
    return first;
}
maage_status maage_client_audio_scene_music_enabled(maage_client_audio_scene *scene,
    int enabled)
{
    if (!scene) return sc_result(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_SET);
    enabled = enabled != 0;
    if (enabled == scene->music_enabled) return sc_result(MAAGE_STATUS_NOOP, MAAGE_OPERATION_SET);
    scene->music_enabled = enabled;
    if (!enabled && scene->bound) {
        maage_status r = sc_stop_music(scene);
        return maage_status_failed(r) ? r : sc_result(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_SET);
    }
    if (enabled && scene->clock_ready) scene->next_music = scene->last_now + sc_span(scene, 5.0, 15.0);
    return sc_result(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_SET);
}
maage_status maage_client_audio_scene_destroy(maage_client_audio_scene *scene)
{
    unsigned int i;
    maage_status r, first = sc_result(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_DESTROY);
    if (!scene) return sc_result(MAAGE_STATUS_NOOP, MAAGE_OPERATION_DESTROY);
    if (scene->bound) {
        for (i = 0; i < SC_CHANNEL_COUNT; ++i) {
            r = sc_stop_channel(scene, (int)i);
            if (maage_status_failed(r) && !maage_status_failed(first)) first = r;
        }
        r = sc_stop_music(scene);
        if (maage_status_failed(r) && !maage_status_failed(first)) first = r;
    }
    free(scene);
    return first;
}
