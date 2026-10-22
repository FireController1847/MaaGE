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
/* Nonzero generational ID; zero is never a live voice. */
typedef maage_status maage_audio_voice_id;

typedef enum maage_audio_bus {
    MAAGE_AUDIO_MASTER = 0, MAAGE_AUDIO_BLOCKS = 1,
    MAAGE_AUDIO_PLAYER = 2, MAAGE_AUDIO_AMBIENT = 3,
    MAAGE_AUDIO_WEATHER = 4, MAAGE_AUDIO_MUSIC = 5,
    MAAGE_AUDIO_UI = 6, MAAGE_AUDIO_BUS_COUNT = 7
} maage_audio_bus;

typedef struct maage_audio_vec3 { float x, y, z; } maage_audio_vec3;
typedef struct maage_audio_config {
    unsigned int sample_rate; /* 0 = 48000 Hz; valid 8000..192000 */
    unsigned int max_voices; /* 0 = 64; valid 1..128 */
    unsigned int frames_per_buffer; /* 0 = 256; valid 64..1024 */
} maage_audio_config;
typedef struct maage_audio_play_desc {
    maage_audio_buffer *buffer;
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
maage_status maage_audio_buffer_destroy(maage_audio_buffer *buffer);
/* Returns BUFFER_OVERFLOW if all voice slots are occupied; no voice stealing.
 * voice_id is set to zero on non-playback outcomes, including absent device. */
maage_status maage_audio_play(maage_audio *audio,
    const maage_audio_play_desc *play_desc, maage_audio_voice_id *voice_id,
    maage_status_trace *trace);
maage_status maage_audio_stop(maage_audio *audio, maage_audio_voice_id voice_id);
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
