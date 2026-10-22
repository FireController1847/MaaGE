#ifndef MAAGE_SOUND_CATALOG_H
#define MAAGE_SOUND_CATALOG_H

#include "maage/status.h"
#include "maage/session_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Public numeric bus requests. The Client-owned adapter maps these to the
 * Audio engine's category IDs; these are not backend audio category IDs. */
typedef enum maage_sound_catalog_bus {
    MAAGE_SOUND_CATALOG_BLOCKS = 0,
    MAAGE_SOUND_CATALOG_PLAYERS = 1,
    MAAGE_SOUND_CATALOG_UI = 2,
    MAAGE_SOUND_CATALOG_AMBIENT = 3,
    MAAGE_SOUND_CATALOG_WEATHER = 4
} maage_sound_catalog_bus;

typedef struct maage_sound_catalog maage_sound_catalog;

/* Temporary integration seam while Phase 6 Audio/SoundBank contracts are
 * concurrently developed. All callbacks run on the Client audio owner thread.
 * decode returns an owned immutable PCM reference in *pcm. upload MUST copy
 * its data into an Audio-owned buffer before returning; PCM is released even
 * if upload fails. A nonzero buffer ID denotes a successfully owned buffer.
 * play is a one-shot, does not take ownership of the buffer. The Audio engine
 * owns listener transforms, voice scheduling and category master volumes. */
typedef struct maage_sound_catalog_port {
    maage_status (*decode)(void *bank, const char *logical_asset,
        void **pcm, maage_status_trace *trace);
    maage_status (*upload)(void *audio, const void *pcm,
        unsigned long *buffer_id, maage_status_trace *trace);
    void (*release_pcm)(void *bank, void *pcm);
    maage_status (*release_buffer)(void *audio, unsigned long buffer_id,
        maage_status_trace *trace);
    maage_status (*play)(void *audio, unsigned long buffer_id,
        unsigned int bus, int positional, const double position[3],
        float gain, float pitch, float max_distance, maage_status_trace *trace);
} maage_sound_catalog_port;

/* bank and audio are borrowed. They MUST outlive catalog destroy. The returned
 * catalog owns the registered path table and successfully uploaded buffers. */
maage_status maage_sound_catalog_create(void *sound_bank, void *audio,
    maage_sound_catalog **output, maage_status_trace *trace);
/* Must be called once before preload; the function table is copied by value.
 * Adapters may bridge to the eventually finalized Audio and SoundBank APIs. */
maage_status maage_sound_catalog_set_port(maage_sound_catalog *catalog,
    const maage_sound_catalog_port *port, maage_status_trace *trace);
/* Performs decode/upload outside the real-time playback path. PARTIAL signals
 * unavailable variants; valid variants remain usable. Idempotent thereafter. */
maage_status maage_sound_catalog_preload(maage_sound_catalog *catalog,
    maage_status_trace *trace);
/* Direct numeric event/material dispatch with deterministic variant selection.
 * Missing event families return NOT_FOUND; inaudible intensity returns NOOP. */
maage_status maage_sound_catalog_play(maage_sound_catalog *catalog,
    const maage_sound_event *event, maage_status_trace *trace);
/* Releases every catalog-owned Audio buffer. NULL is a NOOP. */
maage_status maage_sound_catalog_destroy(maage_sound_catalog *catalog);

#ifdef __cplusplus
}
#endif
#endif