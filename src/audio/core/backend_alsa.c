/* Native Linux ALSA output. Load libasound at runtime so compilation does not
 * depend on optional distro development headers and an unavailable sound card
 * degrades to a normal PARTIAL result. One bounded writer thread owns PCM I/O. */
#include "audio_internal.h"
#include "maage/thread.h"
#include <dlfcn.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

typedef struct maage_alsa_pcm maage_alsa_pcm;
typedef struct maage_alsa_state {
    void *library;
    maage_alsa_pcm *pcm;
    maage_thread *thread;
    maage_audio *audio;
    int stop;
    int (*open)(maage_alsa_pcm **, const char *, int, int);
    int (*close)(maage_alsa_pcm *);
    int (*set_params)(maage_alsa_pcm *, int, int, unsigned int, unsigned int, int, unsigned int);
    int (*format_value)(const char *);
    long (*writei)(maage_alsa_pcm *, const void *, unsigned long);
    int (*recover)(maage_alsa_pcm *, int, int);
    int (*drop)(maage_alsa_pcm *);
} maage_alsa_state;

static int alsa_symbols(maage_alsa_state *s) {
#define ALSA_SYMBOL(member, name)                          \
    do {                                                   \
        *(void **) (&s->member) = dlsym(s->library, name); \
        if (!s->member) return 0;                          \
    } while (0)
    ALSA_SYMBOL(open, "snd_pcm_open");
    ALSA_SYMBOL(close, "snd_pcm_close");
    ALSA_SYMBOL(set_params, "snd_pcm_set_params");
    ALSA_SYMBOL(format_value, "snd_pcm_format_value");
    ALSA_SYMBOL(writei, "snd_pcm_writei");
    ALSA_SYMBOL(recover, "snd_pcm_recover");
    ALSA_SYMBOL(drop, "snd_pcm_drop");
#undef ALSA_SYMBOL
    return 1;
}
static void alsa_writer(void *context) {
    maage_alsa_state *s = (maage_alsa_state *) context;
    maage_audio *audio = s->audio;
    float output[2048];
    signed short pcm[2048];
    unsigned int i, frames = audio->quantum;
    unsigned long offset, remain;
    long written;
    int stopped, native;
    for (;;) {
        maage_audio_lock_enter(audio->lock);
        stopped = s->stop;
        maage_audio_lock_leave(audio->lock);
        if (stopped) break;
        maage_audio_mix(audio, output, frames);
        for (i = 0; i < 2 * frames; ++i) {
            float value = output[i] * 32767.0f;
            pcm[i] = (signed short) (value >= 0.0f ? value + 0.5f : value - 0.5f);
        }
        offset = 0;
        remain = frames;
        while (remain) {
            written = s->writei(s->pcm, pcm + 2 * offset, remain);
            if (written < 0) {
                native = s->recover(s->pcm, (int) written, 0);
                if (!native) continue;
                maage_audio_lock_enter(audio->lock);
                stopped = s->stop;
                maage_audio_lock_leave(audio->lock);
                if (!stopped) maage_audio_backend_fail(audio,
                                                       maage_audio_result(MAAGE_STATUS_IO_ERROR, MAAGE_OPERATION_PRESENT,
                                                                          MAAGE_DETAIL_AUDIO_DEVICE_LOST),
                                                       (long) native);
                return;
            }
            if (!written) {
                maage_audio_backend_fail(audio, maage_audio_result(MAAGE_STATUS_IO_ERROR, MAAGE_OPERATION_PRESENT, MAAGE_DETAIL_AUDIO_DEVICE_LOST), 0);
                return;
            }
            offset += (unsigned long) written;
            remain -= (unsigned long) written;
        }
    }
}
static void alsa_dispose(maage_alsa_state *s) {
    if (!s) return;
    if (s->pcm) s->close(s->pcm);
    if (s->library) dlclose(s->library);
    free(s);
}
maage_status maage_audio_backend_start(maage_audio *audio, maage_status_trace *trace) {
    maage_alsa_state *s;
    int err, fmt;
    maage_status result;
    const unsigned short endian = 0x0102;
    const char *format = (*(const unsigned char *) &endian == 0x02) ? "S16_LE" : "S16_BE";
    (void) trace;
    s = (maage_alsa_state *) calloc(1, sizeof(*s));
    if (!s) return maage_audio_result(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_ACQUIRE, 0);
    s->audio = audio;
    s->library = dlopen("libasound.so.2", RTLD_NOW | RTLD_LOCAL);
    if (!s->library || !alsa_symbols(s)) {
        alsa_dispose(s);
        return maage_audio_result(MAAGE_STATUS_SYSTEM_NOT_SUPPORTED, MAAGE_OPERATION_ACQUIRE, 0);
    }
    fmt = s->format_value(format);
    if (fmt < 0) {
        alsa_dispose(s);
        return maage_audio_result(MAAGE_STATUS_SYSTEM_NOT_SUPPORTED, MAAGE_OPERATION_ACQUIRE, 0);
    }
    err = s->open(&s->pcm, "default", 0, 0);
    if (err < 0) {
        audio->device_native = err;
        alsa_dispose(s);
        return maage_audio_result(MAAGE_STATUS_IO_ERROR, MAAGE_OPERATION_ACQUIRE,
                                  MAAGE_DETAIL_AUDIO_DEVICE_UNAVAILABLE);
    }
    /* SND_PCM_ACCESS_RW_INTERLEAVED=3; ALSA provides optional soft resampling.
     * 40ms is a bounded device latency, not a CPU-side unbounded buffer. */
    err = s->set_params(s->pcm, fmt, 3, 2, audio->rate, 1, 40000);
    if (err < 0) {
        audio->device_native = err;
        alsa_dispose(s);
        return maage_audio_result(MAAGE_STATUS_SYSTEM_NOT_SUPPORTED, MAAGE_OPERATION_ACQUIRE,
                                  MAAGE_DETAIL_AUDIO_DEVICE_UNAVAILABLE);
    }
    audio->backend_state = s;
    result = maage_thread_start(alsa_writer, s, &s->thread);
    if (maage_status_failed(result)) {
        audio->backend_state = NULL;
        alsa_dispose(s);
        return result;
    }
    return maage_audio_result(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_ACQUIRE, 0);
}
void maage_audio_backend_stop(maage_audio *audio) {
    maage_alsa_state *s = (maage_alsa_state *) audio->backend_state;
    if (!s) return;
    maage_audio_lock_enter(audio->lock);
    s->stop = 1;
    maage_audio_lock_leave(audio->lock);
    /* Drop wakes a potentially blocked ALSA write during shutdown. */
    s->drop(s->pcm);
    if (s->thread) (void) maage_thread_join(s->thread);
    audio->backend_state = NULL;
    alsa_dispose(s);
}
