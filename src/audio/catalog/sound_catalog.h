#ifndef MAAGE_SOUND_CATALOG_H
#define MAAGE_SOUND_CATALOG_H

#include "maage/status.h"
#include "maage/sounds.h"
#include "maage/session_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct maage_sound_catalog maage_sound_catalog;

/* Client adapter callbacks run on the audio owner thread.
 * decode returns an owned immutable PCM reference in *pcm. upload MUST copy
 * its data into an Audio-owned buffer before returning; PCM is released even
 * if upload fails. A nonzero buffer ID denotes a successfully owned buffer.
 * play is a one-shot, does not take ownership of the buffer. The Audio engine
 * owns listener transforms, voice scheduling and category master volumes. */
typedef struct maage_sound_catalog_port {
    /* Register numeric IDs before the worker starts; decode without mutating the registry. */
    maage_status (*register_clip)(void *bank, const char *logical_asset, unsigned long *clip_id, maage_status_trace *trace);
    maage_status (*decode)(void *bank, unsigned long clip_id, void **pcm, maage_status_trace *trace);
    maage_status (*upload)(void *audio, const void *pcm, unsigned long *buffer_id, maage_status_trace *trace);
    void (*release_pcm)(void *bank, void *pcm);
    maage_status (*release_buffer)(void *audio, unsigned long buffer_id, maage_status_trace *trace);
    maage_status (*play)(void *audio, unsigned long buffer_id, unsigned int bus, int positional, const double position[3], float gain, float pitch, float max_distance, maage_status_trace *trace);
} maage_sound_catalog_port;

/* bank and audio are borrowed. They MUST outlive catalog destroy. The returned
 * catalog borrows immutable definitions and owns successfully uploaded buffers.
 * The definitions must outlive the catalog and its joined decoding work. */
maage_status maage_sound_catalog_create(void *sound_bank, void *audio, const maage_sound_registry *definitions, maage_sound_catalog **output, maage_status_trace *trace);
/* Accepted decode requests are bounded separately from registered resources.
 * Default 64; callers may configure any positive capacity before prepare. */
maage_status maage_sound_catalog_pending_capacity(maage_sound_catalog *, size_t);
/* Must be called once before prepare; the function table is copied by value.
 * The Client adapter bridges Audio and SoundBank ownership. */
maage_status maage_sound_catalog_set_port(maage_sound_catalog *catalog, const maage_sound_catalog_port *port, maage_status_trace *trace);
/* Initialize one idle decoder worker and register numeric path variants without
 * decoding them. Decoding happens only when requested during playback. */
maage_status maage_sound_catalog_prepare(maage_sound_catalog *catalog, maage_status_trace *trace);
/* Collect background decode completions on the audio owner; upload/replay
 * pending one-shots and release buffers after their last use. */
maage_status maage_sound_catalog_poll(maage_sound_catalog *catalog, double now);
/* Direct numeric registered-event dispatch with deterministic variant selection.
 * Missing event families return NOT_FOUND; inaudible intensity returns NOOP. */
maage_status maage_sound_catalog_play(maage_sound_catalog *catalog, const maage_sound_event *event, maage_status_trace *trace);
/* Joins pending decoder work and releases owned PCM and Audio buffers. NULL is a NOOP. */
maage_status maage_sound_catalog_destroy(maage_sound_catalog *catalog);

#ifdef __cplusplus
}
#endif
#endif
