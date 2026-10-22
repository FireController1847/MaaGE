#include "audio_internal.h"
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <math.h>

maage_status maage_audio_result(unsigned char code, unsigned char operation,
    unsigned char detail)
{
    return maage_status_make(code, MAAGE_CONTEXT_AUDIO, operation, detail);
}
static maage_status audio_trace(maage_status_trace *trace, maage_status result,
    unsigned char operation)
{
    if (!maage_status_is_success(result))
        return MAAGE_STATUS_TRACE(trace, result, MAAGE_CONTEXT_AUDIO, operation);
    return result;
}
static int finite_range(float value, float bound)
{
    return value == value && value >= -bound && value <= bound;
}
static int valid_vec(maage_audio_vec3 v)
{
    return finite_range(v.x, 1000000.0f) && finite_range(v.y, 1000000.0f) &&
        finite_range(v.z, 1000000.0f);
}
static maage_audio_vec3 cross(maage_audio_vec3 a, maage_audio_vec3 b)
{
    maage_audio_vec3 r;
    r.x = a.y * b.z - a.z * b.y;
    r.y = a.z * b.x - a.x * b.z;
    r.z = a.x * b.y - a.y * b.x;
    return r;
}
static float magnitude(maage_audio_vec3 v)
{
    return (float)sqrt((double)v.x*v.x + (double)v.y*v.y + (double)v.z*v.z);
}
static void free_buffer(maage_audio_buffer *buffer)
{
    free(buffer->samples);
    free(buffer);
}
void maage_audio_voice_release(maage_audio_voice *voice)
{
    if (voice->buffer) {
        if (voice->buffer->references) --voice->buffer->references;
        voice->buffer = NULL;
    }
    voice->id = 0;
}
void maage_audio_backend_fail(maage_audio *audio, maage_status result, long native)
{
    maage_audio_lock_enter(audio->lock);
    audio->device_ok = 0;
    audio->device_result = result;
    audio->device_native = native;
    maage_audio_lock_leave(audio->lock);
}
maage_status maage_audio_create(const maage_audio_config *config,
    maage_audio **output, maage_status_trace *trace)
{
    maage_audio *audio;
    maage_status result;
    unsigned int rate = 48000, count = 64, quantum = 256, i;
    if (!output) return audio_trace(trace, maage_audio_result(MAAGE_STATUS_INVALID_ARGUMENT,
        MAAGE_OPERATION_CREATE, 1), MAAGE_OPERATION_CREATE);
    *output = NULL;
    if (config) {
        if (config->sample_rate) rate = config->sample_rate;
        if (config->max_voices) count = config->max_voices;
        if (config->frames_per_buffer) quantum = config->frames_per_buffer;
    }
    if (rate < 8000 || rate > 192000 || count > 128 || !count ||
        quantum < 64 || quantum > 1024)
        return audio_trace(trace, maage_audio_result(MAAGE_STATUS_INVALID_ARGUMENT,
            MAAGE_OPERATION_CREATE, 0), MAAGE_OPERATION_CREATE);
    if (sizeof(signed short) != 2)
        return audio_trace(trace, maage_audio_result(MAAGE_STATUS_SYSTEM_NOT_SUPPORTED,
            MAAGE_OPERATION_CREATE, 0), MAAGE_OPERATION_CREATE);
    audio = (maage_audio *)calloc(1, sizeof(*audio));
    if (!audio) return audio_trace(trace, maage_audio_result(MAAGE_STATUS_ALLOCATION_FAILED,
        MAAGE_OPERATION_CREATE, 0), MAAGE_OPERATION_CREATE);
    result = maage_audio_lock_create(&audio->lock);
    if (maage_status_failed(result)) { free(audio); return audio_trace(trace, result, MAAGE_OPERATION_CREATE); }
    audio->voices = (maage_audio_voice *)calloc(count, sizeof(*audio->voices));
    if (!audio->voices) {
        maage_audio_lock_destroy(audio->lock); free(audio);
        return audio_trace(trace, maage_audio_result(MAAGE_STATUS_ALLOCATION_FAILED,
            MAAGE_OPERATION_CREATE, 0), MAAGE_OPERATION_CREATE);
    }
    audio->voice_count = count; audio->rate = rate; audio->quantum = quantum;
    for (i = 0; i < MAAGE_AUDIO_BUS_COUNT; ++i) audio->volume[i] = 1.0f;
    audio->right.x = 1.0f;
    audio->device_result = maage_audio_result(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_CREATE, 0);
    audio->device_ok = 1; /* Published before backend can run its worker. */
    result = maage_audio_backend_start(audio, trace);
    if (maage_status_failed(result)) {
        audio->device_result = result;
        audio->device_ok = 0;
        if (trace) maage_status_trace_record(trace, result, MAAGE_CONTEXT_AUDIO,
            MAAGE_OPERATION_ACQUIRE, __FILE__, (unsigned long)__LINE__, audio->device_native);
        *output = audio;
        return audio_trace(trace, maage_audio_result(MAAGE_STATUS_PARTIAL,
            MAAGE_OPERATION_CREATE, MAAGE_DETAIL_AUDIO_DEVICE_UNAVAILABLE), MAAGE_OPERATION_CREATE);
    }
    *output = audio;
    return maage_audio_result(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_CREATE, 0);
}
maage_status maage_audio_destroy(maage_audio *audio, maage_status_trace *trace)
{
    maage_audio_buffer *buffer, *next;
    unsigned int i;
    (void)trace;
    if (!audio) return maage_audio_result(MAAGE_STATUS_NOOP, MAAGE_OPERATION_DESTROY, 0);
    /* Backend is stopped and joined before any voice/buffer memory is freed. */
    maage_audio_backend_stop(audio);
    for (i = 0; i < audio->voice_count; ++i) maage_audio_voice_release(&audio->voices[i]);
    for (buffer = audio->buffers; buffer; buffer = next) {
        next = buffer->next;
        free_buffer(buffer);
    }
    free(audio->voices);
    maage_audio_lock_destroy(audio->lock);
    free(audio);
    return maage_audio_result(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_DESTROY, 0);
}
maage_status maage_audio_buffer_create(maage_audio *audio,
    const signed short *interleaved_pcm, size_t frame_count,
    unsigned int channels, unsigned int sample_rate,
    maage_audio_buffer **output, maage_status_trace *trace)
{
    maage_audio_buffer *buffer;
    size_t sample_count;
    if (!output) return audio_trace(trace, maage_audio_result(MAAGE_STATUS_INVALID_ARGUMENT,
        MAAGE_OPERATION_CREATE, 6), MAAGE_OPERATION_CREATE);
    *output = NULL;
    if (!audio || !interleaved_pcm || !frame_count || (channels != 1 && channels != 2) ||
        sample_rate < 8000 || sample_rate > 192000 ||
        frame_count > ((size_t)-1) / (channels * sizeof(signed short)))
        return audio_trace(trace, maage_audio_result(MAAGE_STATUS_INVALID_ARGUMENT,
            MAAGE_OPERATION_CREATE, MAAGE_DETAIL_ARGUMENT_UNKNOWN), MAAGE_OPERATION_CREATE);
    sample_count = frame_count * channels;
    buffer = (maage_audio_buffer *)calloc(1, sizeof(*buffer));
    if (!buffer) return audio_trace(trace, maage_audio_result(MAAGE_STATUS_ALLOCATION_FAILED,
        MAAGE_OPERATION_CREATE, 0), MAAGE_OPERATION_CREATE);
    buffer->samples = (signed short *)malloc(sample_count * sizeof(signed short));
    if (!buffer->samples) {
        free(buffer);
        return audio_trace(trace, maage_audio_result(MAAGE_STATUS_ALLOCATION_FAILED,
            MAAGE_OPERATION_CREATE, 0), MAAGE_OPERATION_CREATE);
    }
    memcpy(buffer->samples, interleaved_pcm, sample_count * sizeof(signed short));
    buffer->owner = audio; buffer->frames = frame_count;
    buffer->channels = channels; buffer->rate = sample_rate;
    maage_audio_lock_enter(audio->lock);
    buffer->next = audio->buffers; audio->buffers = buffer;
    maage_audio_lock_leave(audio->lock);
    *output = buffer;
    return maage_audio_result(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_CREATE, 0);
}
maage_status maage_audio_buffer_destroy(maage_audio_buffer *buffer)
{
    maage_audio_buffer **it;
    maage_audio *audio;
    int free_now;
    if (!buffer) return maage_audio_result(MAAGE_STATUS_NOOP, MAAGE_OPERATION_DESTROY, 0);
    audio = buffer->owner;
    maage_audio_lock_enter(audio->lock);
    if (buffer->retired) {
        maage_audio_lock_leave(audio->lock);
        return maage_audio_result(MAAGE_STATUS_NOOP, MAAGE_OPERATION_DESTROY, 0);
    }
    buffer->retired = 1;
    free_now = buffer->references == 0;
    if (free_now) {
        it = &audio->buffers;
        while (*it && *it != buffer) it = &(*it)->next;
        if (*it == buffer) *it = buffer->next;
    }
    maage_audio_lock_leave(audio->lock);
    if (free_now) free_buffer(buffer);
    return maage_audio_result(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_DESTROY, 0);
}
maage_status maage_audio_play(maage_audio *audio,
    const maage_audio_play_desc *desc, maage_audio_voice_id *voice_id,
    maage_status_trace *trace)
{
    maage_audio_voice *voice = NULL;
    unsigned int i;
    if (voice_id) *voice_id = 0;
    if (!audio || !desc || !voice_id || !desc->buffer ||
        desc->category <= MAAGE_AUDIO_MASTER || desc->category >= MAAGE_AUDIO_BUS_COUNT ||
        !finite_range(desc->gain, 4.0f) || desc->gain < 0.0f ||
        !finite_range(desc->pitch, 4.0f) || desc->pitch < 0.25f ||
        (desc->positional && !valid_vec(desc->position)))
        return audio_trace(trace, maage_audio_result(MAAGE_STATUS_INVALID_ARGUMENT,
            MAAGE_OPERATION_START, MAAGE_DETAIL_ARGUMENT_UNKNOWN), MAAGE_OPERATION_START);
    maage_audio_lock_enter(audio->lock);
    if (desc->buffer->owner != audio || desc->buffer->retired) {
        maage_audio_lock_leave(audio->lock);
        return audio_trace(trace, maage_audio_result(MAAGE_STATUS_INVALID_STATE,
            MAAGE_OPERATION_START, 0), MAAGE_OPERATION_START);
    }
    if (!audio->device_ok) {
        maage_audio_lock_leave(audio->lock);
        return audio_trace(trace, maage_audio_result(MAAGE_STATUS_PARTIAL,
            MAAGE_OPERATION_START, MAAGE_DETAIL_AUDIO_DEVICE_UNAVAILABLE), MAAGE_OPERATION_START);
    }
    for (i = 0; i < audio->voice_count; ++i) {
        if (!audio->voices[i].id) { voice = &audio->voices[i]; break; }
    }
    if (!voice) {
        maage_audio_lock_leave(audio->lock);
        return audio_trace(trace, maage_audio_result(MAAGE_STATUS_BUFFER_OVERFLOW,
            MAAGE_OPERATION_START, 0), MAAGE_OPERATION_START);
    }
    voice->generation = (voice->generation + 1UL) & 0xFFFFFFUL;
    if (!voice->generation) voice->generation = 1;
    voice->id = (maage_audio_voice_id)((voice->generation << 8) | (unsigned long)(i + 1));
    voice->buffer = desc->buffer; ++desc->buffer->references;
    voice->category = (unsigned int)desc->category;
    voice->gain = desc->gain; voice->pitch = desc->pitch;
    voice->positional = desc->positional != 0;
    voice->position = desc->position;
    voice->loop = desc->loop != 0;
    voice->cursor = 0.0;
    *voice_id = voice->id;
    maage_audio_lock_leave(audio->lock);
    return maage_audio_result(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_START, 0);
}
maage_status maage_audio_stop(maage_audio *audio, maage_audio_voice_id voice_id)
{
    unsigned int slot;
    maage_audio_voice *voice;
    if (!audio || !voice_id) return maage_audio_result(MAAGE_STATUS_INVALID_ARGUMENT,
        MAAGE_OPERATION_STOP, !audio ? 0 : 1);
    slot = ((unsigned int)voice_id & 0xFFU);
    if (!slot || slot > audio->voice_count)
        return maage_audio_result(MAAGE_STATUS_NOT_FOUND, MAAGE_OPERATION_STOP, 0);
    maage_audio_lock_enter(audio->lock);
    voice = &audio->voices[slot - 1];
    if (voice->id != voice_id) {
        maage_audio_lock_leave(audio->lock);
        return maage_audio_result(MAAGE_STATUS_NOOP, MAAGE_OPERATION_STOP, 0);
    }
    maage_audio_voice_release(voice);
    maage_audio_lock_leave(audio->lock);
    return maage_audio_result(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_STOP, 0);
}
maage_status maage_audio_set_listener(maage_audio *audio, maage_audio_vec3 position,
    maage_audio_vec3 forward, maage_audio_vec3 up)
{
    float len;
    maage_audio_vec3 right;
    if (!audio || !valid_vec(position) || !valid_vec(forward) || !valid_vec(up))
        return maage_audio_result(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_SET, 0);
    len = magnitude(forward);
    if (len < 0.0001f) return maage_audio_result(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_SET, 1);
    forward.x /= len; forward.y /= len; forward.z /= len;
    right = cross(forward, up);
    len = magnitude(right);
    if (len < 0.0001f) return maage_audio_result(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_SET, 2);
    right.x /= len; right.y /= len; right.z /= len;
    maage_audio_lock_enter(audio->lock);
    audio->listener = position; audio->right = right;
    maage_audio_lock_leave(audio->lock);
    return maage_audio_result(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_SET, 0);
}
maage_status maage_audio_set_volume(maage_audio *audio, maage_audio_bus bus, float gain)
{
    if (!audio || bus < 0 || bus >= MAAGE_AUDIO_BUS_COUNT ||
        !finite_range(gain, 4.0f) || gain < 0.0f)
        return maage_audio_result(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_SET, 0);
    maage_audio_lock_enter(audio->lock);
    audio->volume[bus] = gain;
    maage_audio_lock_leave(audio->lock);
    return maage_audio_result(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_SET, 0);
}
maage_status maage_audio_poll(maage_audio *audio, maage_status_trace *trace)
{
    maage_audio_buffer *it, **link, *garbage = NULL;
    maage_status failure;
    long native;
    if (!audio) return audio_trace(trace, maage_audio_result(MAAGE_STATUS_INVALID_ARGUMENT,
        MAAGE_OPERATION_POLL, 0), MAAGE_OPERATION_POLL);
    maage_audio_lock_enter(audio->lock);
    link = &audio->buffers;
    while (*link) {
        it = *link;
        if (it->retired && !it->references) {
            *link = it->next; it->next = garbage; garbage = it;
        } else link = &it->next;
    }
    failure = audio->device_result;
    native = audio->device_native;
    maage_audio_lock_leave(audio->lock);
    while (garbage) { it = garbage; garbage = garbage->next; free_buffer(it); }
    if (maage_status_failed(failure)) {
        if (trace) maage_status_trace_record(trace, failure, MAAGE_CONTEXT_AUDIO,
            MAAGE_OPERATION_PRESENT, __FILE__, (unsigned long)__LINE__, native);
        return audio_trace(trace, maage_audio_result(MAAGE_STATUS_PARTIAL,
            MAAGE_OPERATION_POLL, MAAGE_DETAIL_AUDIO_DEVICE_UNAVAILABLE), MAAGE_OPERATION_POLL);
    }
    return maage_audio_result(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_POLL, 0);
}
