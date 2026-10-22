#ifndef MAAGE_AUDIO_H
#define MAAGE_AUDIO_H
#include "maage/status.h"
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

/* All public objects are independent of Client, World and renderer types.
 * API mutations may originate from any non-callback thread; callers must not
 * race destroy with another public operation. PCM pointers are copied. */
typedef struct maage_audio maage_audio;
typedef struct maage_audio_buffer maage_audio_buffer;
typedef struct maage_audio_stream maage_audio_stream;
/* Monotonically allocated, never reused within an engine lifetime; zero is
 * reserved. This is independent of the configured voice-slot capacity. */
typedef unsigned long maage_audio_voice_id;
typedef enum maage_audio_voice_state {
    MAAGE_AUDIO_VOICE_STOPPED = 0,
    MAAGE_AUDIO_VOICE_PLAYING = 1,
    MAAGE_AUDIO_VOICE_PAUSED = 2
} maage_audio_voice_state;

typedef enum maage_audio_bus {
    MAAGE_AUDIO_MASTER = 0, MAAGE_AUDIO_BLOCKS = 1,
    MAAGE_AUDIO_PLAYER = 2, MAAGE_AUDIO_AMBIENT = 3,
    MAAGE_AUDIO_WEATHER = 4, MAAGE_AUDIO_MUSIC = 5,
    MAAGE_AUDIO_UI = 6, MAAGE_AUDIO_BUS_COUNT = 7
} maage_audio_bus;

typedef struct maage_audio_vec3 { float x, y, z; } maage_audio_vec3;
typedef struct maage_audio_config {
    unsigned int sample_rate; /* 0 = 48000 Hz; valid 8000..192000 */
    unsigned int max_voices; /* 0 = 64; resource-budget, no artificial ceiling */
    unsigned int frames_per_buffer; /* 0 = 256; valid 64..1024 */
} maage_audio_config;
typedef struct maage_audio_play_desc {
    maage_audio_buffer *buffer; /* Exactly one of buffer/stream is non-NULL. */
    maage_audio_stream *stream;
    maage_audio_bus category; /* One of BLOCKS..UI, not MASTER. */
    float gain; /* 0..4 */
    float pitch; /* 0.25..4; 1 = natural playback rate */
    maage_audio_vec3 position;
    int positional; /* Nonzero to apply distance attenuation and panning. */
    int loop; /* Nonzero for seamless, interpolated loop wrap. */
} maage_audio_play_desc;

/* A device failure returns PARTIAL with a non-NULL engine and no output device.
 * It is safe to destroy the engine and recover by recreating it at an explicit
 * lifecycle boundary; poll never opens devices automatically. */
maage_status maage_audio_create(const maage_audio_config *config,
    maage_audio **output, maage_status_trace *trace);
maage_status maage_audio_destroy(maage_audio *audio, maage_status_trace *trace);
/* signed 16-bit interleaved PCM, exactly 1 (mono) or 2 (stereo) channels;
 * frame_count is per channel. The audio owns a deep immutable copy until all
 * active voices stop/finish, even after buffer_destroy invalidates the handle.
 * An audio engine must outlive all of its publicly held buffer handles. */
maage_status maage_audio_buffer_create(maage_audio *audio,
    const signed short *interleaved_pcm, size_t frame_count,
    unsigned int channels, unsigned int sample_rate,
    maage_audio_buffer **output, maage_status_trace *trace);
/* Float PCM avoids an intermediate 16-bit conversion in the resource adapter.
 * Source data is deep-copied. */
maage_status maage_audio_buffer_create_float(maage_audio *audio,
    const float *interleaved_pcm, size_t frame_count,
    unsigned int channels, unsigned int sample_rate,
    maage_audio_buffer **output, maage_status_trace *trace);
maage_status maage_audio_buffer_destroy(maage_audio_buffer *buffer);

/* Producer writes bounded interleaved float PCM outside the audio callback.
 * A stream borrows no input memory after write returns; accepted may be less
 * than requested when the ring is full. Finish signals EOF or decode failure.
 * The owner must stop/join its producer before stream_destroy or engine destroy.
 * The Audio engine must outlive every stream handle. */
maage_status maage_audio_stream_create(maage_audio *audio,
    unsigned int channels, unsigned int sample_rate, size_t capacity_frames,
    maage_audio_stream **output, maage_status_trace *trace);
maage_status maage_audio_stream_write(maage_audio_stream *stream,
    const float *samples, size_t frames, size_t *accepted);
maage_status maage_audio_stream_finish(maage_audio_stream *stream);
maage_status maage_audio_stream_destroy(maage_audio_stream *stream);
/* Returns BUFFER_OVERFLOW if the configured active-voice resource budget is
 * exhausted; no voice stealing. IDs never alias previously issued handles.
 * voice_id is zero on non-playback outcomes. */
maage_status maage_audio_play(maage_audio *audio,
    const maage_audio_play_desc *play_desc, maage_audio_voice_id *voice_id,
    maage_status_trace *trace);
maage_status maage_audio_stop(maage_audio *audio, maage_audio_voice_id voice_id);
maage_status maage_audio_stop_all(maage_audio *audio);
maage_status maage_audio_voice_get_state(maage_audio *audio,
    maage_audio_voice_id voice_id, maage_audio_voice_state *state);
maage_status maage_audio_voice_pause(maage_audio *audio,
    maage_audio_voice_id voice_id, int paused);
maage_status maage_audio_voice_set_gain(maage_audio *audio,
    maage_audio_voice_id voice_id, float gain);
maage_status maage_audio_voice_set_pitch(maage_audio *audio,
    maage_audio_voice_id voice_id, float pitch);
maage_status maage_audio_voice_set_position(maage_audio *audio,
    maage_audio_voice_id voice_id, maage_audio_vec3 position, int positional);
maage_status maage_audio_set_listener(maage_audio *audio,
    maage_audio_vec3 position, maage_audio_vec3 forward, maage_audio_vec3 up);
/* Gain 0..4, MASTER is multiplied with the voice's category. */
maage_status maage_audio_set_volume(maage_audio *audio, maage_audio_bus bus, float gain);
/* Reclaims stopped/destroyed buffers and reports persistent device failures.
 * Never performs device retries or invokes asset I/O. */
maage_status maage_audio_poll(maage_audio *audio, maage_status_trace *trace);
#ifdef __cplusplus
}
#endif
#endif
