#include "sound_catalog.h"
#include "sound_catalog_manifest.h"
#include "maage/thread.h"
#include "maage/time.h"
#include "maage/log.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <float.h>

#define MAAGE_CAT_EVENT_COUNT (MAAGE_SOUND_MAX + 1U)
#define MAAGE_CAT_MATERIAL_COUNT (MAAGE_MATERIAL_MAX + 1U)

/* Material families: dirt uses gravel; leaves use grass. Glass only
 * overrides destruction and placement, while metal changes stone's pitch. */
static const unsigned char maage_cat_step[MAAGE_CAT_MATERIAL_COUNT] = {
    MAAGE_CAT_STEP_STONE, MAAGE_CAT_STEP_STONE, MAAGE_CAT_STEP_GRASS,
    MAAGE_CAT_STEP_GRAVEL, MAAGE_CAT_STEP_WOOD, MAAGE_CAT_STEP_SAND,
    MAAGE_CAT_STEP_GRAVEL, MAAGE_CAT_STEP_SNOW, MAAGE_CAT_STEP_STONE,
    MAAGE_CAT_STEP_STONE, MAAGE_CAT_NONE, MAAGE_CAT_NONE,
    MAAGE_CAT_STEP_GRASS, MAAGE_CAT_STEP_CLOTH, MAAGE_CAT_STEP_LADDER,
    MAAGE_CAT_NONE};
static const unsigned char maage_cat_dig[MAAGE_CAT_MATERIAL_COUNT] = {
    MAAGE_CAT_DIG_STONE, MAAGE_CAT_DIG_STONE, MAAGE_CAT_DIG_GRASS,
    MAAGE_CAT_DIG_GRAVEL, MAAGE_CAT_DIG_WOOD, MAAGE_CAT_DIG_SAND,
    MAAGE_CAT_DIG_GRAVEL, MAAGE_CAT_DIG_SNOW, MAAGE_CAT_DIG_STONE,
    MAAGE_CAT_GLASS, MAAGE_CAT_NONE, MAAGE_CAT_NONE,
    MAAGE_CAT_DIG_GRASS, MAAGE_CAT_DIG_CLOTH, MAAGE_CAT_DIG_WOOD,
    MAAGE_CAT_DIG_STONE};

typedef struct maage_cat_rule {
    unsigned char family, material_map, bus, positional;
    float gain, pitch;
} maage_cat_rule;
/* The producer supplies only action-specific speed/randomization multipliers.
 * map 1=step, 2=break, 3=placement; no generic pitch jitter is applied. */
static const maage_cat_rule maage_cat_rules[MAAGE_CAT_EVENT_COUNT] = {
    {0, 0, 0, 0, 0.0f, 0.0f},
    {0, 1, MAAGE_SOUND_CATALOG_PLAYERS, 1, 0.15f, 1.00f},
    {0, 2, MAAGE_SOUND_CATALOG_BLOCKS, 1, 1.00f, 0.80f},
    {0, 3, MAAGE_SOUND_CATALOG_BLOCKS, 1, 1.00f, 0.80f},
    {0, 0, 0, 0, 0.0f, 0.0f}, /* Ordinary jumps are silent. */
    {0, 1, MAAGE_SOUND_CATALOG_PLAYERS, 1, 0.50f, 0.75f},
    {MAAGE_CAT_HURT, 0, MAAGE_SOUND_CATALOG_PLAYERS, 1, 1.00f, 1.00f},
    {MAAGE_CAT_SWIM, 0, MAAGE_SOUND_CATALOG_PLAYERS, 1, 1.00f, 1.00f},
    {0, 0, 0, 0, 0.0f, 0.0f}, /* Inventory manipulation is silent. */
    {MAAGE_CAT_CLICK, 0, MAAGE_SOUND_CATALOG_UI, 0, 0.25f, 1.00f},
    {MAAGE_CAT_CAVE, 0, MAAGE_SOUND_CATALOG_AMBIENT, 1, 0.70f, 1.00f},
    {MAAGE_CAT_RAIN, 0, MAAGE_SOUND_CATALOG_WEATHER, 1, 1.00f, 1.00f},
    {MAAGE_CAT_THUNDER, 0, MAAGE_SOUND_CATALOG_WEATHER, 1, 1.00f, 1.00f},
    {MAAGE_CAT_WATER, 0, MAAGE_SOUND_CATALOG_BLOCKS, 1, 1.00f, 1.00f},
    {MAAGE_CAT_LAVA, 0, MAAGE_SOUND_CATALOG_BLOCKS, 1, 1.00f, 1.00f},
    {0, 1, MAAGE_SOUND_CATALOG_BLOCKS, 1, 0.25f, 0.50f},
    {MAAGE_CAT_SPLASH, 0, MAAGE_SOUND_CATALOG_BLOCKS, 1, 1.00f, 1.00f},
    {MAAGE_CAT_POP, 0, MAAGE_SOUND_CATALOG_PLAYERS, 1, 1.00f, 1.00f},
    {MAAGE_CAT_FALL_SMALL, 0, MAAGE_SOUND_CATALOG_PLAYERS, 1, 1.00f, 1.00f},
    {MAAGE_CAT_FALL_BIG, 0, MAAGE_SOUND_CATALOG_PLAYERS, 1, 1.00f, 1.00f},
    {MAAGE_CAT_HURT, 0, MAAGE_SOUND_CATALOG_PLAYERS, 1, 1.00f, 1.00f},
    {MAAGE_CAT_ITEM_BREAK, 0, MAAGE_SOUND_CATALOG_PLAYERS, 1, 1.00f, 1.00f},
    {MAAGE_CAT_EAT, 0, MAAGE_SOUND_CATALOG_PLAYERS, 1, 1.00f, 1.00f},
    {MAAGE_CAT_BURP, 0, MAAGE_SOUND_CATALOG_PLAYERS, 1, 1.00f, 1.00f}};

typedef struct maage_cat_slot {
    char path[MAAGE_CAT_PATH_LIMIT];
    unsigned long buffer, clip_id;
    void *decoded;
    maage_status decode_result;
    struct maage_cat_slot *queue_next;
    double last_used, replay_at;
    double position[3];
    float gain, pitch, range;
    unsigned char bus, positional, state, replay;
} maage_cat_slot;
typedef struct maage_cat_family {
    maage_cat_slot variants[MAAGE_CAT_VARIANT_LIMIT];
    unsigned char count, ready, active[MAAGE_CAT_VARIANT_LIMIT];
} maage_cat_family;
struct maage_sound_catalog {
    void *bank, *audio; /* both borrowed; never freed by this catalog */
    maage_sound_catalog_port port;
    maage_cat_family family[MAAGE_CAT_FAMILY_COUNT];
    unsigned int bound, prepared;
    maage_mutex *mutex;
    maage_condition *wake;
    maage_thread *worker;
    maage_cat_slot *queue_head, *queue_tail;
    double next_poll;
    int stopping;
};

static maage_status maage_cat_status(unsigned char code, unsigned char op) {
    return maage_status_make(code, code < 0x40 ? 0 : MAAGE_CONTEXT_RESOURCE, op, 0);
}

maage_status maage_sound_catalog_create(void *sound_bank, void *audio, maage_sound_catalog **output, maage_status_trace *trace) {
    maage_sound_catalog *catalog;
    unsigned int f, i;
    const maage_catalog_manifest_entry *entry;
    size_t length;
    if (!output || !sound_bank || !audio) return MAAGE_STATUS_PROPAGATE(trace, maage_cat_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_CREATE), MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_CREATE);
    *output = NULL;
    catalog = (maage_sound_catalog *) calloc(1, sizeof(*catalog));
    if (!catalog) return MAAGE_STATUS_PROPAGATE(trace, maage_cat_status(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_CREATE), MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_CREATE);
    catalog->bank = sound_bank;
    catalog->audio = audio;
    for (f = 1; f < MAAGE_CAT_FAMILY_COUNT; ++f) {
        entry = &maage_catalog_manifest[f];
        if (!entry->stem || !entry->count || entry->count > MAAGE_CAT_VARIANT_LIMIT) break;
        length = strlen(entry->stem);
        /* Prefix + optional 2-digit numeric suffix + .ogg + NUL. */
        if (length + sizeof("minecraft/sounds/") + 2U + sizeof(".ogg") >= MAAGE_CAT_PATH_LIMIT) break;
        catalog->family[f].count = entry->count;
        for (i = 0; i < entry->count; ++i) {
            if (!entry->first && !i) sprintf(catalog->family[f].variants[i].path, "minecraft/sounds/%s.ogg", entry->stem);
            else
                sprintf(catalog->family[f].variants[i].path, "minecraft/sounds/%s%u.ogg", entry->stem, entry->first ? (unsigned int) entry->first + i : i + 1U);
        }
    }
    if (f != MAAGE_CAT_FAMILY_COUNT) {
        free(catalog);
        return MAAGE_STATUS_PROPAGATE(trace, maage_cat_status(MAAGE_STATUS_INVALID_STATE, MAAGE_OPERATION_REGISTER), MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_CREATE);
    }
    *output = catalog;
    return maage_cat_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_CREATE);
}

maage_status maage_sound_catalog_set_port(maage_sound_catalog *catalog, const maage_sound_catalog_port *port, maage_status_trace *trace) {
    if (!catalog || !port || !port->register_clip || !port->decode || !port->upload || !port->release_pcm || !port->release_buffer || !port->play) return MAAGE_STATUS_PROPAGATE(trace, maage_cat_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_REGISTER), MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_REGISTER);
    if (catalog->prepared || catalog->bound) return MAAGE_STATUS_PROPAGATE(trace, maage_cat_status(MAAGE_STATUS_INVALID_STATE, MAAGE_OPERATION_REGISTER), MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_REGISTER);
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
        while (!catalog->stopping && !catalog->queue_head) maage_condition_wait(catalog->wake, catalog->mutex);
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
    unsigned int f, i;
    maage_status result;
    if (!catalog || !catalog->bound) return MAAGE_STATUS_PROPAGATE(trace, maage_cat_status(MAAGE_STATUS_INVALID_STATE, MAAGE_OPERATION_LOAD), MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_LOAD);
    if (catalog->prepared) return maage_cat_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_LOAD);
    /* Register immutable paths before any decoder worker can inspect the
     * SoundBank. The registration phase allocates no decoded PCM. */
    for (f = 1; f < MAAGE_CAT_FAMILY_COUNT; ++f)
        for (i = 0; i < catalog->family[f].count; ++i) {
            maage_cat_slot *slot = &catalog->family[f].variants[i];
            result = catalog->port.register_clip(catalog->bank, slot->path, &slot->clip_id, trace);
            if (maage_status_failed(result)) {
                /* Optional resources are not a reason to silence the entire
                 * catalog; a missing path simply disables this variant. */
                if (maage_status_code(result) == MAAGE_STATUS_ALLOCATION_FAILED ||
                    maage_status_code(result) == MAAGE_STATUS_BUFFER_OVERFLOW) return result;
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
    for (f = 1; f < MAAGE_CAT_FAMILY_COUNT; ++f) {
        maage_cat_family *family = &catalog->family[f];
        family->ready = family->count;
        for (i = 0; i < family->count; ++i) family->active[i] = (unsigned char) i;
    }
    catalog->prepared = 1;
    return maage_cat_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_LOAD);
}
/* A completed decode is uploaded only on the audio owner, never on the worker
 * or the device callback. Retired buffers stay valid for active mixer voices. */
maage_status maage_sound_catalog_poll(maage_sound_catalog *catalog, double now) {
    unsigned int f, i;
    maage_cat_slot *slot;
    void *pcm;
    maage_status result;
    unsigned long buffer;
    if (!catalog || !catalog->prepared || !(now >= 0.0)) return maage_cat_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_POLL);
    if (now < catalog->next_poll) return maage_cat_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_POLL);
    catalog->next_poll = now + 0.05;
    for (f = 1; f < MAAGE_CAT_FAMILY_COUNT; ++f)
        for (i = 0; i < catalog->family[f].count; ++i) {
            slot = &catalog->family[f].variants[i];
            pcm = NULL;
            maage_mutex_lock(catalog->mutex);
            if (slot->state == 3) {
                pcm = slot->decoded;
                slot->decoded = NULL;
                result = slot->decode_result;
                slot->state = 0;
            } else
                result = maage_cat_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_POLL);
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
                    if (slot->replay && now - slot->replay_at < 1.0) {
                        (void) catalog->port.play(catalog->audio, buffer, slot->bus, slot->positional, slot->position, slot->gain, slot->pitch, slot->range, NULL);
                    }
                } else {
                    if (buffer) (void) catalog->port.release_buffer(catalog->audio, buffer, NULL);
                    if (maage_status_code(result) != MAAGE_STATUS_ALLOCATION_FAILED && maage_status_code(result) != MAAGE_STATUS_BUFFER_OVERFLOW) slot->state = 5;
                    if (slot->state == 5) maage_log_status(MAAGE_LOG_WARNING, "maage.audio.catalog", result, "Sound unavailable: %s", slot->path);
                }
                slot->replay = 0;
            }
            if (slot->buffer && now - slot->last_used >= 20.0) {
                (void) catalog->port.release_buffer(catalog->audio, slot->buffer, NULL);
                slot->buffer = 0;
                slot->state = 0;
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
    const maage_cat_rule *rule;
    maage_cat_family *family;
    unsigned int event_id, material;
    unsigned char selected;
    unsigned long hash;
    float gain, pitch, volume, range;
    double position[3];
    maage_status result;
    if (!catalog || !event || !catalog->bound || !catalog->prepared) return MAAGE_STATUS_PROPAGATE(trace, maage_cat_status(MAAGE_STATUS_INVALID_STATE, MAAGE_OPERATION_SUBMIT), MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_SUBMIT);
    event_id = event->event_id;
    if (!event_id || event_id >= MAAGE_CAT_EVENT_COUNT) return MAAGE_STATUS_PROPAGATE(trace, maage_cat_status(MAAGE_STATUS_NOT_FOUND, MAAGE_OPERATION_LOOKUP), MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_SUBMIT);
    if (!(event->intensity >= 0.0) || event->intensity > (double) FLT_MAX / 16.0 || !(event->pitch >= 0.0) || event->pitch > (double) FLT_MAX / 2.0) return MAAGE_STATUS_PROPAGATE(trace, maage_cat_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_SUBMIT), MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_SUBMIT);
    if (event->intensity <= 0.0f) return maage_cat_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_SUBMIT);
    rule = &maage_cat_rules[event_id];
    material = event->material;
    if (material >= MAAGE_CAT_MATERIAL_COUNT) return MAAGE_STATUS_PROPAGATE(trace, maage_cat_status(MAAGE_STATUS_NOT_FOUND, MAAGE_OPERATION_LOOKUP), MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_SUBMIT);
    selected = rule->family;
    if (rule->material_map == 1) selected = maage_cat_step[material];
    else if (rule->material_map >= 2) {
        selected = maage_cat_dig[material];
        if (rule->material_map == 3 && material == MAAGE_MATERIAL_GLASS) selected = MAAGE_CAT_STEP_STONE;
        else if (rule->material_map == 3 && material == MAAGE_MATERIAL_ANVIL) selected = MAAGE_CAT_ANVIL_LAND;
    }
    if (!selected) return maage_cat_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_SUBMIT);
    family = &catalog->family[selected];
    if (!family->ready) return MAAGE_STATUS_PROPAGATE(trace, maage_cat_status(MAAGE_STATUS_NOT_FOUND, MAAGE_OPERATION_LOOKUP), MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_SUBMIT);
    position[0] = event->position[0];
    position[1] = event->position[1];
    position[2] = event->position[2];
    if (rule->positional) {
        if (position[0] != position[0] || position[1] != position[1] ||
            position[2] != position[2] || position[0] > 100000000.0 ||
            position[0] < -100000000.0 || position[1] > 100000000.0 ||
            position[1] < -100000000.0 || position[2] > 100000000.0 ||
            position[2] < -100000000.0)
            return MAAGE_STATUS_PROPAGATE(trace, maage_cat_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_SUBMIT), MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_SUBMIT);
    } else
        position[0] = position[1] = position[2] = 0.0;
    hash = maage_cat_mix(2166136261UL, (unsigned long) event->tick);
    hash = maage_cat_mix(hash, (unsigned long) event->sequence);
    hash = maage_cat_mix(hash, (unsigned long) event_id * 31UL + material);
    hash = maage_cat_position_mix(hash, event->position[0]);
    hash = maage_cat_position_mix(hash, event->position[1]);
    hash = maage_cat_position_mix(hash, event->position[2]);
    hash ^= hash >> 16;
    hash = (hash * 2246822519UL) & 0xffffffffUL;
    hash ^= hash >> 13;
    hash = (hash * 3266489917UL) & 0xffffffffUL;
    hash ^= hash >> 16;
    selected = family->active[hash % family->ready];
    if (maage_cat_decode_state(catalog, &family->variants[selected]) == 5) {
        unsigned int attempt;
        for (attempt = 1; attempt < family->ready; ++attempt) {
            unsigned char alternative = family->active[(hash % family->ready + attempt) % family->ready];
            if (maage_cat_decode_state(catalog, &family->variants[alternative]) != 5) {
                selected = alternative;
                break;
            }
        }
        if (maage_cat_decode_state(catalog, &family->variants[selected]) == 5) return maage_cat_status(MAAGE_STATUS_NOT_FOUND, MAAGE_OPERATION_LOOKUP);
    }
    volume = material == MAAGE_MATERIAL_ANVIL ? 0.3f : 1.0f;
    gain = rule->gain;
    pitch = rule->pitch;
    if (rule->material_map) {
        if (event_id == MAAGE_SOUND_BLOCK_BREAK || event_id == MAAGE_SOUND_BLOCK_PLACE) gain = (volume + 1.0f) * 0.5f;
        else if (event_id == MAAGE_SOUND_BLOCK_DIG) gain = (volume + 1.0f) * 0.125f;
        else
            gain *= volume;
        if (material == MAAGE_MATERIAL_METAL) pitch *= 1.5f;
    }
    gain *= (float) event->intensity;
    pitch *= (float) (event->pitch > 0.0 ? event->pitch : 1.0);
    /* Expand linear attenuation using the original volume,
     * then clamps the audible gain. Category/master gain is applied by Audio. */
    range = 16.0f * (gain > 1.0f ? gain : 1.0f);
    if (gain > 1.0f) gain = 1.0f;
    if (pitch < 0.5f) pitch = 0.5f;
    if (pitch > 2.0f) pitch = 2.0f;
    {
        maage_cat_slot *slot = &family->variants[selected];
        double now = maage_time_monotonic_seconds();
        if (slot->buffer) {
            slot->last_used = now;
            result = catalog->port.play(catalog->audio, slot->buffer, rule->bus, rule->positional, position, gain, pitch, range, trace);
            if (maage_status_failed(result)) return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_SUBMIT);
            return result;
        }
        maage_mutex_lock(catalog->mutex);
        if (!slot->state) {
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
            slot->bus = rule->bus;
            slot->positional = rule->positional;
            memcpy(slot->position, position, sizeof(position));
            slot->gain = gain;
            slot->pitch = pitch;
            slot->range = range;
        }
        return maage_cat_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_SUBMIT);
    }
}

maage_status maage_sound_catalog_destroy(maage_sound_catalog *catalog) {
    unsigned int f, i;
    maage_status first = maage_cat_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_DESTROY);
    maage_status result;
    if (!catalog) return maage_cat_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_DESTROY);
    if (catalog->mutex) {
        maage_mutex_lock(catalog->mutex);
        catalog->stopping = 1;
        if (catalog->wake) maage_condition_broadcast(catalog->wake);
        maage_mutex_unlock(catalog->mutex);
    }
    if (catalog->worker) (void) maage_thread_join(catalog->worker);
    if (catalog->bound)
        for (f = 1; f < MAAGE_CAT_FAMILY_COUNT; ++f)
            for (i = 0; i < catalog->family[f].count; ++i) {
                maage_cat_slot *slot = &catalog->family[f].variants[i];
                if (slot->decoded) catalog->port.release_pcm(catalog->bank, slot->decoded);
                if (!slot->buffer) continue;
                result = catalog->port.release_buffer(catalog->audio, slot->buffer, NULL);
                if (maage_status_failed(result) && !maage_status_failed(first)) first = result;
            }
    if (catalog->wake) (void) maage_condition_destroy(catalog->wake);
    if (catalog->mutex) (void) maage_mutex_destroy(catalog->mutex);
    free(catalog);
    return first;
}
