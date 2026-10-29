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
    SC_RAIN_BEGIN = 0,
    SC_RAIN_END = 4,
    SC_WATER = 4,
    SC_LAVA = 5,
    SC_LAVA_POP = 6,
    SC_MUSIC_BEGIN = 7,
    SC_MUSIC_END = 19,
    SC_CREATIVE_END = 25,
    SC_MENU_BEGIN = 25,
    SC_MENU_END = 29,
    SC_ASSET_COUNT = 36,
    SC_CHANNEL_COUNT = 16,
    SC_MUSIC_MENU = 0,
    SC_MUSIC_GAME = 1,
    SC_MUSIC_CREATIVE = 2
};
static const char *const sc_assets[SC_ASSET_COUNT] = {
    "minecraft/sounds/ambient/weather/rain1.ogg",
    "minecraft/sounds/ambient/weather/rain2.ogg",
    "minecraft/sounds/ambient/weather/rain3.ogg",
    "minecraft/sounds/ambient/weather/rain4.ogg",
    "minecraft/sounds/liquid/water.ogg",
    "minecraft/sounds/liquid/lava.ogg",
    "minecraft/sounds/liquid/lavapop.ogg",
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
    "minecraft/sounds/music/game/piano3.ogg",
    "minecraft/sounds/music/game/creative/creative1.ogg",
    "minecraft/sounds/music/game/creative/creative2.ogg",
    "minecraft/sounds/music/game/creative/creative3.ogg",
    "minecraft/sounds/music/game/creative/creative4.ogg",
    "minecraft/sounds/music/game/creative/creative5.ogg",
    "minecraft/sounds/music/game/creative/creative6.ogg",
    "minecraft/sounds/music/menu/menu1.ogg",
    "minecraft/sounds/music/menu/menu2.ogg",
    "minecraft/sounds/music/menu/menu3.ogg",
    "minecraft/sounds/music/menu/menu4.ogg",
    "minecraft/sounds/music/game/nether/nether1.ogg",
    "minecraft/sounds/music/game/nether/nether2.ogg",
    "minecraft/sounds/music/game/nether/nether3.ogg",
    "minecraft/sounds/music/game/nether/nether4.ogg",
    "minecraft/sounds/music/game/end/end.ogg",
    "minecraft/sounds/music/game/end/credits.ogg",
    "minecraft/sounds/music/game/end/boss.ogg"};
typedef struct sc_channel {
    maage_client_audio_scene_voice voice;
    int source;
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
    int music_index, music_enabled, music_paused;
    int bound, paused, clock_ready, failure_logged, music_mode, has_world;
    unsigned long failure_count;
    double last_now, next_music, music_paused_since;
    unsigned long session_epoch, world_epoch, cave_ticks, effect_sequence;
    double next_observation;
    unsigned int rain_counter;
};

/* C90-safe, reproducible 32-bit random arithmetic on either 32/64-bit ulong. */
static unsigned long sc_random(maage_client_audio_scene *scene) {
    scene->random = (scene->random * 1664525UL + 1013904223UL) & 0xffffffffUL;
    return scene->random;
}
static unsigned long sc_bounded(maage_client_audio_scene *scene, unsigned long bound) {
    return (sc_random(scene) >> 8) % bound;
}
static double sc_unit(maage_client_audio_scene *scene) {
    return (double) (sc_random(scene) & 0xffffffUL) / 16777216.0;
}
static double sc_span(maage_client_audio_scene *scene, double min, double max) {
    return min + (max - min) * sc_unit(scene);
}
static int sc_valid_coordinate(double x) {
    return x == x && x > -30000000.0 && x < 30000000.0;
}
static maage_status sc_result(unsigned int status, unsigned int operation) {
    return maage_status_make((unsigned char) status, MAAGE_CONTEXT_CLIENT, (unsigned char) operation, 0);
}
/* Voice-capacity and allocation pressure are transient, not evidence that
 * a registered sound resource is corrupt or missing. */
static int sc_transient_pressure(maage_status result) {
    unsigned int code = maage_status_code(result);
    return code == MAAGE_STATUS_BUFFER_OVERFLOW || code == MAAGE_STATUS_ALLOCATION_FAILED || code == MAAGE_STATUS_TIMEOUT;
}

/* Log only once for each immutable resource generation (the scene's lifetime).
 * Preserve the original result, not an invented success from recovery. */
static void sc_resource_failed(maage_client_audio_scene *scene, int index, maage_status result, maage_status_trace *trace) {
    if (index < 0 || index >= SC_ASSET_COUNT) return;
    scene->disabled[index] = 1;
    if (scene->logged[index]) return;
    ++scene->failure_count;
    scene->logged[index] = 1;
    MAAGE_STATUS_TRACE(trace, result, MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_LOAD);
    maage_log_trace_status(MAAGE_LOG_WARNING, "maage.client.audio_scene", result, trace, "Optional 1.7.10 sound unavailable: %s; this sound is disabled until scene recreation", sc_assets[index]);
}
static int sc_select(maage_client_audio_scene *scene, int first, int end, int avoid) {
    int size, start, i, index, fallback = -1;
    size = end - first;
    if (size <= 0) return -1;
    start = (int) (sc_random(scene) % (unsigned long) size);
    for (i = 0; i < size; ++i) {
        index = first + (start + i) % size;
        if (!scene->available[index] || scene->disabled[index]) continue;
        if (index != avoid) return index;
        fallback = index;
    }
    return fallback;
}
static maage_status sc_stop_channel(maage_client_audio_scene *scene, int channel) {
    sc_channel *c = &scene->channels[channel];
    maage_status r;
    if (!c->voice) return sc_result(MAAGE_STATUS_NOOP, MAAGE_OPERATION_STOP);
    r = scene->ports.voice_stop(scene->context, scene->audio, c->voice);
    if (!maage_status_failed(r)) {
        c->voice = 0;
        c->source = -1;
    }
    return r;
}
static maage_status sc_stop_music(maage_client_audio_scene *scene) {
    maage_status result = sc_result(MAAGE_STATUS_NOOP, MAAGE_OPERATION_STOP);
    if (scene->music_voice) {
        result = scene->ports.voice_stop(scene->context, scene->audio, scene->music_voice);
        if (maage_status_failed(result)) return result;
        scene->music_voice = 0;
    }
    /* PCM is held until the mixer no longer references it. */
    if (scene->music_pcm) {
        scene->ports.music_release(scene->context, scene->music_pcm);
        scene->music_pcm = NULL;
        scene->music_bytes = 0;
    }
    scene->music_index = -1;
    return result;
}
static void sc_record_device_failure(maage_client_audio_scene *scene, maage_status result, maage_status_trace *trace) {
    if (scene->failure_logged) return;
    scene->failure_logged = 1;
    ++scene->failure_count;
    MAAGE_STATUS_TRACE(trace, result, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_UPDATE);
    maage_log_trace_status(MAAGE_LOG_WARNING, "maage.client.audio_scene", result, trace, "Optional ambient voice operation failed; environmental audio will continue");
}
/* The backend must report completion of one-shots, not keep phantom voices. */
static void sc_reap(maage_client_audio_scene *scene, int channel) {
    sc_channel *c = &scene->channels[channel];
    if (c->voice && !scene->ports.voice_running(scene->context, scene->audio, c->voice)) (void) sc_stop_channel(scene, channel);
}
/* One-shots may overlap, but never accumulate a queue after a Client stall. */
static void sc_effect(maage_client_audio_scene *scene, int begin, int end, unsigned int category, double gain, double pitch, const double position[3], maage_status_trace *trace) {
    sc_channel *channel = NULL;
    maage_client_audio_scene_voice voice = 0;
    maage_status result;
    unsigned int i;
    int index;
    for (i = 0; i < SC_CHANNEL_COUNT; ++i) {
        sc_reap(scene, (int) i);
        if (!scene->channels[i].voice && !channel) channel = &scene->channels[i];
    }
    if (!channel) return;
    index = sc_select(scene, begin, end, -1);
    if (index < 0) return;
    result = scene->ports.play_sound(scene->context, scene->audio, scene->sounds[index], category, position, gain, pitch, 0, &voice);
    if (voice) {
        channel->voice = voice;
        channel->source = index;
    }
    if (maage_status_failed(result) || !voice) {
        if (!maage_status_failed(result)) result = sc_result(MAAGE_STATUS_INVALID_STATE, MAAGE_OPERATION_START);
        if (!sc_transient_pressure(result)) sc_resource_failed(scene, index, result, trace);
    }
}
static double sc_music_minimum(const maage_client_audio_scene *scene) {
    return scene->music_mode == SC_MUSIC_MENU ? 1.0 : (scene->music_mode == SC_MUSIC_CREATIVE ? 60.0 : 600.0);
}
static double sc_music_maximum(const maage_client_audio_scene *scene) {
    return scene->music_mode == SC_MUSIC_MENU ? 30.0 : (scene->music_mode == SC_MUSIC_CREATIVE ? 180.0 : 1200.0);
}
static double sc_music_delay(maage_client_audio_scene *scene, double minimum, double maximum) {
    unsigned long ticks = (unsigned long) (minimum * 20.0);
    unsigned long span = (unsigned long) ((maximum - minimum) * 20.0) + 1UL;
    return (double) (ticks + ((sc_random(scene) >> 8) % span)) / 20.0;
}
static void sc_music_update(maage_client_audio_scene *scene, double now, maage_status_trace *trace) {
    maage_client_audio_scene_voice voice = 0;
    void *pcm = NULL;
    size_t bytes = 0;
    maage_status r;
    int selected, begin, end;
    if (scene->music_paused) return;
    if (!scene->music_enabled) {
        if (scene->music_voice && maage_status_failed(sc_stop_music(scene))) sc_record_device_failure(scene, sc_result(MAAGE_STATUS_ERROR, MAAGE_OPERATION_STOP), trace);
        return;
    }
    if (scene->music_voice) {
        if (scene->ports.voice_running(scene->context, scene->audio, scene->music_voice)) return;
        r = sc_stop_music(scene);
        if (maage_status_failed(r)) {
            sc_record_device_failure(scene, r, trace);
            return;
        }
        scene->next_music = now + sc_music_delay(scene, sc_music_minimum(scene), sc_music_maximum(scene));
    }
    if (now < scene->next_music) return;
    /* Exactly one choice/prepare at a scheduling boundary, never per frame.
     * The adapter performs sound-bank acquisition outside the device callback. */
    begin = scene->music_mode == SC_MUSIC_MENU ? SC_MENU_BEGIN : SC_MUSIC_BEGIN;
    end = scene->music_mode == SC_MUSIC_MENU ? SC_MENU_END : (scene->music_mode == SC_MUSIC_CREATIVE ? SC_CREATIVE_END : SC_MUSIC_END);
    selected = sc_select(scene, begin, end, -1);
    if (selected < 0) {
        scene->next_music = now + 600.0;
        return;
    }
    r = scene->ports.music_prepare(scene->context, scene->catalog, scene->sounds[selected], MAAGE_AUDIO_SCENE_MAX_PCM_BYTES, &pcm, &bytes);
    if (!maage_status_failed(r) && (!pcm || !bytes || bytes > MAAGE_AUDIO_SCENE_MAX_PCM_BYTES)) r = sc_result(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_OPERATION_DECODE);
    if (!maage_status_failed(r)) r = scene->ports.music_play(scene->context, scene->audio, pcm, 1.0, &voice);
    if (maage_status_failed(r) || !voice) {
        if (voice) {
            maage_status stopped = scene->ports.voice_stop(scene->context, scene->audio, voice);
            if (maage_status_failed(stopped)) {
                /* An uncertain stop may still be reading PCM. Retain both. */
                scene->music_voice = voice;
                scene->music_pcm = pcm;
                scene->music_bytes = bytes;
                scene->music_index = selected;
                sc_record_device_failure(scene, stopped, trace);
                if (!sc_transient_pressure(r)) sc_resource_failed(scene, selected, r, trace);
                return;
            }
        }
        if (pcm) scene->ports.music_release(scene->context, pcm);
        if (!maage_status_failed(r)) r = sc_result(MAAGE_STATUS_INVALID_STATE, MAAGE_OPERATION_START);
        if (!sc_transient_pressure(r)) sc_resource_failed(scene, selected, r, trace);
        scene->next_music = now + 7.0; /* Retry a different track; never tight-loop. */
        return;
    }
    scene->music_voice = voice;
    scene->music_pcm = pcm;
    scene->music_bytes = bytes;
    scene->music_index = selected;
    scene->next_music = 0.0; /* Re-armed only after completion. */
}
maage_status maage_client_audio_scene_create(const void *catalog, void *audio, maage_client_audio_scene **output, maage_status_trace *trace) {
    maage_client_audio_scene *scene;
    unsigned int i;
    if (output) *output = NULL;
    if (!catalog || !audio || !output) return MAAGE_STATUS_PROPAGATE(trace, sc_result(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_CREATE), MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_CREATE);
    scene = (maage_client_audio_scene *) calloc(1, sizeof(*scene));
    if (!scene) return MAAGE_STATUS_PROPAGATE(trace, sc_result(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_CREATE), MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_CREATE);
    scene->catalog = catalog;
    scene->audio = audio;
    scene->music_index = -1;
    scene->music_enabled = 1;
    scene->music_mode = SC_MUSIC_MENU;
    scene->random = 0x7f4a7c15UL;
    for (i = 0; i < SC_CHANNEL_COUNT; ++i)
        scene->channels[i].source = -1;
    *output = scene;
    return sc_result(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_CREATE);
}
maage_status maage_client_audio_scene_bind(maage_client_audio_scene *scene, const maage_client_audio_scene_ports *ports, void *context, maage_status_trace *trace) {
    maage_status r, first = sc_result(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_RESOLVE);
    int i, missing = 0;
    if (!scene || !ports || !ports->play_event || !ports->resolve_sound ||
        !ports->play_sound || !ports->music_prepare || !ports->music_play ||
        !ports->music_release || !ports->voice_running || !ports->voice_gain ||
        !ports->voice_pause || !ports->voice_stop)
        return MAAGE_STATUS_PROPAGATE(trace, sc_result(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_INITIALIZE), MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_INITIALIZE);
    if (scene->bound) return sc_result(MAAGE_STATUS_INVALID_STATE, MAAGE_OPERATION_INITIALIZE);
    scene->ports = *ports;
    scene->context = context;
    scene->bound = 1;
    for (i = 0; i < SC_ASSET_COUNT; ++i) {
        r = ports->resolve_sound(context, scene->catalog, sc_assets[i], &scene->sounds[i]);
        if (maage_status_failed(r)) {
            if (!missing) first = r;
            ++missing;
            sc_resource_failed(scene, i, r, trace);
        } else
            scene->available[i] = 1;
    }
    /* Missing assets are optional; binding still returns a usable scene. */
    if (missing) {
        if (trace) (void) maage_status_trace_recover(trace, sc_result(MAAGE_STATUS_PARTIAL, MAAGE_OPERATION_INITIALIZE), MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_INITIALIZE, __FILE__, (unsigned long) __LINE__);
        (void) first;
        return sc_result(MAAGE_STATUS_PARTIAL, MAAGE_OPERATION_INITIALIZE);
    }
    return sc_result(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_INITIALIZE);
}
static maage_status sc_observe(maage_client_audio_scene *scene, const maage_world *visual, const maage_session_snapshot *snapshot, const double listener[3], maage_client_audio_scene_observation *audio, maage_status_trace *trace) {
    const maage_block_registry *registry = maage_world_registry(visual);
    const maage_block_state_definition *state, *above;
    const maage_block_definition *block;
    maage_world_observation observation, local;
    maage_environment_state environment;
    maage_sound_event sound;
    double position[3], dx, dy, dz, rain;
    int px, py, pz, x, y, z, cx, cz, i, samples;
    unsigned long water_count = 0, lava_count = 0, rain_count = 0;
    memset(audio, 0, sizeof(*audio));
    if (!visual) return sc_result(MAAGE_STATUS_NOOP, MAAGE_OPERATION_SNAPSHOT);
    px = (int) floor(listener[0]);
    py = (int) floor(listener[1] - MAAGE_PLAYER_EYE_HEIGHT);
    pz = (int) floor(listener[2]);
    /* Copy bounded local display trial counts and reservoir-selected
     * environmental positions for Client playback. */
    for (i = 0; i < 1000; ++i) {
        x = px + (int) sc_bounded(scene, 16) - (int) sc_bounded(scene, 16);
        y = py + (int) sc_bounded(scene, 16) - (int) sc_bounded(scene, 16);
        z = pz + (int) sc_bounded(scene, 16) - (int) sc_bounded(scene, 16);
        if (y < 0 || y >= MAAGE_WORLD_HEIGHT) continue;
        state = maage_block_registry_state(registry, maage_world_get(visual, x, y, z));
        if (!state) continue;
        if (state->fluid_kind == MAAGE_FLUID_WATER && state->fluid_level > 0 && state->fluid_level < 8) {
            if (sc_bounded(scene, ++water_count) == 0) {
                audio->flowing_water_available = 1;
                audio->water_position[0] = x + 0.5;
                audio->water_position[1] = y + 0.5;
                audio->water_position[2] = z + 0.5;
            }
        } else if (state->fluid_kind == MAAGE_FLUID_LAVA) {
            above = maage_block_registry_state(registry, maage_world_get(visual, x, y + 1, z));
            if (above && above->block_id == MAAGE_BLOCK_AIR && sc_bounded(scene, ++lava_count) == 0) {
                audio->lava_available = 1;
                audio->lava_position[0] = x;
                audio->lava_position[1] = y;
                audio->lava_position[2] = z;
            }
        }
    }
    audio->water_samples = (unsigned int) water_count;
    audio->lava_samples = (unsigned int) lava_count;
    environment = snapshot->environment;
    audio->rain_intensity = environment.rain_intensity;
    rain = environment.rain_intensity;
    samples = (int) (100.0 * rain * rain);
    memset(&local, 0, sizeof(local));
    (void) maage_world_observe(visual, px, py, pz, &local);
    /* Sample real precipitation surfaces,
     * reject snow/desert/lava/air, and reservoir-select one impact. */
    for (i = 0; i < samples; ++i) {
        x = px + (int) sc_bounded(scene, 10) - (int) sc_bounded(scene, 10);
        z = pz + (int) sc_bounded(scene, 10) - (int) sc_bounded(scene, 10);
        if (!maage_world_observe(visual, x, py, z, &observation)) continue;
        y = observation.precipitation_height;
        if (y < 1 || y >= MAAGE_WORLD_HEIGHT || y < py - 10 || y > py + 10) continue;
        if (!maage_world_observe(visual, x, y, z, &observation) || observation.rainfall <= 0.0 || observation.temperature < 0.15) continue;
        state = maage_block_registry_state(registry, maage_world_get(visual, x, y - 1, z));
        block = state ? maage_block_registry_block(registry, state->block_id) : NULL;
        if (!block || state->block_id == MAAGE_BLOCK_AIR || state->fluid_kind == MAAGE_FLUID_LAVA) continue;
        if (sc_bounded(scene, ++rain_count) != 0) continue;
        audio->rain_available = 1;
        audio->rain_position[0] = x + sc_bounded(scene, 65536) / 65536.0;
        audio->rain_position[1] = y + 0.1 - block->selection_shape.minimum[1];
        audio->rain_position[2] = z + sc_bounded(scene, 65536) / 65536.0;
    }
    audio->rain_above = audio->rain_available && audio->rain_position[1] > listener[1] - MAAGE_PLAYER_EYE_HEIGHT + 1.0 && local.precipitation_height > py;
    /* Cave ambience requires real dark air, 2..8 blocks from the
     * player, and a 6000..17999 tick cooldown. Only nearby columns can pass
     * that distance predicate, so no scan of all loaded World columns. */
    if (scene->cave_ticks) {
        --scene->cave_ticks;
        return sc_result(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_SNAPSHOT);
    }
    for (cx = -1; cx <= 1; ++cx)
        for (cz = -1; cz <= 1; ++cz) {
            x = (maage_world_chunk_coordinate(px) + cx) * 16 + (int) sc_bounded(scene, 16);
            y = (int) sc_bounded(scene, 256);
            z = (maage_world_chunk_coordinate(pz) + cz) * 16 + (int) sc_bounded(scene, 16);
            position[0] = x + 0.5;
            position[1] = y + 0.5;
            position[2] = z + 0.5;
            dx = position[0] - listener[0];
            dy = position[1] - listener[1];
            dz = position[2] - listener[2];
            if (dx * dx + dy * dy + dz * dz <= 4.0 || dx * dx + dy * dy + dz * dz > 64.0) continue;
            if (maage_world_get(visual, x, y, z) != MAAGE_STATE_AIR ||
                !maage_world_observe(visual, x, y, z, &observation) ||
                maage_status_code(maage_world_present_light(visual, x, y, z, &observation.light)) != MAAGE_STATUS_SUCCESS || (observation.light >> 4) != 0 ||
                (observation.light & 15) > sc_bounded(scene, 8)) continue;
            memset(&sound, 0, sizeof(sound));
            sound.event_id = MAAGE_SOUND_CAVE_AMBIENT;
            sound.material = MAAGE_MATERIAL_DEFAULT;
            sound.tick = snapshot->tick;
            sound.sequence = ++scene->effect_sequence;
            memcpy(sound.position, position, sizeof(position));
            sound.intensity = 1;
            sound.pitch = 0.8 + sc_bounded(scene, 65536) / 65536.0 * 0.2;
            scene->cave_ticks = 6000UL + sc_bounded(scene, 12000);
            return scene->ports.play_event((void *) scene->catalog, scene->audio, &sound, trace);
        }
    return sc_result(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_SNAPSHOT);
}

maage_status maage_client_audio_scene_update(maage_client_audio_scene *scene, const maage_session_snapshot *snapshot, const maage_world *visual, const double listener_position[3], double now, maage_status_trace *trace) {
    maage_client_audio_scene_observation obs;
    maage_status r;
    int music_mode, above;
    unsigned int i, sample;
    unsigned long failures_before;

    if (!scene || !listener_position || !scene->bound || !(now >= 0.0) || now > 1.0e15) return MAAGE_STATUS_PROPAGATE(trace, sc_result(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_UPDATE), MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_UPDATE);
    for (i = 0; i < 3; ++i)
        if (!sc_valid_coordinate(listener_position[i]))
            return sc_result(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_UPDATE);
    failures_before = scene->failure_count;
    if (!scene->clock_ready) {
        scene->clock_ready = 1;
        scene->last_now = now;
        scene->random ^= ((unsigned long) fmod(now * 1000.0, 4294967296.0)) & 0xffffffffUL;
        scene->next_music = now + 5.0;
    }
    if (now < scene->last_now) return MAAGE_STATUS_PROPAGATE(trace, sc_result(MAAGE_STATUS_INVALID_STATE, MAAGE_OPERATION_UPDATE), MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_UPDATE);
    scene->last_now = now;
    /* Options/Pause and replacement worlds retain a matching music context. */
    music_mode = !snapshot ? SC_MUSIC_MENU : (snapshot->player.game_mode == MAAGE_GAME_MODE_CREATIVE ? SC_MUSIC_CREATIVE : SC_MUSIC_GAME);
    if (scene->music_mode != music_mode) {
        scene->music_mode = music_mode;
        if (scene->music_voice) {
            r = sc_stop_music(scene);
            if (maage_status_failed(r)) return r;
            scene->next_music = now + sc_music_delay(scene, 0.0, sc_music_minimum(scene) * 0.5);
        }
    }
    if (scene->has_world && (!snapshot ||
                             scene->session_epoch != snapshot->session_epoch ||
                             scene->world_epoch != snapshot->world_epoch)) {
        for (i = 0; i < SC_CHANNEL_COUNT; ++i) {
            r = sc_stop_channel(scene, (int) i);
            if (maage_status_failed(r)) return r;
        }
        scene->has_world = 0;
        scene->rain_counter = 0;
    }
    if (!snapshot) {
        sc_music_update(scene, now, trace);
        return sc_result(scene->failure_count != failures_before ? MAAGE_STATUS_PARTIAL : MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_UPDATE);
    }
    if (!scene->has_world) {
        scene->session_epoch = snapshot->session_epoch;
        scene->world_epoch = snapshot->world_epoch;
        scene->next_observation = now;
        scene->cave_ticks = sc_bounded(scene, 12000);
        scene->has_world = 1;
    }
    /* Cosmetic opportunities use a bounded local cadence, independently of
     * coalesced Server snapshots. Pause never accumulates catch-up work. */
    if (!scene->paused && now >= scene->next_observation) {
        scene->next_observation = now + 0.05;
        r = sc_observe(scene, visual, snapshot, listener_position, &obs, trace);
        if (maage_status_failed(r)) return MAAGE_STATUS_PROPAGATE(trace, r, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_SNAPSHOT);
        if (obs.water_samples > 1000U || obs.lava_samples > 1000U) return sc_result(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_SNAPSHOT);
        if (obs.rain_available && obs.rain_intensity > 0.0 && sc_bounded(scene, 3UL) < scene->rain_counter++) {
            scene->rain_counter = 0;
            above = obs.rain_above;
            sc_effect(scene, SC_RAIN_BEGIN, SC_RAIN_END, MAAGE_AUDIO_SCENE_WEATHER,
                      above ? 0.1 : 0.2, above ? 0.5 : 1.0, obs.rain_position, trace);
        }
        /* Each sampled block has an independent local display trial. */
        if (obs.flowing_water_available)
            for (sample = 0; sample < obs.water_samples; ++sample) {
                if (sc_bounded(scene, 64UL) == 0) sc_effect(scene, SC_WATER, SC_WATER + 1, MAAGE_AUDIO_SCENE_BLOCKS, sc_span(scene, 0.75, 1.0), sc_span(scene, 0.5, 1.5), obs.water_position, trace);
            }
        if (obs.lava_available)
            for (sample = 0; sample < obs.lava_samples; ++sample) {
                if (sc_bounded(scene, 100UL) == 0) sc_effect(scene, SC_LAVA_POP, SC_LAVA_POP + 1, MAAGE_AUDIO_SCENE_BLOCKS, sc_span(scene, 0.2, 0.4), sc_span(scene, 0.9, 1.05), obs.lava_position, trace);
                if (sc_bounded(scene, 200UL) == 0) sc_effect(scene, SC_LAVA, SC_LAVA + 1, MAAGE_AUDIO_SCENE_BLOCKS, sc_span(scene, 0.2, 0.4), sc_span(scene, 0.9, 1.05), obs.lava_position, trace);
            }
    }
    sc_music_update(scene, now, trace);
    return sc_result(scene->failure_count != failures_before ? MAAGE_STATUS_PARTIAL : MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_UPDATE);
}
maage_status maage_client_audio_scene_pause(maage_client_audio_scene *scene, int paused) {
    unsigned int i;
    maage_status r, first = sc_result(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_SET);
    if (!scene) return sc_result(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_SET);
    paused = paused != 0;
    if (paused == scene->paused) return sc_result(MAAGE_STATUS_NOOP, MAAGE_OPERATION_SET);
    scene->paused = paused;
    if (scene->bound) {
        for (i = 0; i < SC_CHANNEL_COUNT; ++i)
            if (scene->channels[i].voice) {
                r = scene->ports.voice_pause(scene->context, scene->audio, scene->channels[i].voice, paused);
                if (maage_status_failed(r) && !maage_status_failed(first)) first = r;
            }
    }
    return first;
}
maage_status maage_client_audio_scene_music_pause(maage_client_audio_scene *scene, int paused) {
    double now;
    maage_status result = sc_result(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_SET);
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
    return result;
}
maage_status maage_client_audio_scene_music_enabled(maage_client_audio_scene *scene, int enabled) {
    if (!scene) return sc_result(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_SET);
    enabled = enabled != 0;
    if (enabled == scene->music_enabled) return sc_result(MAAGE_STATUS_NOOP, MAAGE_OPERATION_SET);
    scene->music_enabled = enabled;
    if (!enabled && scene->bound) {
        maage_status r = sc_stop_music(scene);
        if (maage_status_failed(r)) return r;
        if (scene->clock_ready) scene->next_music = scene->last_now + sc_music_delay(scene, sc_music_minimum(scene), sc_music_maximum(scene));
        return sc_result(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_SET);
    }
    return sc_result(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_SET);
}
maage_status maage_client_audio_scene_destroy(maage_client_audio_scene *scene) {
    unsigned int i;
    maage_status r, first = sc_result(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_DESTROY);
    if (!scene) return sc_result(MAAGE_STATUS_NOOP, MAAGE_OPERATION_DESTROY);
    if (scene->bound) {
        for (i = 0; i < SC_CHANNEL_COUNT; ++i) {
            r = sc_stop_channel(scene, (int) i);
            if (maage_status_failed(r) && !maage_status_failed(first)) first = r;
        }
        r = sc_stop_music(scene);
        if (maage_status_failed(r) && !maage_status_failed(first)) first = r;
    }
    /* Preserve ownership if an uncertain device stop may still reference PCM.
     * Callers may retry destroy; dependencies must remain alive until success. */
    if (maage_status_failed(first)) return first;
    free(scene);
    return first;
}
