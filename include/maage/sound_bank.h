#ifndef MAAGE_SOUND_BANK_H
#define MAAGE_SOUND_BANK_H

#include <stddef.h>
#include "maage/status.h"

#ifdef __cplusplus
extern "C" {
#endif

/* One owner thread registers and decodes bank resources; do not concurrently
 * mutate/decode/destroy the same bank. No API runs on an audio callback.
 * Resource generation is immutable after registration; destroy and recreate a
 * bank to retry cached failures after Launcher-driven asset repair.
 *
 * Nonzero clip IDs are caller-assigned 32-bit values (0 is reserved). The
 * four-level sparse radix table provides deterministic O(1) lookup without
 * allocating storage proportional to the largest registered ID. Paths are
 * resolved during registration, not lookup.
 *
 * Decoding is lazy. Cache retention is LRU and bounded by memory_budget,
 * not counting caller-owned output or decoder scratch. A zero budget disables
 * retained PCM. The size limits below apply only to full-file decoding;
 * streaming does not retain the whole encoded file or PCM in memory.
 */
#define MAAGE_SOUND_MAX_CLIP_ID 0xffffffffUL
#define MAAGE_SOUND_MAX_PCM_BYTES (128UL * 1024UL * 1024UL)
#define MAAGE_SOUND_MAX_OGG_BYTES (64UL * 1024UL * 1024UL)

typedef struct maage_sound_bank maage_sound_bank;
typedef struct maage_sound_stream maage_sound_stream;

/* Value type: initialize to all-zero before the first decode. On success,
 * samples is a NEW caller-owned allocation, even on cache hits. It remains
 * immutable by contract while shared with a mixer and is never borrowed from
 * the bank. Destroy it after the final consumer releases it; do not shallow
 * copy this owning value or destroy it while a consumer retains its pointer.
 * On failure decode leaves the all-zero output intact.
 * frame_count counts frames, not floats; samples has frame_count*channels
 * interleaved float values. Channels are 1 or 2.
 */
typedef struct maage_sound_pcm {
    float *samples;
    size_t frame_count;
    unsigned int channels;
    unsigned int sample_rate;
} maage_sound_pcm;

/* data_root is a MaaGE application data directory (NULL = platform default).
 * Copying the root makes caller string lifetime irrelevant. output must be
 * non-NULL and *output must be NULL. memory_budget bounds cached PCM only. */
maage_status maage_sound_bank_create(const char *data_root, size_t memory_budget,
    maage_sound_bank **output, maage_status_trace *trace);

/* Exactly one registration per numeric ID. Asset names use existing
 * maage_minecraft_asset_path validation; only .ogg is accepted. Registration
 * never opens, downloads, or decodes the file. */
maage_status maage_sound_bank_register(maage_sound_bank *bank,
    unsigned long numeric_clip_id, const char *logical_asset_path,
    maage_status_trace *trace);

/* Copy out a caller-owned PCM. No string lookup, filesystem access on cache
 * hits, or negative-resource reopen after the first failed attempt.
 * NOT_FOUND is recoverable and scoped to the failing clip. Transient memory
 * allocation failures are not negatively cached. */
maage_status maage_sound_bank_decode(maage_sound_bank *bank,
    unsigned long numeric_clip_id, maage_sound_pcm *pcm,
    maage_status_trace *trace);

/* Incremental Ogg/Vorbis decoder; open/read/close run on a non-audio thread.
 * A stream owns its FILE and decoder independently from the bank. Reads produce
 * up to frame_capacity interleaved float frames; zero frames means EOF.
 * Decoding never occurs on the real-time mixer callback. */
maage_status maage_sound_bank_stream_open(maage_sound_bank *bank,
    unsigned long numeric_clip_id, maage_sound_stream **output,
    maage_status_trace *trace);
maage_status maage_sound_stream_format(const maage_sound_stream *stream,
    unsigned int *channels, unsigned int *sample_rate);
maage_status maage_sound_stream_read(maage_sound_stream *stream,
    float *samples, size_t frame_capacity, size_t *frames_read,
    maage_status_trace *trace);
maage_status maage_sound_stream_destroy(maage_sound_stream *stream);

maage_status maage_sound_pcm_destroy(maage_sound_pcm *pcm);
maage_status maage_sound_bank_destroy(maage_sound_bank *bank);

#ifdef __cplusplus
}
#endif
#endif
