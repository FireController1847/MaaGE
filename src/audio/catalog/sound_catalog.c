#include "sound_catalog.h"
#include "maage/thread.h"
#include "maage/time.h"
#include "maage/log.h"
#include <stdlib.h>
#include <string.h>
#include <float.h>

typedef struct maage_cat_slot {
    const char *path; /* Borrowed immutable registry resource. */
    unsigned long buffer, clip_id;
    void *decoded;
    maage_status decode_result;
    struct maage_cat_slot *queue_next;
    double last_used, replay_at;
    double position[3];
    float gain, pitch, range;
    unsigned int bus;
    unsigned char positional, state, replay;
} maage_cat_slot;
struct maage_sound_catalog {
    void *bank, *audio;
    const maage_sound_registry *definitions;
    maage_sound_catalog_port port;
    maage_cat_slot *slots;
    size_t resource_count, pending, pending_capacity;
    unsigned int bound, prepared;
    maage_mutex *mutex;
    maage_condition *wake;
    maage_thread *worker;
    maage_cat_slot *queue_head, *queue_tail;
    double next_poll;
    int stopping;
};
static maage_status maage_cat_status(unsigned char code, unsigned char operation) {
    return maage_status_make(code, code < MAAGE_STATUS_WARNING ? 0 : MAAGE_CONTEXT_RESOURCE, operation, 0);
}
maage_status maage_sound_catalog_create(void *bank, void *audio, const maage_sound_registry *definitions, maage_sound_catalog **output, maage_status_trace *trace) {
    maage_sound_catalog *catalog;
    const maage_sound_resource_definition *resource;
    size_t i, count;
    if (output) *output = NULL;
    if (!output || !bank || !audio || !maage_sound_is_finalized(definitions)) return MAAGE_STATUS_PROPAGATE(trace, maage_cat_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_CREATE), MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_CREATE);
    count = maage_sound_resource_count(definitions);
    if (count >= (size_t) - 1 / sizeof(*catalog->slots)) return maage_cat_status(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_OPERATION_CREATE);
    catalog = (maage_sound_catalog *) calloc(1, sizeof(*catalog));
    if (!catalog) return maage_cat_status(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_CREATE);
    catalog->slots = (maage_cat_slot *) calloc(count + 1, sizeof(*catalog->slots));
    if (!catalog->slots) {
        free(catalog);
        return maage_cat_status(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_CREATE);
    }
    catalog->bank = bank;
    catalog->audio = audio;
    catalog->definitions = definitions;
    catalog->resource_count = count;
    catalog->pending_capacity = 64;
    for (i = 1; i <= count; ++i) {
        resource = maage_sound_resource_get(definitions, (maage_sound_resource_id) i);
        if (!resource->streamed) catalog->slots[i].path = resource->path;
    }
    *output = catalog;
    return maage_cat_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_CREATE);
}
maage_status maage_sound_catalog_pending_capacity(maage_sound_catalog *catalog, size_t capacity) {
    if (!catalog || catalog->prepared || !capacity) return maage_cat_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_SET);
    catalog->pending_capacity = capacity;
    return maage_cat_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_SET);
}
maage_status maage_sound_catalog_set_port(maage_sound_catalog *catalog, const maage_sound_catalog_port *port, maage_status_trace *trace) {
    if (!catalog || !port || !port->register_clip || !port->decode || !port->upload || !port->release_pcm || !port->release_buffer || !port->play) return MAAGE_STATUS_PROPAGATE(trace, maage_cat_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_REGISTER), MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_REGISTER);
    if (catalog->prepared || catalog->bound) return maage_cat_status(MAAGE_STATUS_INVALID_STATE, MAAGE_OPERATION_REGISTER);
    catalog->port = *port;
    catalog->bound = 1;
    return maage_cat_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_REGISTER);
}
/* The worker owns filesystem/decoder work; only the audio owner uploads PCM
 * and creates voices. Requests borrow stable catalog slot storage until join. */
static void maage_cat_decode_worker(void *context) {
    maage_sound_catalog *catalog = (maage_sound_catalog *) context;
    maage_cat_slot *slot;
    void *pcm;
    maage_status result;
    for (;;) {
        maage_mutex_lock(catalog->mutex);
        while (!catalog->stopping && !catalog->queue_head)
            maage_condition_wait(catalog->wake, catalog->mutex);
        if (catalog->stopping) {
            maage_mutex_unlock(catalog->mutex);
            return;
        }
        slot = catalog->queue_head;
        catalog->queue_head = slot->queue_next;
        if (!catalog->queue_head) catalog->queue_tail = NULL;
        slot->queue_next = NULL;
        slot->state = 2;
        maage_mutex_unlock(catalog->mutex);
        pcm = NULL;
        result = catalog->port.decode(catalog->bank, slot->clip_id, &pcm, NULL);
        maage_mutex_lock(catalog->mutex);
        slot->decoded = pcm;
        slot->decode_result = result;
        slot->state = 3;
        maage_mutex_unlock(catalog->mutex);
    }
}
maage_status maage_sound_catalog_prepare(maage_sound_catalog *catalog, maage_status_trace *trace) {
    size_t i;
    maage_cat_slot *slot;
    maage_status result;
    if (!catalog || !catalog->bound) return maage_cat_status(MAAGE_STATUS_INVALID_STATE, MAAGE_OPERATION_LOAD);
    if (catalog->prepared) return maage_cat_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_LOAD);
    /* Resource registration allocates metadata only. One reusable decoder owns
     * file/decoder work; only the audio owner uploads the resulting PCM. */
    for (i = 1; i <= catalog->resource_count; ++i) {
        slot = &catalog->slots[i];
        if (!slot->path) continue;
        result = catalog->port.register_clip(catalog->bank, slot->path, &slot->clip_id, trace);
        if (maage_status_failed(result)) {
            if (maage_status_code(result) == MAAGE_STATUS_ALLOCATION_FAILED || maage_status_code(result) == MAAGE_STATUS_BUFFER_OVERFLOW) return result;
            slot->state = 5;
            if (trace) maage_status_trace_recover(trace, maage_cat_status(MAAGE_STATUS_PARTIAL, MAAGE_OPERATION_LOAD), MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_LOAD, __FILE__, (unsigned long) __LINE__);
        }
    }
    result = maage_mutex_create(&catalog->mutex);
    if (maage_status_failed(result)) return result;
    result = maage_condition_create(&catalog->wake);
    if (!maage_status_failed(result)) result = maage_thread_start(maage_cat_decode_worker, catalog, &catalog->worker);
    if (maage_status_failed(result)) {
        if (catalog->wake) (void) maage_condition_destroy(catalog->wake);
        (void) maage_mutex_destroy(catalog->mutex);
        catalog->mutex = NULL;
        catalog->wake = NULL;
        return result;
    }
    catalog->prepared = 1;
    return maage_cat_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_LOAD);
}
maage_status maage_sound_catalog_poll(maage_sound_catalog *catalog, double now) {
    size_t i;
    maage_cat_slot *slot;
    void *pcm;
    maage_status result;
    unsigned long buffer;
    if (!catalog || !catalog->prepared || !(now >= 0)) return maage_cat_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_POLL);
    if (now < catalog->next_poll) return maage_cat_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_POLL);
    catalog->next_poll = now + 0.05;
    for (i = 1; i <= catalog->resource_count; ++i) {
        slot = &catalog->slots[i];
        if (!slot->path) continue;
        pcm = NULL;
        maage_mutex_lock(catalog->mutex);
        if (slot->state == 3) {
            pcm = slot->decoded;
            slot->decoded = NULL;
            result = slot->decode_result;
            slot->state = 0;
            --catalog->pending;
        } else result = maage_cat_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_POLL);
        maage_mutex_unlock(catalog->mutex);
        if (maage_status_code(result) != MAAGE_STATUS_NOOP) {
            buffer = 0;
            if (!maage_status_failed(result) && !pcm) result = maage_cat_status(MAAGE_STATUS_NOT_FOUND, MAAGE_OPERATION_DECODE);
            if (!maage_status_failed(result) && pcm) result = catalog->port.upload(catalog->audio, pcm, &buffer, NULL);
            if (pcm) catalog->port.release_pcm(catalog->bank, pcm);
            if (!maage_status_failed(result) && buffer) {
                slot->buffer = buffer;
                slot->state = 4;
                slot->last_used = now;
                if (slot->replay && now - slot->replay_at < 1.0) (void) catalog->port.play(catalog->audio, buffer, slot->bus, slot->positional, slot->position, slot->gain, slot->pitch, slot->range, NULL);
            } else {
                if (buffer) (void) catalog->port.release_buffer(catalog->audio, buffer, NULL);
                if (maage_status_code(result) != MAAGE_STATUS_ALLOCATION_FAILED && maage_status_code(result) != MAAGE_STATUS_BUFFER_OVERFLOW) slot->state = 5;
                if (slot->state == 5) maage_log_status(MAAGE_LOG_WARNING, "maage.client.audio.catalog", result, "Sound unavailable: %s", slot->path);
            }
            slot->replay = 0;
        }
        if (slot->buffer && now - slot->last_used >= 20.0) {
            result = catalog->port.release_buffer(catalog->audio, slot->buffer, NULL);
            if (!maage_status_failed(result)) {
                slot->buffer = 0;
                slot->state = 0;
            }
        }
    }
    return maage_cat_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_POLL);
}
static unsigned char maage_cat_decode_state(maage_sound_catalog *catalog, maage_cat_slot *slot) {
    unsigned char state;
    maage_mutex_lock(catalog->mutex);
    state = slot->state;
    maage_mutex_unlock(catalog->mutex);
    return state;
}
/* 32-bit arithmetic on C90 unsigned long, including 64-bit hosts. No global
 * PRNG state and no dependence on frame timing or preload order. */
static unsigned long maage_cat_mix(unsigned long h, unsigned long value) {
    return ((h ^ (value & 0xffffffffUL)) * 1664525UL + 1013904223UL) & 0xffffffffUL;
}
static unsigned long maage_cat_position_mix(unsigned long h, double coord) {
    double abs_coord = coord < 0.0 ? -coord : coord;
    unsigned long scaled;
    if (abs_coord > 100000000.0 || !(abs_coord == abs_coord)) abs_coord = 0.0;
    scaled = (unsigned long) (abs_coord * 16.0);
    return maage_cat_mix(h, scaled ^ (coord < 0.0 ? 0x80000000UL : 0UL));
}

maage_status maage_sound_catalog_play(maage_sound_catalog *catalog, const maage_sound_event *event, maage_status_trace *trace) {
    const maage_sound_definition *definition;
    maage_cat_slot *slot = NULL;
    size_t selected, attempt;
    unsigned long hash;
    float gain, pitch, range;
    double position[3], now;
    maage_status result;
    if (!catalog || !event || !catalog->bound || !catalog->prepared) return maage_cat_status(MAAGE_STATUS_INVALID_STATE, MAAGE_OPERATION_SUBMIT);
    if (!event->event_id) return maage_cat_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_SUBMIT);
    definition = maage_sound_get(catalog->definitions, event->event_id);
    if (!definition) return maage_cat_status(MAAGE_STATUS_NOT_FOUND, MAAGE_OPERATION_LOOKUP);
    if (!(event->intensity >= 0 && event->intensity <= FLT_MAX / 16.0 && event->pitch > 0 && event->pitch <= FLT_MAX / 4.0)) return maage_cat_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_SUBMIT);
    if (event->intensity == 0 || !definition->resource_count || !definition->gain) return maage_cat_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_SUBMIT);
    memcpy(position, event->position, sizeof(position));
    if (definition->positional) {
        for (attempt = 0; attempt < 3; ++attempt)
            if (!(position[attempt] >= -100000000.0 && position[attempt] <= 100000000.0)) return maage_cat_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_SUBMIT);
    } else memset(position, 0, sizeof(position));
    hash = maage_cat_mix(2166136261UL, event->tick);
    hash = maage_cat_mix(hash, event->sequence);
    hash = maage_cat_mix(hash, event->event_id * 31UL + event->material);
    hash = maage_cat_position_mix(hash, event->position[0]);
    hash = maage_cat_position_mix(hash, event->position[1]);
    hash = maage_cat_position_mix(hash, event->position[2]);
    hash ^= hash >> 16;
    hash = (hash * 2246822519UL) & 0xffffffffUL;
    hash ^= hash >> 13;
    hash = (hash * 3266489917UL) & 0xffffffffUL;
    hash ^= hash >> 16;
    selected = hash % definition->resource_count;
    for (attempt = 0; attempt < definition->resource_count; ++attempt) {
        slot = &catalog->slots[definition->resources[selected]];
        if (maage_cat_decode_state(catalog, slot) != 5) break;
        if (++selected == definition->resource_count) selected = 0;
    }
    if (attempt == definition->resource_count) return maage_cat_status(MAAGE_STATUS_NOT_FOUND, MAAGE_OPERATION_LOOKUP);
    if (definition->gain > FLT_MAX / event->intensity || definition->pitch > FLT_MAX / event->pitch) return maage_cat_status(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_OPERATION_SUBMIT);
    gain = definition->gain * (float) event->intensity;
    pitch = definition->pitch * (float) event->pitch;
    if (definition->distance > FLT_MAX / (gain > 1 ? gain : 1)) return maage_cat_status(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_OPERATION_SUBMIT);
    range = definition->distance * (gain > 1 ? gain : 1);
    if (gain > 1) gain = 1;
    now = maage_time_monotonic_seconds();
    if (slot->buffer) {
        slot->last_used = now;
        result = catalog->port.play(catalog->audio, slot->buffer, definition->bus, definition->positional, position, gain, pitch, range, trace);
        return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_SUBMIT);
    }
    maage_mutex_lock(catalog->mutex);
    if (!slot->state) {
        if (catalog->pending >= catalog->pending_capacity) {
            maage_mutex_unlock(catalog->mutex);
            return maage_cat_status(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_OPERATION_SUBMIT);
        }
        ++catalog->pending;
        slot->state = 1;
        slot->queue_next = NULL;
        if (catalog->queue_tail) catalog->queue_tail->queue_next = slot;
        else catalog->queue_head = slot;
        catalog->queue_tail = slot;
        maage_condition_signal(catalog->wake);
    }
    maage_mutex_unlock(catalog->mutex);
    if (!slot->replay) {
        slot->replay = 1;
        slot->replay_at = now;
        slot->bus = definition->bus;
        slot->positional = (unsigned char) definition->positional;
        memcpy(slot->position, position, sizeof(position));
        slot->gain = gain;
        slot->pitch = pitch;
        slot->range = range;
    }
    return maage_cat_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_SUBMIT);
}
maage_status maage_sound_catalog_destroy(maage_sound_catalog *catalog) {
    size_t i;
    maage_status first = maage_cat_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_DESTROY), result;
    if (!catalog) return maage_cat_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_DESTROY);
    if (catalog->mutex) {
        maage_mutex_lock(catalog->mutex);
        catalog->stopping = 1;
        if (catalog->wake) maage_condition_broadcast(catalog->wake);
        maage_mutex_unlock(catalog->mutex);
    }
    if (catalog->worker) {
        result = maage_thread_join(catalog->worker);
        if (maage_status_failed(result)) return result;
        catalog->worker = NULL;
    }
    if (catalog->bound)
        for (i = 1; i <= catalog->resource_count; ++i) {
            maage_cat_slot *slot = &catalog->slots[i];
            if (slot->decoded) {
                catalog->port.release_pcm(catalog->bank, slot->decoded);
                slot->decoded = NULL;
            }
            if (!slot->buffer) continue;
            result = catalog->port.release_buffer(catalog->audio, slot->buffer, NULL);
            if (maage_status_failed(result)) {
                if (!maage_status_failed(first)) first = result;
            } else slot->buffer = 0;
        }
    if (maage_status_failed(first)) return first;
    if (catalog->wake) (void) maage_condition_destroy(catalog->wake);
    if (catalog->mutex) (void) maage_mutex_destroy(catalog->mutex);
    free(catalog->slots);
    free(catalog);
    return first;
}
