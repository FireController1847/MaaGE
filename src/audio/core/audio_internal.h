#ifndef MAAGE_AUDIO_INTERNAL_H
#define MAAGE_AUDIO_INTERNAL_H
#include "maage/audio.h"
#include <stddef.h>

typedef struct maage_audio_lock maage_audio_lock;
maage_status maage_audio_lock_create(maage_audio_lock **output);
void maage_audio_lock_destroy(maage_audio_lock *lock);
void maage_audio_lock_enter(maage_audio_lock *lock);
int maage_audio_lock_try(maage_audio_lock *lock);
void maage_audio_lock_leave(maage_audio_lock *lock);

struct maage_audio_buffer {
    struct maage_audio *owner;
    struct maage_audio_buffer *next;
    float *samples;
    size_t frames;
    unsigned int channels, rate, references;
    int retired;
};
struct maage_audio_stream {
    struct maage_audio *owner;
    struct maage_audio_stream *next;
    float *samples;
    size_t capacity, read_index, buffered;
    unsigned int channels, rate, references;
    int retired, ended;
};
typedef struct maage_audio_voice {
    maage_audio_buffer *buffer;
    maage_audio_stream *stream;
    maage_audio_voice_id id;
    double cursor; /* Static position, or streaming fractional-frame offset. */
    maage_audio_vec3 position;
    float gain, pitch;
    unsigned int category;
    int positional, loop, paused;
} maage_audio_voice;
struct maage_audio {
    maage_audio_lock *lock;
    maage_audio_buffer *buffers;
    maage_audio_stream *streams;
    maage_audio_voice *voices;
    maage_audio_voice_id last_voice_id;
    unsigned int voice_count, rate, quantum;
    float volume[MAAGE_AUDIO_BUS_COUNT];
    maage_audio_vec3 listener, right;
    int device_ok;
    maage_status device_result;
    long device_native;
    void *backend_state;
};

/* The backend calls mix on at most quantum frames with a preallocated stereo
 * float target. Lock contention outputs silence; no waiting/allocation/IO. */
void maage_audio_mix(maage_audio *audio, float *output, unsigned int frames);
void maage_audio_voice_release(maage_audio_voice *voice); /* lock held */
void maage_audio_backend_fail(maage_audio *audio, maage_status result, long native);
maage_status maage_audio_backend_start(maage_audio *audio, maage_status_trace *trace);
void maage_audio_backend_stop(maage_audio *audio);
maage_status maage_audio_result(unsigned char code, unsigned char operation,
    unsigned char detail);
#endif
