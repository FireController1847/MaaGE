#include "sound_catalog.h"
#include "sound_catalog_manifest.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define MAAGE_CAT_EVENT_COUNT 15U
#define MAAGE_CAT_MATERIAL_COUNT 13U

/* The stable event/material ordinals are provided by session_protocol.h. The
 * following are mapping tables indexed by those ordinals, not ID registries. */
static const unsigned char maage_cat_step[MAAGE_CAT_MATERIAL_COUNT] = {
    MAAGE_CAT_STEP_STONE, MAAGE_CAT_STEP_STONE, MAAGE_CAT_STEP_GRASS,
    MAAGE_CAT_STEP_GRASS, MAAGE_CAT_STEP_WOOD, MAAGE_CAT_STEP_SAND,
    MAAGE_CAT_STEP_GRAVEL, MAAGE_CAT_STEP_SNOW, MAAGE_CAT_STEP_STONE,
    MAAGE_CAT_STEP_STONE, MAAGE_CAT_SWIM, MAAGE_CAT_STEP_STONE,
    MAAGE_CAT_STEP_GRASS
};
static const unsigned char maage_cat_dig[MAAGE_CAT_MATERIAL_COUNT] = {
    MAAGE_CAT_DIG_STONE, MAAGE_CAT_DIG_STONE, MAAGE_CAT_DIG_GRASS,
    MAAGE_CAT_DIG_GRAVEL, MAAGE_CAT_DIG_WOOD, MAAGE_CAT_DIG_SAND,
    MAAGE_CAT_DIG_GRAVEL, MAAGE_CAT_DIG_SNOW, MAAGE_CAT_DIG_STONE,
    MAAGE_CAT_GLASS, MAAGE_CAT_SPLASH, MAAGE_CAT_LAVA_POP,
    MAAGE_CAT_DIG_CLOTH
};

typedef struct maage_cat_rule {
    unsigned char family, material_map, bus, positional;
    float gain, pitch, range;
} maage_cat_rule;
/* Indices 1..14 correspond to Agent 6's stable protocol ordinals. Values are
 * behavior parameters, not new protocol identities. map 1=step, 2=dig. */
static const maage_cat_rule maage_cat_rules[MAAGE_CAT_EVENT_COUNT] = {
    {0, 0, 0, 0, 0.0f, 0.0f, 0.0f},
    {MAAGE_CAT_STEP_STONE, 1, MAAGE_SOUND_CATALOG_PLAYERS, 1, 0.40f, 1.00f, 16.0f},
    {MAAGE_CAT_DIG_STONE, 2, MAAGE_SOUND_CATALOG_BLOCKS, 1, 0.95f, 0.95f, 24.0f},
    {MAAGE_CAT_DIG_STONE, 2, MAAGE_SOUND_CATALOG_BLOCKS, 1, 0.70f, 0.83f, 24.0f},
    {MAAGE_CAT_STEP_STONE, 1, MAAGE_SOUND_CATALOG_PLAYERS, 1, 0.23f, 1.05f, 12.0f},
    {MAAGE_CAT_FALL_SMALL, 0, MAAGE_SOUND_CATALOG_PLAYERS, 1, 0.70f, 1.00f, 24.0f},
    {MAAGE_CAT_HURT, 0, MAAGE_SOUND_CATALOG_PLAYERS, 1, 1.00f, 1.00f, 24.0f},
    {MAAGE_CAT_SWIM, 0, MAAGE_SOUND_CATALOG_PLAYERS, 1, 0.48f, 1.00f, 20.0f},
    {MAAGE_CAT_CLICK, 0, MAAGE_SOUND_CATALOG_UI, 0, 0.50f, 1.00f, 0.0f},
    {MAAGE_CAT_CLICK, 0, MAAGE_SOUND_CATALOG_UI, 0, 0.35f, 1.00f, 0.0f},
    {MAAGE_CAT_CAVE, 0, MAAGE_SOUND_CATALOG_AMBIENT, 1, 0.70f, 1.00f, 48.0f},
    {MAAGE_CAT_RAIN, 0, MAAGE_SOUND_CATALOG_WEATHER, 0, 0.60f, 1.00f, 0.0f},
    {MAAGE_CAT_THUNDER, 0, MAAGE_SOUND_CATALOG_WEATHER, 1, 1.00f, 1.00f, 256.0f},
    {MAAGE_CAT_WATER, 0, MAAGE_SOUND_CATALOG_AMBIENT, 1, 0.55f, 1.00f, 28.0f},
    {MAAGE_CAT_LAVA, 0, MAAGE_SOUND_CATALOG_AMBIENT, 1, 0.58f, 1.00f, 28.0f}
};

typedef struct maage_cat_slot {
    char path[MAAGE_CAT_PATH_LIMIT];
    unsigned long buffer;
} maage_cat_slot;
typedef struct maage_cat_family {
    maage_cat_slot variants[MAAGE_CAT_VARIANT_LIMIT];
    unsigned char count, ready, active[MAAGE_CAT_VARIANT_LIMIT];
} maage_cat_family;
struct maage_sound_catalog {
    void *bank, *audio; /* both borrowed; never freed by this catalog */
    maage_sound_catalog_port port;
    maage_cat_family family[MAAGE_CAT_FAMILY_COUNT];
    unsigned int bound, preloaded;
};

static maage_status maage_cat_status(unsigned char code, unsigned char op)
{ return maage_status_make(code, code < 0x40 ? 0 : MAAGE_CONTEXT_RESOURCE, op, 0); }

maage_status maage_sound_catalog_create(void *sound_bank, void *audio,
    maage_sound_catalog **output, maage_status_trace *trace)
{
    maage_sound_catalog *catalog;
    unsigned int f, i;
    const maage_catalog_manifest_entry *entry;
    size_t length;
    if (!output || !sound_bank || !audio)
        return MAAGE_STATUS_PROPAGATE(trace, maage_cat_status(MAAGE_STATUS_INVALID_ARGUMENT,
            MAAGE_OPERATION_CREATE), MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_CREATE);
    *output = NULL;
    catalog = (maage_sound_catalog *)calloc(1, sizeof(*catalog));
    if (!catalog) return MAAGE_STATUS_PROPAGATE(trace,
        maage_cat_status(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_CREATE),
        MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_CREATE);
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
            if (!entry->first) sprintf(catalog->family[f].variants[i].path,
                "minecraft/sounds/%s.ogg", entry->stem);
            else sprintf(catalog->family[f].variants[i].path,
                "minecraft/sounds/%s%u.ogg", entry->stem, (unsigned int)entry->first + i);
        }
    }
    if (f != MAAGE_CAT_FAMILY_COUNT) {
        free(catalog);
        return MAAGE_STATUS_PROPAGATE(trace, maage_cat_status(MAAGE_STATUS_INVALID_STATE,
            MAAGE_OPERATION_REGISTER), MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_CREATE);
    }
    *output = catalog;
    return maage_cat_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_CREATE);
}

maage_status maage_sound_catalog_set_port(maage_sound_catalog *catalog,
    const maage_sound_catalog_port *port, maage_status_trace *trace)
{
    if (!catalog || !port || !port->decode || !port->upload || !port->release_pcm ||
        !port->release_buffer || !port->play)
        return MAAGE_STATUS_PROPAGATE(trace, maage_cat_status(MAAGE_STATUS_INVALID_ARGUMENT,
            MAAGE_OPERATION_REGISTER), MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_REGISTER);
    if (catalog->preloaded || catalog->bound)
        return MAAGE_STATUS_PROPAGATE(trace, maage_cat_status(MAAGE_STATUS_INVALID_STATE,
            MAAGE_OPERATION_REGISTER), MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_REGISTER);
    catalog->port = *port;
    catalog->bound = 1;
    return maage_cat_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_REGISTER);
}

maage_status maage_sound_catalog_preload(maage_sound_catalog *catalog,
    maage_status_trace *trace)
{
    unsigned int f, i, missing = 0, loaded = 0;
    maage_cat_family *family;
    maage_status result, first_failure = 0;
    void *pcm;
    unsigned long buffer;
    if (!catalog || !catalog->bound)
        return MAAGE_STATUS_PROPAGATE(trace, maage_cat_status(MAAGE_STATUS_INVALID_STATE,
            MAAGE_OPERATION_LOAD), MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_LOAD);
    if (catalog->preloaded) return maage_cat_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_LOAD);
    catalog->preloaded = 1;
    for (f = 1; f < MAAGE_CAT_FAMILY_COUNT; ++f) {
        family = &catalog->family[f];
        for (i = 0; i < family->count; ++i) {
            pcm = NULL;
            buffer = 0;
            result = catalog->port.decode(catalog->bank, family->variants[i].path, &pcm, trace);
            if (!maage_status_failed(result) && pcm) {
                result = catalog->port.upload(catalog->audio, pcm, &buffer, trace);
                catalog->port.release_pcm(catalog->bank, pcm);
            } else if (pcm) catalog->port.release_pcm(catalog->bank, pcm);
            if (maage_status_failed(result) || !buffer) {
                if (!first_failure) first_failure = maage_status_failed(result) ? result :
                    maage_cat_status(MAAGE_STATUS_NOT_FOUND, MAAGE_OPERATION_LOAD);
                /* An adapter must not hand back an owned buffer on error. If
                 * it does, release that buffer rather than leak it. */
                if (buffer) catalog->port.release_buffer(catalog->audio, buffer, trace);
                ++missing;
                continue;
            }
            family->variants[i].buffer = buffer;
            family->active[family->ready++] = (unsigned char)i;
            ++loaded;
        }
    }
    if (missing) {
        /* Recoverable partial registration; no trace-per-missing-variant storm.
         * Preserve the first originating failure and the recovery outcome. */
        if (first_failure) MAAGE_STATUS_TRACE(trace, first_failure,
            MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_LOAD);
        result = maage_cat_status(MAAGE_STATUS_PARTIAL, MAAGE_OPERATION_LOAD);
        if (trace) maage_status_trace_recover(trace, result, MAAGE_CONTEXT_RESOURCE,
            MAAGE_OPERATION_LOAD, __FILE__, (unsigned long)__LINE__);
        return result;
    }
    return maage_cat_status(loaded ? MAAGE_STATUS_SUCCESS : MAAGE_STATUS_NOOP,
        MAAGE_OPERATION_LOAD);
}

static unsigned char maage_cat_fallback(unsigned char family)
{
    if (family >= MAAGE_CAT_STEP_STONE && family <= MAAGE_CAT_STEP_CLOTH)
        return family == MAAGE_CAT_STEP_STONE ? MAAGE_CAT_NONE : MAAGE_CAT_STEP_STONE;
    if (family >= MAAGE_CAT_DIG_STONE && family <= MAAGE_CAT_DIG_CLOTH)
        return family == MAAGE_CAT_DIG_STONE ? MAAGE_CAT_NONE : MAAGE_CAT_DIG_STONE;
    if (family == MAAGE_CAT_GLASS) return MAAGE_CAT_DIG_STONE;
    if (family == MAAGE_CAT_FALL_SMALL || family == MAAGE_CAT_FALL_BIG)
        return MAAGE_CAT_STEP_STONE;
    if (family == MAAGE_CAT_SWIM || family == MAAGE_CAT_WATER) return MAAGE_CAT_SPLASH;
    if (family == MAAGE_CAT_LAVA) return MAAGE_CAT_LAVA_POP;
    return MAAGE_CAT_NONE;
}

/* 32-bit arithmetic on C90 unsigned long, including 64-bit hosts. No global
 * PRNG state and no dependence on frame timing or preload order. */
static unsigned long maage_cat_mix(unsigned long h, unsigned long value)
{ return ((h ^ (value & 0xffffffffUL)) * 1664525UL + 1013904223UL) & 0xffffffffUL; }
static unsigned long maage_cat_position_mix(unsigned long h, double coord)
{
    double abs_coord = coord < 0.0 ? -coord : coord;
    unsigned long scaled;
    if (abs_coord > 100000000.0 || !(abs_coord == abs_coord)) abs_coord = 0.0;
    scaled = (unsigned long)(abs_coord * 16.0);
    return maage_cat_mix(h, scaled ^ (coord < 0.0 ? 0x80000000UL : 0UL));
}

maage_status maage_sound_catalog_play(maage_sound_catalog *catalog,
    const maage_sound_event *event, maage_status_trace *trace)
{
    const maage_cat_rule *rule;
    maage_cat_family *family;
    unsigned int event_id, material;
    unsigned char selected, fallback;
    unsigned long hash;
    float gain, pitch, jitter;
    double position[3];
    maage_status result;
    if (!catalog || !event || !catalog->bound || !catalog->preloaded)
        return MAAGE_STATUS_PROPAGATE(trace, maage_cat_status(MAAGE_STATUS_INVALID_STATE,
            MAAGE_OPERATION_SUBMIT), MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_SUBMIT);
    event_id = event->event_id;
    if (!event_id || event_id >= MAAGE_CAT_EVENT_COUNT)
        return MAAGE_STATUS_PROPAGATE(trace, maage_cat_status(MAAGE_STATUS_NOT_FOUND,
            MAAGE_OPERATION_LOOKUP), MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_SUBMIT);
    if (!(event->intensity == event->intensity) || !(event->pitch == event->pitch))
        return MAAGE_STATUS_PROPAGATE(trace, maage_cat_status(MAAGE_STATUS_INVALID_ARGUMENT,
            MAAGE_OPERATION_SUBMIT), MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_SUBMIT);
    if (event->intensity <= 0.0f) return maage_cat_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_SUBMIT);
    rule = &maage_cat_rules[event_id];
    material = event->material < MAAGE_CAT_MATERIAL_COUNT ? event->material : 0;
    selected = rule->family;
    if (rule->material_map == 1) selected = maage_cat_step[material];
    else if (rule->material_map == 2) selected = maage_cat_dig[material];
    if (event_id == 5) selected = event->intensity >= 0.85f ?
        MAAGE_CAT_FALL_BIG : MAAGE_CAT_FALL_SMALL;
    family = &catalog->family[selected];
    fallback = selected;
    while (!family->ready) {
        fallback = maage_cat_fallback(fallback);
        if (!fallback) return MAAGE_STATUS_PROPAGATE(trace,
            maage_cat_status(MAAGE_STATUS_NOT_FOUND, MAAGE_OPERATION_LOOKUP),
            MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_SUBMIT);
        family = &catalog->family[fallback];
    }
    position[0] = event->position[0];
    position[1] = event->position[1];
    position[2] = event->position[2];
    if (rule->positional) {
        if (position[0] != position[0] || position[1] != position[1] ||
            position[2] != position[2] || position[0] > 100000000.0 ||
            position[0] < -100000000.0 || position[1] > 100000000.0 ||
            position[1] < -100000000.0 || position[2] > 100000000.0 ||
            position[2] < -100000000.0)
            return MAAGE_STATUS_PROPAGATE(trace,
                maage_cat_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_SUBMIT),
                MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_SUBMIT);
    } else position[0] = position[1] = position[2] = 0.0;
    hash = maage_cat_mix(2166136261UL, (unsigned long)event->tick);
    hash = maage_cat_mix(hash, (unsigned long)event->sequence);
    hash = maage_cat_mix(hash, (unsigned long)event_id * 31UL + material);
    hash = maage_cat_position_mix(hash, event->position[0]);
    hash = maage_cat_position_mix(hash, event->position[1]);
    hash = maage_cat_position_mix(hash, event->position[2]);
    selected = family->active[hash % family->ready];
    jitter = 0.96f + (float)((hash >> 12) & 255UL) / 255.0f * 0.08f;
    gain = rule->gain * (event->intensity > 2.0f ? 2.0f : event->intensity);
    pitch = rule->pitch * (event->pitch > 0.0f ? event->pitch : 1.0f);
    pitch *= event_id == 8 || event_id == 9 ? 1.0f : jitter;
    if (pitch < 0.5f) pitch = 0.5f;
    if (pitch > 2.0f) pitch = 2.0f;
    result = catalog->port.play(catalog->audio, family->variants[selected].buffer,
        rule->bus, rule->positional, position, gain, pitch, rule->range, trace);
    if (maage_status_failed(result)) return MAAGE_STATUS_PROPAGATE(trace, result,
        MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_SUBMIT);
    return result;
}

maage_status maage_sound_catalog_destroy(maage_sound_catalog *catalog)
{
    unsigned int f, i;
    maage_status first = maage_cat_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_DESTROY);
    maage_status result;
    if (!catalog) return maage_cat_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_DESTROY);
    if (catalog->bound) for (f = 1; f < MAAGE_CAT_FAMILY_COUNT; ++f) {
        for (i = 0; i < catalog->family[f].count; ++i) {
            unsigned long buffer = catalog->family[f].variants[i].buffer;
            if (!buffer) continue;
            result = catalog->port.release_buffer(catalog->audio, buffer, NULL);
            if (maage_status_failed(result) && !maage_status_failed(first)) first = result;
        }
    }
    free(catalog);
    return first;
}