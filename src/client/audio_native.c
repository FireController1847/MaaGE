#include "audio_native.h"
#include "audio_service.h"
#include "audio_scene.h"
#include "maage/audio.h"
#include "maage/sound_bank.h"
#include "maage/thread.h"
#include "maage/log.h"
#include "../audio/catalog/sound_catalog.h"
#include <stdlib.h>
#include <string.h>
#include <limits.h>

/* Catalog effects are retained by Audio; caching the same PCM in SoundBank
 * duplicates memory. Other SoundBank users may choose a nonzero cache. */
#define MAAGE_NATIVE_BANK_BUDGET 0U
#define MAAGE_NATIVE_STREAM_FRAMES 16384U
#define MAAGE_NATIVE_DECODE_FRAMES 4096U
#define MAAGE_NATIVE_MAX_VOLUME 4.0f

typedef struct maage_native_bank {
    maage_sound_bank *bank;
    unsigned long next_id;
} maage_native_bank;
typedef struct maage_native_voice {
    maage_audio_voice_id native_id;
    maage_audio_play_desc playback;
    unsigned long token;
    int active;
} maage_native_voice;
typedef struct maage_native_engine {
    maage_audio *audio;
    maage_audio_buffer **buffers;
    size_t buffer_capacity;
    maage_native_voice *voices;
    size_t voice_capacity, voice_count;
    unsigned long next_token;
} maage_native_engine;
typedef struct maage_native_pcm {
    maage_sound_pcm pcm;
} maage_native_pcm;
typedef struct maage_native_music {
    maage_sound_stream *decoder;
    maage_audio_stream *ring;
    maage_thread *worker;
    maage_mutex *mutex;
    maage_condition *wake;
    maage_status result;
    unsigned int channels;
    int cancel;
} maage_native_music;
typedef struct maage_native_scene_sound {
    maage_audio_buffer *buffer;
    maage_native_music *stream;
    size_t bytes;
    int music, loop;
    unsigned long clip_id;
} maage_native_scene_sound;
typedef struct maage_native_scene {
    maage_client_audio_scene *scene;
    maage_native_bank *bank;
    maage_native_engine *engine;
    maage_native_scene_sound *sounds;
    size_t sound_count, sound_capacity;
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
    unsigned long id;
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
/* Resource decoding delivers immutable float PCM directly to Audio. */
static maage_status mn_upload_pcm(maage_native_engine *engine,
    const maage_native_pcm *pcm, maage_audio_buffer **buffer, maage_status_trace *trace)
{
    if (buffer) *buffer = NULL;
    if (!engine || !pcm || !buffer || !pcm->pcm.samples)
        return mn_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_UPLOAD);
    return maage_audio_buffer_create_float(engine->audio, pcm->pcm.samples,
        pcm->pcm.frame_count, pcm->pcm.channels, pcm->pcm.sample_rate, buffer, trace);
}
static maage_status mn_buffers_reserve(maage_native_engine *engine, size_t need)
{
    maage_audio_buffer **grown;
    size_t capacity = engine->buffer_capacity ? engine->buffer_capacity : 32U;
    if (need <= engine->buffer_capacity) return mn_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_RESIZE);
    while (capacity < need) {
        if (capacity > ((size_t)-1) / 2U) return mn_status(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_OPERATION_RESIZE);
        capacity *= 2U;
    }
    if (capacity > ((size_t)-1) / sizeof(*grown) || capacity - 1U > ULONG_MAX)
        return mn_status(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_OPERATION_RESIZE);
    grown = (maage_audio_buffer **)realloc(engine->buffers, capacity * sizeof(*grown));
    if (!grown) return mn_status(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_RESIZE);
    memset(grown + engine->buffer_capacity, 0,
        (capacity - engine->buffer_capacity) * sizeof(*grown));
    engine->buffers = grown;
    engine->buffer_capacity = capacity;
    return mn_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_RESIZE);
}
static maage_status mn_voices_reserve(maage_native_engine *engine, size_t need)
{
    maage_native_voice *grown;
    size_t capacity = engine->voice_capacity ? engine->voice_capacity : 8U;
    if (need <= engine->voice_capacity) return mn_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_RESIZE);
    while (capacity < need) {
        if (capacity > ((size_t)-1) / 2U) return mn_status(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_OPERATION_RESIZE);
        capacity *= 2U;
    }
    if (capacity > ((size_t)-1) / sizeof(*grown))
        return mn_status(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_OPERATION_RESIZE);
    grown = (maage_native_voice *)realloc(engine->voices, capacity * sizeof(*grown));
    if (!grown) return mn_status(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_RESIZE);
    memset(grown + engine->voice_capacity, 0,
        (capacity - engine->voice_capacity) * sizeof(*grown));
    engine->voices = grown;
    engine->voice_capacity = capacity;
    return mn_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_RESIZE);
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
    size_t i;
    if (numeric) *numeric = 0;
    if (!engine || !pcm || !numeric)
        return mn_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_UPLOAD);
    if (!engine->buffer_capacity) {
        status = mn_buffers_reserve(engine, 32U);
        if (maage_status_failed(status)) return status;
    }
    for (i = 1; i < engine->buffer_capacity; ++i)
        if (!engine->buffers[i]) break;
    if (i == engine->buffer_capacity) {
        status = mn_buffers_reserve(engine, i + 1U);
        if (maage_status_failed(status)) return status;
    }
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
    if (!engine || !numeric || numeric >= engine->buffer_capacity || !engine->buffers[numeric])
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
static maage_status mn_play(maage_native_engine *engine,
    maage_audio_buffer *buffer, maage_audio_stream *stream,
    unsigned int category, int positional, const double position[3],
    float gain, float pitch, int loop, unsigned long *token,
    maage_status_trace *trace)
{
    maage_audio_play_desc desc;
    maage_audio_voice_id native_id = 0;
    maage_native_voice *voice = NULL;
    maage_status result;
    size_t index = 0;
    if (token) *token = 0;
    if (!engine || (!buffer && !stream) || !position)
        return mn_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_SUBMIT);
    if (token) {
        if (engine->next_token == ULONG_MAX)
            return mn_status(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_OPERATION_SUBMIT);
        for (index = 0; index < engine->voice_count; ++index)
            if (!engine->voices[index].active) break;
        if (index == engine->voice_count) {
            result = mn_voices_reserve(engine, index + 1U);
            if (maage_status_failed(result)) return result;
            ++engine->voice_count;
        }
        voice = &engine->voices[index];
    }
    memset(&desc, 0, sizeof(desc));
    desc.buffer = buffer;
    desc.stream = stream;
    desc.category = (maage_audio_bus)category;
    desc.gain = gain < 0.0f ? 0.0f : (gain > MAAGE_NATIVE_MAX_VOLUME ? MAAGE_NATIVE_MAX_VOLUME : gain);
    desc.pitch = pitch < 0.25f ? 0.25f : (pitch > 4.0f ? 4.0f : pitch);
    desc.positional = positional;
    desc.position = mn_vector(position);
    desc.loop = loop;
    result = maage_audio_play(engine->audio, &desc, &native_id, trace);
    if (maage_status_failed(result) || !native_id) return maage_status_failed(result) ? result :
        mn_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_SUBMIT);
    if (voice) {
        voice->native_id = native_id;
        voice->playback = desc;
        voice->active = 1;
        voice->token = ++engine->next_token;
        *token = voice->token;
    }
    return result;
}
static maage_native_voice *mn_voice_find(maage_native_engine *engine, unsigned long token)
{
    size_t i;
    if (!engine || !token) return NULL;
    for (i = 0; i < engine->voice_count; ++i)
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
    if (!engine || !buffer || buffer >= engine->buffer_capacity || !engine->buffers[buffer] || bus >= 5U)
        return mn_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_SUBMIT);
    return mn_play(engine, engine->buffers[buffer], NULL, mn_catalog_bus(bus),
        positional, position, gain, pitch, 0, NULL, trace);
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
static maage_status mn_engine_poll(void *handle, maage_status_trace *trace)
{
    maage_native_engine *engine = (maage_native_engine *)handle;
    maage_audio_voice_state state;
    maage_status result;
    size_t i;
    if (!engine) return mn_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_POLL);
    result = maage_audio_poll(engine->audio, trace);
    for (i = 0; i < engine->voice_count; ++i) if (engine->voices[i].active) {
        state = MAAGE_AUDIO_VOICE_STOPPED;
        (void)maage_audio_voice_get_state(engine->audio, engine->voices[i].native_id, &state);
        if (state == MAAGE_AUDIO_VOICE_STOPPED) {
            engine->voices[i].active = 0;
            engine->voices[i].native_id = 0;
        }
    }
    return result;
}
static maage_status mn_stop_voices(void *handle, maage_status_trace *trace)
{
    maage_native_engine *engine = (maage_native_engine *)handle;
    maage_status result;
    size_t i;
    (void)trace;
    if (!engine) return mn_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_STOP);
    result = maage_audio_stop_all(engine->audio);
    for (i = 0; i < engine->voice_count; ++i) {
        engine->voices[i].active = 0;
        engine->voices[i].native_id = 0;
    }
    return result;
}
static maage_status mn_engine_stop(void *handle, maage_status_trace *trace)
{
    return mn_engine_poll(handle, trace);
}
static maage_status mn_engine_destroy(void *handle, maage_status_trace *trace)
{
    maage_native_engine *engine = (maage_native_engine *)handle;
    maage_status first, result;
    size_t i;
    if (!engine) return mn_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_DESTROY);
    first = mn_stop_voices(handle, trace);
    for (i = 1; i < engine->buffer_capacity; ++i) if (engine->buffers[i]) {
        result = maage_audio_buffer_destroy(engine->buffers[i]);
        if (maage_status_failed(result) && !maage_status_failed(first)) first = result;
    }
    result = maage_audio_destroy(engine->audio, trace);
    if (maage_status_failed(result) && !maage_status_failed(first)) first = result;
    free(engine->voices);
    free(engine->buffers);
    free(engine);
    return first;
}


/* One bounded decoder worker per selected music track. The mixer sees only
 * a preallocated float ring and never touches the decoder or filesystem. */
static void mn_music_worker(void *context)
{
    maage_native_music *music = (maage_native_music *)context;
    float samples[MAAGE_NATIVE_DECODE_FRAMES * 2U];
    size_t received, offset, accepted;
    maage_status result = mn_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_DECODE);
    int cancelled = 0;
    for (;;) {
        maage_mutex_lock(music->mutex);
        cancelled = music->cancel;
        maage_mutex_unlock(music->mutex);
        if (cancelled) break;
        received = 0;
        result = maage_sound_stream_read(music->decoder, samples,
            MAAGE_NATIVE_DECODE_FRAMES, &received, NULL);
        if (maage_status_failed(result) || !received) break;
        offset = 0;
        while (offset < received) {
            maage_mutex_lock(music->mutex);
            cancelled = music->cancel;
            maage_mutex_unlock(music->mutex);
            if (cancelled) break;
            accepted = 0;
            result = maage_audio_stream_write(music->ring,
                samples + offset * music->channels, received - offset, &accepted);
            if (maage_status_failed(result)) break;
            offset += accepted;
            if (!accepted) {
                maage_mutex_lock(music->mutex);
                if (!music->cancel) (void)maage_condition_timed_wait(
                    music->wake, music->mutex, 10UL);
                cancelled = music->cancel;
                maage_mutex_unlock(music->mutex);
                if (cancelled) break;
            }
        }
        if (maage_status_failed(result) || cancelled) break;
    }
    music->result = result;
    (void)maage_audio_stream_finish(music->ring);
}
static maage_status mn_music_release(maage_native_music *music)
{
    maage_status result;
    if (!music) return mn_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_RELEASE);
    if (music->mutex) {
        maage_mutex_lock(music->mutex);
        music->cancel = 1;
        if (music->wake) maage_condition_broadcast(music->wake);
        maage_mutex_unlock(music->mutex);
    }
    if (music->worker) (void)maage_thread_join(music->worker);
    result = music->worker ? music->result :
        mn_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_RELEASE);
    if (music->ring) (void)maage_audio_stream_destroy(music->ring);
    if (music->decoder) (void)maage_sound_stream_destroy(music->decoder);
    if (music->wake) (void)maage_condition_destroy(music->wake);
    if (music->mutex) (void)maage_mutex_destroy(music->mutex);
    free(music);
    return result;
}
static maage_status mn_music_prepare(maage_native_scene *scene,
    maage_native_scene_sound *sound, size_t budget)
{
    maage_native_music *music;
    maage_status result;
    unsigned int channels = 0, rate = 0;
    size_t bytes;
    music = (maage_native_music *)calloc(1, sizeof(*music));
    if (!music) return mn_status(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_CREATE);
    result = maage_sound_bank_stream_open(scene->bank->bank, sound->clip_id,
        &music->decoder, NULL);
    if (maage_status_failed(result)) { mn_music_release(music); return result; }
    result = maage_sound_stream_format(music->decoder, &channels, &rate);
    if (maage_status_failed(result)) { mn_music_release(music); return result; }
    bytes = MAAGE_NATIVE_STREAM_FRAMES * (size_t)channels * sizeof(float);
    if (bytes > budget) {
        mn_music_release(music);
        return mn_status(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_OPERATION_CREATE);
    }
    music->channels = channels;
    result = maage_audio_stream_create(scene->engine->audio, channels, rate,
        MAAGE_NATIVE_STREAM_FRAMES, &music->ring, NULL);
    if (maage_status_failed(result)) { mn_music_release(music); return result; }
    result = maage_mutex_create(&music->mutex);
    if (maage_status_failed(result)) { mn_music_release(music); return result; }
    result = maage_condition_create(&music->wake);
    if (maage_status_failed(result)) { mn_music_release(music); return result; }
    result = maage_thread_start(mn_music_worker, music, &music->worker);
    if (maage_status_failed(result)) { mn_music_release(music); return result; }
    sound->stream = music;
    sound->bytes = bytes;
    return mn_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_CREATE);
}
static maage_status mn_scene_sounds_reserve(maage_native_scene *scene, size_t need)
{
    maage_native_scene_sound *grown;
    size_t capacity = scene->sound_capacity ? scene->sound_capacity : 32U;
    if (need <= scene->sound_capacity) return mn_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_RESIZE);
    while (capacity < need) {
        if (capacity > ((size_t)-1) / 2U) return mn_status(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_OPERATION_RESIZE);
        capacity *= 2U;
    }
    if (capacity > ((size_t)-1) / sizeof(*grown) || capacity - 1U > ULONG_MAX)
        return mn_status(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_OPERATION_RESIZE);
    grown = (maage_native_scene_sound *)realloc(scene->sounds, capacity * sizeof(*grown));
    if (!grown) return mn_status(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_RESIZE);
    memset(grown + scene->sound_capacity, 0,
        (capacity - scene->sound_capacity) * sizeof(*grown));
    scene->sounds = grown;
    scene->sound_capacity = capacity;
    return mn_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_RESIZE);
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

/* Ambient assets use preloaded PCM; all music is registered by numeric ID
 * and decoded incrementally by a worker only when selected. The audio
 * callback performs neither file I/O nor decoding. */
/* Registration records a logical asset only. Presentation residency starts
 * on first playback, independently of registered sound count. */
static maage_status mn_scene_resolve(void *context, const void *catalog,
    const char *path, maage_client_audio_scene_sound_id *numeric)
{
    maage_native_scene *scene = (maage_native_scene *)context;
    maage_native_scene_sound *sound;
    maage_status result;
    size_t id;
    (void)catalog;
    if (numeric) *numeric = 0;
    if (!scene || !path || !numeric || scene->sound_count >= ULONG_MAX - 1UL)
        return mn_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_RESOLVE);
    id = scene->sound_count + 1U;
    result = mn_scene_sounds_reserve(scene, id + 1U);
    if (maage_status_failed(result)) return result;
    scene->sound_count = id;
    sound = &scene->sounds[id];
    if (scene->bank->next_id >= MAAGE_SOUND_MAX_CLIP_ID)
        return mn_status(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_OPERATION_REGISTER);
    sound->clip_id = ++scene->bank->next_id;
    result = maage_sound_bank_register(scene->bank->bank, sound->clip_id, path, NULL);
    if (maage_status_failed(result)) {
        sound->clip_id = 0;
        return result;
    }
    sound->music = strstr(path, "/music/") != NULL;
    sound->loop = !sound->music &&
        (strstr(path, "/weather/rain") != NULL ||
         strstr(path, "/liquid/water") != NULL ||
         strstr(path, "/liquid/lava.") != NULL);
    *numeric = (maage_client_audio_scene_sound_id)id;
    return mn_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_RESOLVE);
}
static maage_native_scene_sound *mn_scene_get(maage_native_scene *scene,
    maage_client_audio_scene_sound_id numeric)
{
    if (!scene || !numeric || numeric > scene->sound_count)
        return NULL;
    return scene->sounds[numeric].clip_id ? &scene->sounds[numeric] : NULL;
}
static int mn_scene_can_loop(void *context, const void *catalog,
    maage_client_audio_scene_sound_id numeric)
{
    maage_native_scene_sound *sound = mn_scene_get((maage_native_scene *)context, numeric);
    (void)catalog;
    return sound && sound->loop;
}
/* The Client presentation thread may materialize an effect on first use;
 * no asset decoding, allocation, or filesystem I/O occurs in the audio callback. */
static maage_status mn_scene_prepare_buffer(maage_native_scene *scene,
    maage_native_scene_sound *sound)
{
    maage_sound_pcm pcm;
    maage_audio_buffer *buffer = NULL;
    maage_status result;
    if (sound->buffer) return mn_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_LOAD);
    if (!sound->clip_id || sound->music)
        return mn_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_LOAD);
    memset(&pcm, 0, sizeof(pcm));
    result = maage_sound_bank_decode(scene->bank->bank, sound->clip_id, &pcm, NULL);
    if (maage_status_failed(result)) return result;
    result = maage_audio_buffer_create_float(scene->engine->audio, pcm.samples,
        pcm.frame_count, pcm.channels, pcm.sample_rate, &buffer, NULL);
    (void)maage_sound_pcm_destroy(&pcm);
    if (maage_status_failed(result)) return result;
    if (!buffer) return mn_status(MAAGE_STATUS_INVALID_STATE, MAAGE_OPERATION_LOAD);
    sound->buffer = buffer;
    return mn_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_LOAD);
}
static maage_status mn_scene_play_sound(void *context, void *audio,
    maage_client_audio_scene_sound_id numeric, unsigned int category,
    const double *position, double gain, int loop,
    maage_client_audio_scene_voice *voice)
{
    static const double origin[3] = {0.0, 0.0, 0.0};
    maage_native_scene *scene = (maage_native_scene *)context;
    maage_native_scene_sound *sound = mn_scene_get(scene, numeric);
    maage_status result;
    unsigned int bus;
    (void)audio;
    if (voice) *voice = 0;
    if (!sound || !voice || sound->music || (loop && !sound->loop))
        return mn_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_SUBMIT);
    result = mn_scene_prepare_buffer(scene, sound);
    if (maage_status_failed(result)) return result;
    bus = category == MAAGE_AUDIO_SCENE_WEATHER ? MAAGE_AUDIO_WEATHER :
        (category == MAAGE_AUDIO_SCENE_MUSIC ? MAAGE_AUDIO_MUSIC : MAAGE_AUDIO_AMBIENT);
    return mn_play(scene->engine, sound->buffer, NULL, bus, position != NULL,
        position ? position : origin, (float)gain, 1.0f, loop,
        (unsigned long *)voice, NULL);
}
static maage_status mn_scene_music_prepare(void *context, const void *catalog,
    maage_client_audio_scene_sound_id numeric, size_t max_bytes,
    void **pcm, size_t *bytes)
{
    maage_native_scene *scene = (maage_native_scene *)context;
    maage_native_scene_sound *sound = mn_scene_get(scene, numeric);
    maage_status result;
    (void)catalog;
    if (pcm) *pcm = NULL;
    if (bytes) *bytes = 0;
    if (!pcm || !bytes || !sound || !sound->music || !sound->clip_id)
        return mn_status(MAAGE_STATUS_NOT_FOUND, MAAGE_OPERATION_LOOKUP);
    if (sound->stream)
        return mn_status(MAAGE_STATUS_INVALID_STATE, MAAGE_OPERATION_DECODE);
    result = mn_music_prepare(scene, sound, max_bytes);
    if (maage_status_failed(result)) return result;
    *pcm = sound;
    *bytes = sound->bytes;
    return result;
}
static maage_status mn_scene_music_play(void *context, void *audio, void *pcm,
    double gain, maage_client_audio_scene_voice *voice)
{
    static const double zero[3] = {0.0, 0.0, 0.0};
    maage_native_scene_sound *sound = (maage_native_scene_sound *)pcm;
    maage_native_scene *scene = (maage_native_scene *)context;
    (void)audio;
    if (voice) *voice = 0;
    if (!scene || !sound || !sound->music || !sound->stream || !voice)
        return mn_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_SUBMIT);
    return mn_play(scene->engine, NULL, sound->stream->ring, MAAGE_AUDIO_MUSIC,
        0, zero, (float)gain, 1.0f, 0, (unsigned long *)voice, NULL);
}
static void mn_scene_music_release(void *context, void *pcm)
{
    maage_native_scene_sound *sound = (maage_native_scene_sound *)pcm;
    maage_status result;
    (void)context;
    if (!sound || !sound->stream) return;
    result = mn_music_release(sound->stream);
    sound->stream = NULL;
    sound->bytes = 0;
    if (maage_status_failed(result)) (void)maage_log_status(MAAGE_LOG_WARNING,
        "maage.client.audio_native", result,
        "Incremental Vorbis music decoding stopped due to an error");
}
static int mn_scene_voice_running(void *context, void *audio,
    maage_client_audio_scene_voice token)
{
    maage_native_scene *scene = (maage_native_scene *)context;
    maage_native_voice *voice;
    maage_audio_voice_state state = MAAGE_AUDIO_VOICE_STOPPED;
    (void)audio;
    if (!scene) return 0;
    voice = mn_voice_find(scene->engine, (unsigned long)token);
    if (!voice) return 0;
    (void)maage_audio_voice_get_state(scene->engine->audio, voice->native_id, &state);
    return state != MAAGE_AUDIO_VOICE_STOPPED;
}
static maage_status mn_scene_voice_gain(void *context, void *audio,
    maage_client_audio_scene_voice token, double gain)
{
    maage_native_scene *scene = (maage_native_scene *)context;
    maage_native_voice *voice;
    maage_status result;
    float clamped;
    (void)audio;
    if (!scene) return mn_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_SET);
    voice = mn_voice_find(scene->engine, (unsigned long)token);
    if (!voice) return mn_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_SET);
    clamped = gain < 0.0 ? 0.0f : (gain > MAAGE_NATIVE_MAX_VOLUME ? MAAGE_NATIVE_MAX_VOLUME : (float)gain);
    result = maage_audio_voice_set_gain(scene->engine->audio, voice->native_id, clamped);
    if (!maage_status_failed(result)) voice->playback.gain = clamped;
    return result;
}
static maage_status mn_scene_voice_pause(void *context, void *audio,
    maage_client_audio_scene_voice token, int paused)
{
    maage_native_scene *scene = (maage_native_scene *)context;
    maage_native_voice *voice;
    (void)audio;
    if (!scene) return mn_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_SET);
    voice = mn_voice_find(scene->engine, (unsigned long)token);
    if (!voice) return mn_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_SET);
    return maage_audio_voice_pause(scene->engine->audio, voice->native_id, paused);
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
    size_t i;
    (void)trace;
    if (!scene) return mn_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_DESTROY);
    first = maage_client_audio_scene_destroy(scene->scene);
    for (i = 1; i <= scene->sound_count; ++i) {
        if (scene->sounds[i].stream) mn_scene_music_release(scene, &scene->sounds[i]);
        if (scene->sounds[i].buffer) {
            result = maage_audio_buffer_destroy(scene->sounds[i].buffer);
            if (maage_status_failed(result)) return result;
            scene->sounds[i].buffer = NULL;
        }
    }
    free(scene->sounds);
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
