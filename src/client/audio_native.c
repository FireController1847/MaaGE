#include "audio_native.h"
#include "audio_service.h"
#include "audio_scene.h"
#include "maage/audio.h"
#include "maage/sound_bank.h"
#include "../audio/catalog/sound_catalog.h"
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#define MAAGE_NATIVE_BANK_BUDGET ((size_t)48U * 1024U * 1024U)
#define MAAGE_NATIVE_BUFFERS 512U
#define MAAGE_NATIVE_VOICES 128U
#define MAAGE_NATIVE_SCENE_SOUNDS 32U
#define MAAGE_NATIVE_MUSIC_TRACKS 3U
#define MAAGE_NATIVE_MAX_VOLUME 4.0f

typedef struct maage_native_bank {
    maage_sound_bank *bank;
    unsigned int next_id;
} maage_native_bank;
typedef struct maage_native_voice {
    maage_audio_voice_id native_id;
    maage_audio_play_desc playback;
    double started, duration;
    unsigned long token;
    int active, paused;
} maage_native_voice;
typedef struct maage_native_engine {
    maage_audio *audio;
    maage_audio_buffer *buffers[MAAGE_NATIVE_BUFFERS];
    maage_native_voice voices[MAAGE_NATIVE_VOICES];
    unsigned int next_buffer, next_voice;
    unsigned long next_token;
    double now;
} maage_native_engine;
typedef struct maage_native_pcm {
    maage_sound_pcm pcm;
} maage_native_pcm;
typedef struct maage_native_scene_sound {
    maage_audio_buffer *buffer;
    double duration;
    size_t bytes;
    int music, loop;
} maage_native_scene_sound;
typedef struct maage_native_scene {
    maage_client_audio_scene *scene;
    maage_native_bank *bank;
    maage_native_engine *engine;
    maage_native_scene_sound sounds[MAAGE_NATIVE_SCENE_SOUNDS];
    unsigned int sound_count, music_count;
} maage_native_scene;

static maage_status mn_status(unsigned char code, unsigned char operation)
{
    return maage_status_make(code, code < 0x40 ? 0 : MAAGE_CONTEXT_CLIENT, operation, 0);
}
static maage_audio_vec3 mn_vector(const double v[3])
{
    maage_audio_vec3 o;
    o.x = (float)v[0]; o.y = (float)v[1]; o.z = (float)v[2];
    return o;
}
static maage_status mn_bank_create(const char *root, void **output, maage_status_trace *trace)
{
    maage_native_bank *bank;
    maage_status status;
    if (output) *output = NULL;
    bank = (maage_native_bank *)calloc(1, sizeof(*bank));
    if (!bank) return mn_status(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_CREATE);
    status = maage_sound_bank_create(root, MAAGE_NATIVE_BANK_BUDGET, &bank->bank, trace);
    if (maage_status_failed(status) || !bank->bank) {
        free(bank);
        return status;
    }
    *output = bank;
    return status;
}
static maage_status mn_bank_destroy(void *handle, maage_status_trace *trace)
{
    maage_native_bank *bank = (maage_native_bank *)handle;
    maage_status status;
    (void)trace;
    if (!bank) return mn_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_DESTROY);
    status = maage_sound_bank_destroy(bank->bank);
    free(bank);
    return status;
}
static maage_status mn_decode_file(maage_native_bank *bank, const char *path,
    maage_native_pcm **output, maage_status_trace *trace)
{
    maage_native_pcm *pcm;
    maage_status result;
    unsigned int id;
    if (output) *output = NULL;
    if (!bank || !path || !output || bank->next_id >= MAAGE_SOUND_MAX_CLIP_ID)
        return mn_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_DECODE);
    id = ++bank->next_id;
    result = maage_sound_bank_register(bank->bank, id, path, trace);
    if (maage_status_failed(result)) return result;
    pcm = (maage_native_pcm *)calloc(1, sizeof(*pcm));
    if (!pcm) return mn_status(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_DECODE);
    result = maage_sound_bank_decode(bank->bank, id, &pcm->pcm, trace);
    if (maage_status_failed(result)) { free(pcm); return result; }
    *output = pcm;
    return result;
}
static maage_status mn_catalog_decode(void *bank, const char *path,
    void **output, maage_status_trace *trace)
{
    return mn_decode_file((maage_native_bank *)bank, path, (maage_native_pcm **)output, trace);
}
static void mn_catalog_release_pcm(void *bank, void *value)
{
    maage_native_pcm *pcm = (maage_native_pcm *)value;
    (void)bank;
    if (!pcm) return;
    maage_sound_pcm_destroy(&pcm->pcm);
    free(pcm);
}
/* Bridge immutable float PCM from the SoundBank to the signed-16-bit Audio
 * engine once at preload, never during frame/event playback. */
static maage_status mn_upload_pcm(maage_native_engine *engine,
    const maage_native_pcm *pcm, maage_audio_buffer **buffer, maage_status_trace *trace)
{
    size_t frames, samples, i;
    signed short *values;
    maage_status result;
    float sample;
    if (buffer) *buffer = NULL;
    if (!engine || !pcm || !buffer || !pcm->pcm.samples ||
        (pcm->pcm.channels != 1U && pcm->pcm.channels != 2U) ||
        pcm->pcm.frame_count > ((size_t)-1) / pcm->pcm.channels)
        return mn_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_UPLOAD);
    frames = pcm->pcm.frame_count;
    samples = frames * pcm->pcm.channels;
    if (samples > ((size_t)-1) / sizeof(*values))
        return mn_status(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_OPERATION_UPLOAD);
    values = (signed short *)malloc(samples * sizeof(*values));
    if (!values) return mn_status(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_UPLOAD);
    for (i = 0; i < samples; ++i) {
        sample = pcm->pcm.samples[i];
        if (!(sample == sample)) sample = 0.0f;
        if (sample > 1.0f) sample = 1.0f;
        if (sample < -1.0f) sample = -1.0f;
        values[i] = (signed short)(sample * (sample >= 0.0f ? 32767.0f : 32768.0f));
    }
    result = maage_audio_buffer_create(engine->audio, values, frames,
        pcm->pcm.channels, pcm->pcm.sample_rate, buffer, trace);
    free(values);
    return result;
}
static maage_status mn_engine_create(void **output, maage_status_trace *trace)
{
    maage_native_engine *engine;
    maage_status status;
    if (output) *output = NULL;
    engine = (maage_native_engine *)calloc(1, sizeof(*engine));
    if (!engine) return mn_status(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_CREATE);
    status = maage_audio_create(NULL, &engine->audio, trace);
    if (maage_status_failed(status) || !engine->audio) {
        free(engine);
        return status;
    }
    *output = engine;
    return status;
}
static maage_status mn_catalog_upload(void *value, const void *pcm,
    unsigned long *numeric, maage_status_trace *trace)
{
    maage_native_engine *engine = (maage_native_engine *)value;
    maage_audio_buffer *buffer;
    maage_status status;
    unsigned int i;
    if (numeric) *numeric = 0;
    if (!engine || !pcm || !numeric)
        return mn_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_UPLOAD);
    for (i = 1; i < MAAGE_NATIVE_BUFFERS; ++i)
        if (!engine->buffers[i]) break;
    if (i == MAAGE_NATIVE_BUFFERS)
        return mn_status(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_OPERATION_UPLOAD);
    status = mn_upload_pcm(engine, (const maage_native_pcm *)pcm, &buffer, trace);
    if (maage_status_failed(status)) return status;
    if (!buffer) return mn_status(MAAGE_STATUS_INVALID_STATE, MAAGE_OPERATION_UPLOAD);
    engine->buffers[i] = buffer;
    *numeric = (unsigned long)i;
    return status;
}
static maage_status mn_catalog_release_buffer(void *value, unsigned long numeric,
    maage_status_trace *trace)
{
    maage_native_engine *engine = (maage_native_engine *)value;
    maage_status result;
    (void)trace;
    if (!engine || !numeric || numeric >= MAAGE_NATIVE_BUFFERS || !engine->buffers[numeric])
        return mn_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_DESTROY);
    result = maage_audio_buffer_destroy(engine->buffers[numeric]);
    engine->buffers[numeric] = NULL;
    return result;
}
static maage_audio_bus mn_catalog_bus(unsigned int bus)
{
    static const maage_audio_bus mapping[5] = {
        MAAGE_AUDIO_BLOCKS, MAAGE_AUDIO_PLAYER, MAAGE_AUDIO_UI,
        MAAGE_AUDIO_AMBIENT, MAAGE_AUDIO_WEATHER
    };
    return bus < 5U ? mapping[bus] : MAAGE_AUDIO_BLOCKS;
}
static maage_status mn_play(maage_native_engine *engine, maage_audio_buffer *buffer,
    unsigned int category, int positional, const double position[3],
    float gain, float pitch, int loop, double duration, unsigned long *token,
    maage_status_trace *trace)
{
    maage_audio_play_desc desc;
    maage_audio_voice_id native_id = 0;
    maage_native_voice *voice;
    maage_status result;
    unsigned int index;
    if (token) *token = 0;
    if (!engine || !buffer || !position)
        return mn_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_SUBMIT);
    memset(&desc, 0, sizeof(desc));
    desc.buffer = buffer;
    desc.category = (maage_audio_bus)category;
    desc.gain = gain < 0.0f ? 0.0f : (gain > MAAGE_NATIVE_MAX_VOLUME ? MAAGE_NATIVE_MAX_VOLUME : gain);
    desc.pitch = pitch < 0.25f ? 0.25f : (pitch > 4.0f ? 4.0f : pitch);
    desc.positional = positional;
    desc.position = mn_vector(position);
    desc.loop = loop;
    result = maage_audio_play(engine->audio, &desc, &native_id, trace);
    if (maage_status_failed(result) || !native_id) return maage_status_failed(result) ? result :
        mn_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_SUBMIT);
    index = engine->next_voice++ % MAAGE_NATIVE_VOICES;
    voice = &engine->voices[index];
    if (voice->active && voice->native_id)
        (void)maage_audio_stop(engine->audio, voice->native_id);
    voice->native_id = native_id;
    voice->playback = desc;
    voice->started = engine->now;
    voice->duration = duration > 0.0 ? duration / (double)desc.pitch : 0.0;
    voice->paused = 0;
    voice->active = 1;
    voice->token = ++engine->next_token;
    if (!voice->token) voice->token = ++engine->next_token;
    if (token) *token = voice->token;
    return result;
}
static maage_native_voice *mn_voice_find(maage_native_engine *engine, unsigned long token)
{
    unsigned int i;
    if (!engine || !token) return NULL;
    for (i = 0; i < MAAGE_NATIVE_VOICES; ++i)
        if (engine->voices[i].active && engine->voices[i].token == token)
            return &engine->voices[i];
    return NULL;
}
static maage_status mn_catalog_play(void *value, unsigned long buffer,
    unsigned int bus, int positional, const double position[3],
    float gain, float pitch, float max_distance, maage_status_trace *trace)
{
    maage_native_engine *engine = (maage_native_engine *)value;
    (void)max_distance; /* Audio owns its positional distance response. */
    if (!engine || !buffer || buffer >= MAAGE_NATIVE_BUFFERS || !engine->buffers[buffer] || bus >= 5U)
        return mn_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_SUBMIT);
    return mn_play(engine, engine->buffers[buffer], mn_catalog_bus(bus),
        positional, position, gain, pitch, 0, 0.0, NULL, trace);
}
static maage_status mn_catalog_create(void *bank, void *engine,
    void **output, maage_status_trace *trace)
{
    maage_sound_catalog *catalog = NULL;
    maage_sound_catalog_port port;
    maage_status status;
    if (output) *output = NULL;
    if (!bank || !engine || !output)
        return mn_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_CREATE);
    status = maage_sound_catalog_create(bank, engine, &catalog, trace);
    if (maage_status_failed(status) || !catalog) return status;
    memset(&port, 0, sizeof(port));
    port.decode = mn_catalog_decode;
    port.upload = mn_catalog_upload;
    port.release_pcm = mn_catalog_release_pcm;
    port.release_buffer = mn_catalog_release_buffer;
    port.play = mn_catalog_play;
    status = maage_sound_catalog_set_port(catalog, &port, trace);
    if (!maage_status_failed(status)) status = maage_sound_catalog_preload(catalog, trace);
    if (maage_status_failed(status)) {
        (void)maage_sound_catalog_destroy(catalog);
        return status;
    }
    *output = catalog;
    return status;
}
static maage_status mn_catalog_destroy(void *handle, maage_status_trace *trace)
{
    (void)trace;
    return maage_sound_catalog_destroy((maage_sound_catalog *)handle);
}
static maage_status mn_catalog_event(void *catalog, void *engine,
    const maage_sound_event *event, maage_status_trace *trace)
{
    (void)engine;
    return maage_sound_catalog_play((maage_sound_catalog *)catalog, event, trace);
}
static maage_status mn_set_volume(void *engine, unsigned int bus,
    int percentage, maage_status_trace *trace)
{
    (void)trace;
    if (!engine || bus >= MAAGE_CLIENT_AUDIO_BUS_COUNT || percentage < 0 || percentage > 100)
        return mn_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_SET);
    return maage_audio_set_volume(((maage_native_engine *)engine)->audio,
        (maage_audio_bus)bus, (float)percentage / 100.0f);
}
static maage_status mn_set_listener(void *engine, const double position[3],
    const double forward[3], maage_status_trace *trace)
{
    maage_audio_vec3 up;
    (void)trace;
    if (!engine || !position || !forward)
        return mn_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_SET);
    up.x = 0.0f; up.y = 1.0f; up.z = 0.0f;
    return maage_audio_set_listener(((maage_native_engine *)engine)->audio,
        mn_vector(position), mn_vector(forward), up);
}
static maage_status mn_engine_poll(void *engine, maage_status_trace *trace)
{
    return maage_audio_poll(((maage_native_engine *)engine)->audio, trace);
}
static maage_status mn_stop_voices(void *handle, maage_status_trace *trace)
{
    maage_native_engine *engine = (maage_native_engine *)handle;
    maage_status first, result;
    unsigned int i;
    (void)trace;
    first = mn_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_STOP);
    for (i = 0; i < MAAGE_NATIVE_VOICES; ++i) {
        maage_native_voice *voice = &engine->voices[i];
        if (!voice->active || !voice->native_id) continue;
        result = maage_audio_stop(engine->audio, voice->native_id);
        if (maage_status_failed(result) && !maage_status_failed(first)) first = result;
        voice->active = 0;
        voice->native_id = 0;
    }
    return first;
}
static maage_status mn_engine_stop(void *handle, maage_status_trace *trace)
{
    return mn_engine_poll(handle, trace);
}
static maage_status mn_engine_destroy(void *handle, maage_status_trace *trace)
{
    maage_native_engine *engine = (maage_native_engine *)handle;
    maage_status first, result;
    unsigned int i;
    if (!engine) return mn_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_DESTROY);
    first = mn_stop_voices(handle, trace);
    for (i = 1; i < MAAGE_NATIVE_BUFFERS; ++i) if (engine->buffers[i]) {
        result = maage_audio_buffer_destroy(engine->buffers[i]);
        if (maage_status_failed(result) && !maage_status_failed(first)) first = result;
    }
    result = maage_audio_destroy(engine->audio, trace);
    if (maage_status_failed(result) && !maage_status_failed(first)) first = result;
    free(engine);
    return first;
}

/* The environment scene consumes copied numeric Server observations only.
 * Missing nearby-liquid proximity is represented as zero, never guessed from
 * the visual renderer or by performing World reads on this thread. */
static maage_status mn_scene_read_snapshot(void *context,
    const maage_session_snapshot *snapshot, const double listener[3],
    maage_client_audio_scene_observation *observation)
{
    unsigned int sky, block;
    (void)context;
    if (!snapshot || !listener || !observation)
        return mn_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_SNAPSHOT);
    memset(observation, 0, sizeof(*observation));
    observation->rain_intensity = snapshot->environment.rain_intensity;
    observation->thunder_intensity = snapshot->environment.thunder_intensity;
    observation->sky_exposed = 1;
    if (snapshot->has_observation) {
        observation->biome_rainfall = snapshot->observation.rainfall;
        observation->biome_temperature = snapshot->observation.temperature;
        observation->sky_exposed = snapshot->observation.sky_exposed;
        observation->under_cover = !snapshot->observation.sky_exposed;
        observation->light_available = snapshot->observation.light_available;
        sky = ((unsigned int)snapshot->observation.light >> 4) & 15U;
        block = (unsigned int)snapshot->observation.light & 15U;
        observation->cave_darkness =
            1.0 - (double)(sky > block ? sky : block) / 15.0;
    } else {
        /* Weather is not emitted from an unknown biome. */
        observation->biome_rainfall = 0.0;
        observation->biome_temperature = 0.0;
    }
    observation->water_position[0] = observation->lava_position[0] = listener[0];
    observation->water_position[1] = observation->lava_position[1] = listener[1];
    observation->water_position[2] = observation->lava_position[2] = listener[2];
    return mn_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_SNAPSHOT);
}

/* Exactly one registration/decode/upload per logical ambient asset at bind.
 * No resource lookup, disk access, or decoding occurs in scene_update. */
static maage_status mn_scene_resolve(void *context, const void *catalog,
    const char *path, maage_client_audio_scene_sound_id *numeric)
{
    maage_native_scene *scene = (maage_native_scene *)context;
    maage_native_scene_sound *sound;
    maage_native_pcm *pcm = NULL;
    maage_audio_buffer *buffer = NULL;
    maage_status result;
    size_t samples;
    unsigned int id;
    int music;
    (void)catalog;
    if (numeric) *numeric = 0;
    if (!scene || !path || !numeric || scene->sound_count + 1U >= MAAGE_NATIVE_SCENE_SOUNDS)
        return mn_status(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_OPERATION_RESOLVE);
    id = ++scene->sound_count;
    sound = &scene->sounds[id];
    music = strstr(path, "/music/") != NULL;
    /* Long tracks are expensive. This explicit bound is fixed at initialization;
     * the other tracks remain optional without causing an update-time decode. */
    if (music && scene->music_count >= MAAGE_NATIVE_MUSIC_TRACKS)
        return mn_status(MAAGE_STATUS_NOT_FOUND, MAAGE_OPERATION_RESOLVE);
    result = mn_decode_file(scene->bank, path, &pcm, NULL);
    if (maage_status_failed(result)) return result;
    if (pcm->pcm.frame_count > ((size_t)-1) / pcm->pcm.channels / sizeof(float)) {
        mn_catalog_release_pcm(scene->bank, pcm);
        return mn_status(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_OPERATION_RESOLVE);
    }
    samples = pcm->pcm.frame_count * (size_t)pcm->pcm.channels;
    sound->bytes = samples * sizeof(float);
    if (music && sound->bytes > MAAGE_AUDIO_SCENE_MAX_PCM_BYTES) {
        mn_catalog_release_pcm(scene->bank, pcm);
        return mn_status(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_OPERATION_RESOLVE);
    }
    result = mn_upload_pcm(scene->engine, pcm, &buffer, NULL);
    if (pcm->pcm.sample_rate)
        sound->duration = (double)pcm->pcm.frame_count / (double)pcm->pcm.sample_rate;
    mn_catalog_release_pcm(scene->bank, pcm);
    if (maage_status_failed(result) || !buffer)
        return maage_status_failed(result) ? result :
            mn_status(MAAGE_STATUS_INVALID_STATE, MAAGE_OPERATION_UPLOAD);
    sound->buffer = buffer;
    sound->music = music;
    sound->loop = strstr(path, "/weather/rain") != NULL ||
        strstr(path, "/liquid/water") != NULL ||
        strstr(path, "/liquid/lava.") != NULL;
    if (music) ++scene->music_count;
    *numeric = (maage_client_audio_scene_sound_id)id;
    return mn_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_RESOLVE);
}
static maage_native_scene_sound *mn_scene_get(maage_native_scene *scene,
    maage_client_audio_scene_sound_id numeric)
{
    if (!scene || !numeric || numeric > scene->sound_count || numeric >= MAAGE_NATIVE_SCENE_SOUNDS)
        return NULL;
    return scene->sounds[numeric].buffer ? &scene->sounds[numeric] : NULL;
}
static int mn_scene_can_loop(void *context, const void *catalog,
    maage_client_audio_scene_sound_id numeric)
{
    maage_native_scene_sound *sound = mn_scene_get((maage_native_scene *)context, numeric);
    (void)catalog;
    return sound && sound->loop;
}
static maage_status mn_scene_play_sound(void *context, void *audio,
    maage_client_audio_scene_sound_id numeric, unsigned int category,
    const double *position, double gain, int loop,
    maage_client_audio_scene_voice *voice)
{
    static const double origin[3] = {0.0, 0.0, 0.0};
    maage_native_scene *scene = (maage_native_scene *)context;
    maage_native_scene_sound *sound = mn_scene_get(scene, numeric);
    unsigned int bus;
    (void)audio;
    if (voice) *voice = 0;
    if (!sound || !voice || (loop && !sound->loop))
        return mn_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_SUBMIT);
    bus = category == MAAGE_AUDIO_SCENE_WEATHER ? MAAGE_AUDIO_WEATHER :
        (category == MAAGE_AUDIO_SCENE_MUSIC ? MAAGE_AUDIO_MUSIC : MAAGE_AUDIO_AMBIENT);
    return mn_play(scene->engine, sound->buffer, bus, position != NULL,
        position ? position : origin, (float)gain, 1.0f, loop, sound->duration,
        (unsigned long *)voice, NULL);
}
static maage_status mn_scene_music_prepare(void *context, const void *catalog,
    maage_client_audio_scene_sound_id numeric, size_t max_bytes,
    void **pcm, size_t *bytes)
{
    maage_native_scene_sound *sound = mn_scene_get((maage_native_scene *)context, numeric);
    (void)catalog;
    if (pcm) *pcm = NULL;
    if (bytes) *bytes = 0;
    if (!pcm || !bytes || !sound || !sound->music)
        return mn_status(MAAGE_STATUS_NOT_FOUND, MAAGE_OPERATION_LOOKUP);
    if (sound->bytes > max_bytes)
        return mn_status(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_OPERATION_LOOKUP);
    *pcm = sound;
    *bytes = sound->bytes;
    return mn_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_LOOKUP);
}
static maage_status mn_scene_music_play(void *context, void *audio, void *pcm,
    double gain, maage_client_audio_scene_voice *voice)
{
    static const double zero[3] = {0.0, 0.0, 0.0};
    maage_native_scene_sound *sound = (maage_native_scene_sound *)pcm;
    maage_native_scene *scene = (maage_native_scene *)context;
    (void)audio;
    if (voice) *voice = 0;
    if (!scene || !sound || !sound->music || !voice)
        return mn_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_SUBMIT);
    return mn_play(scene->engine, sound->buffer, MAAGE_AUDIO_MUSIC, 0, zero,
        (float)gain, 1.0f, 0, sound->duration, (unsigned long *)voice, NULL);
}
static void mn_scene_music_release(void *context, void *pcm)
{
    /* Borrowed immutable Audio-owned buffer retained until scene_destroy. */
    (void)context; (void)pcm;
}
static int mn_scene_voice_running(void *context, void *audio,
    maage_client_audio_scene_voice token)
{
    maage_native_scene *scene = (maage_native_scene *)context;
    maage_native_voice *voice;
    (void)audio;
    if (!scene) return 0;
    voice = mn_voice_find(scene->engine, (unsigned long)token);
    if (!voice) return 0;
    if (voice->paused || voice->playback.loop) return 1;
    /* Audio exposes no completion query: duration is based on the exact decoded
     * frame count and chosen pitch; expired one-shots are reaped by the scene. */
    return voice->duration > 0.0 && scene->engine->now - voice->started < voice->duration;
}
static maage_status mn_scene_voice_gain(void *context, void *audio,
    maage_client_audio_scene_voice token, double gain)
{
    maage_native_scene *scene = (maage_native_scene *)context;
    maage_native_voice *voice;
    maage_audio_play_desc desc;
    maage_audio_voice_id replacement = 0;
    maage_status result;
    float clamped;
    (void)audio;
    if (!scene) return mn_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_SET);
    voice = mn_voice_find(scene->engine, (unsigned long)token);
    if (!voice) return mn_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_SET);
    clamped = gain < 0.0 ? 0.0f : (gain > MAAGE_NATIVE_MAX_VOLUME ? MAAGE_NATIVE_MAX_VOLUME : (float)gain);
    if (clamped - voice->playback.gain < 0.05f &&
        voice->playback.gain - clamped < 0.05f)
        return mn_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_SET);
    /* Engine v1 has no per-voice gain setter. Restart only on significant
     * envelope changes instead of re-decoding, and retain the logical token. */
    desc = voice->playback;
    desc.gain = clamped;
    if (voice->paused) { voice->playback = desc; return mn_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_SET); }
    result = maage_audio_stop(scene->engine->audio, voice->native_id);
    if (maage_status_failed(result)) return result;
    voice->native_id = 0;
    result = maage_audio_play(scene->engine->audio, &desc, &replacement, NULL);
    if (maage_status_failed(result) || !replacement) {
        voice->active = 0;
        return maage_status_failed(result) ? result :
            mn_status(MAAGE_STATUS_INVALID_STATE, MAAGE_OPERATION_START);
    }
    voice->native_id = replacement;
    voice->playback = desc;
    voice->started = scene->engine->now;
    return result;
}
static maage_status mn_scene_voice_pause(void *context, void *audio,
    maage_client_audio_scene_voice token, int paused)
{
    maage_native_scene *scene = (maage_native_scene *)context;
    maage_native_voice *voice;
    maage_audio_voice_id replacement = 0;
    maage_status result;
    (void)audio;
    if (!scene) return mn_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_SET);
    voice = mn_voice_find(scene->engine, (unsigned long)token);
    if (!voice || paused == voice->paused) return mn_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_SET);
    if (paused) {
        result = maage_audio_stop(scene->engine->audio, voice->native_id);
        if (maage_status_failed(result)) return result;
        voice->native_id = 0;
        voice->paused = 1;
        return result;
    }
    result = maage_audio_play(scene->engine->audio, &voice->playback, &replacement, NULL);
    if (maage_status_failed(result) || !replacement) return maage_status_failed(result) ? result :
        mn_status(MAAGE_STATUS_INVALID_STATE, MAAGE_OPERATION_START);
    voice->native_id = replacement;
    voice->started = scene->engine->now;
    voice->paused = 0;
    return result;
}
static maage_status mn_scene_voice_stop(void *context, void *audio,
    maage_client_audio_scene_voice token)
{
    maage_native_scene *scene = (maage_native_scene *)context;
    maage_native_voice *voice;
    maage_status result;
    (void)audio;
    if (!scene) return mn_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_STOP);
    voice = mn_voice_find(scene->engine, (unsigned long)token);
    if (!voice) return mn_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_STOP);
    result = voice->native_id ? maage_audio_stop(scene->engine->audio, voice->native_id) :
        mn_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_STOP);
    if (!maage_status_failed(result)) { voice->active = 0; voice->native_id = 0; }
    return result;
}
static maage_status mn_scene_destroy(void *handle, maage_status_trace *trace)
{
    maage_native_scene *scene = (maage_native_scene *)handle;
    maage_status first, result;
    unsigned int i;
    (void)trace;
    if (!scene) return mn_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_DESTROY);
    first = maage_client_audio_scene_destroy(scene->scene);
    for (i = 1; i <= scene->sound_count; ++i) if (scene->sounds[i].buffer) {
        result = maage_audio_buffer_destroy(scene->sounds[i].buffer);
        if (maage_status_failed(result) && !maage_status_failed(first)) first = result;
    }
    free(scene);
    return first;
}
static maage_status mn_scene_create(void *bank, void *catalog, void *engine,
    void **output, maage_status_trace *trace)
{
    maage_native_scene *scene;
    maage_client_audio_scene_ports ports;
    maage_status result;
    if (output) *output = NULL;
    if (!bank || !catalog || !engine || !output)
        return mn_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_CREATE);
    scene = (maage_native_scene *)calloc(1, sizeof(*scene));
    if (!scene) return mn_status(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_CREATE);
    scene->bank = (maage_native_bank *)bank;
    scene->engine = (maage_native_engine *)engine;
    result = maage_client_audio_scene_create(catalog, engine, &scene->scene, trace);
    if (maage_status_failed(result)) { free(scene); return result; }
    memset(&ports, 0, sizeof(ports));
    ports.read_snapshot = mn_scene_read_snapshot;
    ports.resolve_sound = mn_scene_resolve;
    ports.sound_can_loop = mn_scene_can_loop;
    ports.play_sound = mn_scene_play_sound;
    ports.music_prepare = mn_scene_music_prepare;
    ports.music_play = mn_scene_music_play;
    ports.music_release = mn_scene_music_release;
    ports.voice_running = mn_scene_voice_running;
    ports.voice_gain = mn_scene_voice_gain;
    ports.voice_pause = mn_scene_voice_pause;
    ports.voice_stop = mn_scene_voice_stop;
    result = maage_client_audio_scene_bind(scene->scene, &ports, scene, trace);
    if (maage_status_failed(result)) { (void)mn_scene_destroy(scene, trace); return result; }
    *output = scene;
    return result;
}
static maage_status mn_scene_update(void *handle, const maage_session_snapshot *snapshot,
    const double position[3], const double forward[3], double now, maage_status_trace *trace)
{
    maage_native_scene *scene = (maage_native_scene *)handle;
    (void)forward;
    if (!scene || !snapshot) return mn_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_UPDATE);
    scene->engine->now = now;
    return maage_client_audio_scene_update(scene->scene, snapshot, position, now, trace);
}
static maage_status mn_scene_stop(void *handle, maage_status_trace *trace)
{
    (void)trace;
    return maage_client_audio_scene_pause(((maage_native_scene *)handle)->scene, 1);
}
static maage_status mn_scene_music_enabled(void *handle, int enabled)
{
    return maage_client_audio_scene_music_enabled(((maage_native_scene *)handle)->scene, enabled);
}

maage_status maage_client_audio_native_install(void)
{
    maage_client_audio_components components;
    memset(&components, 0, sizeof(components));
    components.version = MAAGE_CLIENT_AUDIO_COMPONENTS_VERSION;
    components.bank_create = mn_bank_create;
    components.engine_create = mn_engine_create;
    components.catalog_create = mn_catalog_create;
    components.scene_create = mn_scene_create;
    components.set_volume = mn_set_volume;
    components.set_listener = mn_set_listener;
    components.play_event = mn_catalog_event;
    components.scene_update = mn_scene_update;
    components.scene_stop = mn_scene_stop;
    components.scene_music_enabled = mn_scene_music_enabled;
    components.poll = mn_engine_poll;
    components.stop_voices = mn_stop_voices;
    components.engine_stop = mn_engine_stop;
    components.scene_destroy = mn_scene_destroy;
    components.catalog_destroy = mn_catalog_destroy;
    components.engine_destroy = mn_engine_destroy;
    components.bank_destroy = mn_bank_destroy;
    return maage_client_audio_service_components_install(&components);
}
