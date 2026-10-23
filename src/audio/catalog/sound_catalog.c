#include "sound_catalog.h"
#include "sound_catalog_manifest.h"
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
    MAAGE_CAT_NONE
};
static const unsigned char maage_cat_dig[MAAGE_CAT_MATERIAL_COUNT] = {
    MAAGE_CAT_DIG_STONE, MAAGE_CAT_DIG_STONE, MAAGE_CAT_DIG_GRASS,
    MAAGE_CAT_DIG_GRAVEL, MAAGE_CAT_DIG_WOOD, MAAGE_CAT_DIG_SAND,
    MAAGE_CAT_DIG_GRAVEL, MAAGE_CAT_DIG_SNOW, MAAGE_CAT_DIG_STONE,
    MAAGE_CAT_GLASS, MAAGE_CAT_NONE, MAAGE_CAT_NONE,
    MAAGE_CAT_DIG_GRASS, MAAGE_CAT_DIG_CLOTH, MAAGE_CAT_DIG_WOOD,
    MAAGE_CAT_DIG_STONE
};

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
    {MAAGE_CAT_ITEM_BREAK, 0, MAAGE_SOUND_CATALOG_PLAYERS, 1, 1.00f, 1.00f}
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
            if (!entry->first && !i) sprintf(catalog->family[f].variants[i].path,
                "minecraft/sounds/%s.ogg", entry->stem);
            else sprintf(catalog->family[f].variants[i].path,
                "minecraft/sounds/%s%u.ogg", entry->stem, entry->first ? (unsigned int)entry->first + i : i + 1U);
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
    unsigned char selected;
    unsigned long hash;
    float gain, pitch, volume, range;
    double position[3];
    maage_status result;
    if (!catalog || !event || !catalog->bound || !catalog->preloaded)
        return MAAGE_STATUS_PROPAGATE(trace, maage_cat_status(MAAGE_STATUS_INVALID_STATE,
            MAAGE_OPERATION_SUBMIT), MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_SUBMIT);
    event_id = event->event_id;
    if (!event_id || event_id >= MAAGE_CAT_EVENT_COUNT)
        return MAAGE_STATUS_PROPAGATE(trace, maage_cat_status(MAAGE_STATUS_NOT_FOUND,
            MAAGE_OPERATION_LOOKUP), MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_SUBMIT);
    if (!(event->intensity >= 0.0) || event->intensity > (double)FLT_MAX / 16.0 ||
        !(event->pitch >= 0.0) || event->pitch > (double)FLT_MAX / 2.0)
        return MAAGE_STATUS_PROPAGATE(trace, maage_cat_status(MAAGE_STATUS_INVALID_ARGUMENT,
            MAAGE_OPERATION_SUBMIT), MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_SUBMIT);
    if (event->intensity <= 0.0f) return maage_cat_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_SUBMIT);
    rule = &maage_cat_rules[event_id];
    material = event->material;
    if (material >= MAAGE_CAT_MATERIAL_COUNT)
        return MAAGE_STATUS_PROPAGATE(trace, maage_cat_status(MAAGE_STATUS_NOT_FOUND,
            MAAGE_OPERATION_LOOKUP), MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_SUBMIT);
    selected = rule->family;
    if (rule->material_map == 1) selected = maage_cat_step[material];
    else if (rule->material_map >= 2) {
        selected = maage_cat_dig[material];
        if (rule->material_map == 3 && material == MAAGE_MATERIAL_GLASS)
            selected = MAAGE_CAT_STEP_STONE;
        else if (rule->material_map == 3 && material == MAAGE_MATERIAL_ANVIL)
            selected = MAAGE_CAT_ANVIL_LAND;
    }
    if (!selected) return maage_cat_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_SUBMIT);
    family = &catalog->family[selected];
    if (!family->ready) return MAAGE_STATUS_PROPAGATE(trace,
        maage_cat_status(MAAGE_STATUS_NOT_FOUND, MAAGE_OPERATION_LOOKUP),
        MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_SUBMIT);
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
    hash ^= hash >> 16;
    hash = (hash * 2246822519UL) & 0xffffffffUL;
    hash ^= hash >> 13;
    hash = (hash * 3266489917UL) & 0xffffffffUL;
    hash ^= hash >> 16;
    selected = family->active[hash % family->ready];
    volume = material == MAAGE_MATERIAL_ANVIL ? 0.3f : 1.0f;
    gain = rule->gain;
    pitch = rule->pitch;
    if (rule->material_map) {
        if (event_id == MAAGE_SOUND_BLOCK_BREAK || event_id == MAAGE_SOUND_BLOCK_PLACE)
            gain = (volume + 1.0f) * 0.5f;
        else if (event_id == MAAGE_SOUND_BLOCK_DIG) gain = (volume + 1.0f) * 0.125f;
        else gain *= volume;
        if (material == MAAGE_MATERIAL_METAL) pitch *= 1.5f;
    }
    gain *= (float)event->intensity;
    pitch *= (float)(event->pitch > 0.0 ? event->pitch : 1.0);
    /* Expand linear attenuation using the original volume,
     * then clamps the audible gain. Category/master gain is applied by Audio. */
    range = 16.0f * (gain > 1.0f ? gain : 1.0f);
    if (gain > 1.0f) gain = 1.0f;
    if (pitch < 0.5f) pitch = 0.5f;
    if (pitch > 2.0f) pitch = 2.0f;
    result = catalog->port.play(catalog->audio, family->variants[selected].buffer,
        rule->bus, rule->positional, position, gain, pitch, range, trace);
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
