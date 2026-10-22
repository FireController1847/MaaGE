/* WASAPI shared-mode event-driven output. All COM interfaces remain on one
 * dedicated MaaGE::Thread; only a Win32 event is signalled for teardown. */
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <ksmedia.h>
#include <objbase.h>
#include "audio_internal.h"
#include "maage/thread.h"
#include <stdlib.h>
#include <string.h>

typedef struct maage_wasapi_state {
    maage_audio *audio;
    maage_thread *worker;
    maage_mutex *mutex;
    maage_condition *condition;
    HANDLE wake;
    int ready, stop, sample_type;
    maage_status startup;
    long native;
} maage_wasapi_state;
/* 1 = IEEE float 32, 2 = signed PCM16. */
static int select_format(const WAVEFORMATEX *w)
{
    WORD tag = w->wFormatTag;
    if (!w->nChannels || w->nChannels > 8 || !w->nSamplesPerSec) return 0;
    if (tag == WAVE_FORMAT_EXTENSIBLE) {
        const WAVEFORMATEXTENSIBLE *e = (const WAVEFORMATEXTENSIBLE *)w;
        if (w->cbSize < 22) return 0;
        if (!memcmp(&e->SubFormat, &KSDATAFORMAT_SUBTYPE_IEEE_FLOAT, sizeof(GUID)))
            tag = WAVE_FORMAT_IEEE_FLOAT;
        else if (!memcmp(&e->SubFormat, &KSDATAFORMAT_SUBTYPE_PCM, sizeof(GUID)))
            tag = WAVE_FORMAT_PCM;
        else return 0;
    }
    if (tag == WAVE_FORMAT_IEEE_FLOAT && w->wBitsPerSample == 32 &&
        w->nBlockAlign == w->nChannels * sizeof(float)) return 1;
    if (tag == WAVE_FORMAT_PCM && w->wBitsPerSample == 16 &&
        w->nBlockAlign == w->nChannels * sizeof(short)) return 2;
    return 0;
}
static void convert_output(BYTE *destination, const WAVEFORMATEX *format,
    unsigned int sample_type, const float *stereo, unsigned int frames)
{
    unsigned int i, j, channels = format->nChannels;
    float value;
    for (i = 0; i < frames; ++i) for (j = 0; j < channels; ++j) {
        value = j == 0 ? stereo[2*i] : (j == 1 ? stereo[2*i+1] : 0.0f);
        if (channels == 1) value = 0.5f * (stereo[2*i] + stereo[2*i+1]);
        if (sample_type == 1)
            ((float *)destination)[i*channels+j] = value;
        else
            ((signed short *)destination)[i*channels+j] = (signed short)(value * 32767.0f);
    }
}
static void wasapi_worker(void *context)
{
    maage_wasapi_state *s = (maage_wasapi_state *)context;
    IMMDeviceEnumerator *enumerator = NULL;
    IMMDevice *device = NULL;
    IAudioClient *client = NULL;
    IAudioRenderClient *render = NULL;
    WAVEFORMATEX *format = NULL;
    UINT32 capacity = 0, padding = 0, available, span;
    BYTE *bytes;
    HRESULT hr;
    int initialized = 0, started = 0, running;
    unsigned int sample_type = 0;
    float scratch[2048];
    maage_status result = maage_audio_result(MAAGE_STATUS_IO_ERROR, MAAGE_OPERATION_ACQUIRE,
        MAAGE_DETAIL_AUDIO_DEVICE_UNAVAILABLE);
    hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(hr)) goto finish;
    initialized = 1;
    hr = CoCreateInstance(&CLSID_MMDeviceEnumerator, NULL, CLSCTX_INPROC_SERVER,
        &IID_IMMDeviceEnumerator, (void **)&enumerator);
    if (FAILED(hr)) goto finish;
    hr = IMMDeviceEnumerator_GetDefaultAudioEndpoint(enumerator, eRender, eConsole, &device);
    if (FAILED(hr)) goto finish;
    hr = IMMDevice_Activate(device, &IID_IAudioClient, CLSCTX_INPROC_SERVER,
        NULL, (void **)&client);
    if (FAILED(hr)) goto finish;
    hr = IAudioClient_GetMixFormat(client, &format);
    if (FAILED(hr)) goto finish;
    sample_type = select_format(format);
    if (!sample_type) {
        result = maage_audio_result(MAAGE_STATUS_SYSTEM_NOT_SUPPORTED,
            MAAGE_OPERATION_ACQUIRE, MAAGE_DETAIL_AUDIO_DEVICE_UNAVAILABLE);
        hr = E_NOTIMPL; goto finish;
    }
    hr = IAudioClient_Initialize(client, AUDCLNT_SHAREMODE_SHARED,
        AUDCLNT_STREAMFLAGS_EVENTCALLBACK, 1000000, 0, format, NULL);
    if (FAILED(hr)) goto finish;
    hr = IAudioClient_GetBufferSize(client, &capacity);
    if (FAILED(hr) || !capacity) goto finish;
    hr = IAudioClient_GetService(client, &IID_IAudioRenderClient, (void **)&render);
    if (FAILED(hr)) goto finish;
    s->wake = CreateEventW(NULL, FALSE, FALSE, NULL);
    if (!s->wake) { hr = HRESULT_FROM_WIN32(GetLastError()); goto finish; }
    hr = IAudioClient_SetEventHandle(client, s->wake);
    if (FAILED(hr)) goto finish;
    hr = IAudioRenderClient_GetBuffer(render, capacity, &bytes);
    if (FAILED(hr)) goto finish;
    hr = IAudioRenderClient_ReleaseBuffer(render, capacity, AUDCLNT_BUFFERFLAGS_SILENT);
    if (FAILED(hr)) goto finish;
    /* WASAPI shared-mode output uses the endpoint native mix rate. */
    s->audio->rate = format->nSamplesPerSec;
    hr = IAudioClient_Start(client);
    if (FAILED(hr)) goto finish;
    started = 1;
    result = maage_audio_result(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_ACQUIRE, 0);
    maage_mutex_lock(s->mutex);
    s->startup = result; s->ready = 1;
    maage_condition_signal(s->condition);
    maage_mutex_unlock(s->mutex);
    for (;;) {
        maage_mutex_lock(s->mutex);
        running = !s->stop;
        maage_mutex_unlock(s->mutex);
        if (!running) break;
        if (WaitForSingleObject(s->wake, 500) != WAIT_OBJECT_0) continue;
        maage_mutex_lock(s->mutex);
        running = !s->stop;
        maage_mutex_unlock(s->mutex);
        if (!running) break;
        hr = IAudioClient_GetCurrentPadding(client, &padding);
        if (FAILED(hr)) break;
        available = capacity - padding;
        while (available) {
            span = available > s->audio->quantum ? s->audio->quantum : available;
            hr = IAudioRenderClient_GetBuffer(render, span, &bytes);
            if (FAILED(hr)) break;
            maage_audio_mix(s->audio, scratch, span);
            convert_output(bytes, format, sample_type, scratch, span);
            hr = IAudioRenderClient_ReleaseBuffer(render, span, 0);
            if (FAILED(hr)) break;
            available -= span;
        }
        if (FAILED(hr)) break;
    }
    maage_mutex_lock(s->mutex);
    running = !s->stop;
    maage_mutex_unlock(s->mutex);
    if (running) maage_audio_backend_fail(s->audio,
        maage_audio_result(MAAGE_STATUS_IO_ERROR, MAAGE_OPERATION_PRESENT,
            MAAGE_DETAIL_AUDIO_DEVICE_LOST), (long)hr);
finish:
    if (started) IAudioClient_Stop(client);
    if (format) CoTaskMemFree(format);
    if (render) IAudioRenderClient_Release(render);
    if (client) IAudioClient_Release(client);
    if (device) IMMDevice_Release(device);
    if (enumerator) IMMDeviceEnumerator_Release(enumerator);
    if (initialized) CoUninitialize();
    maage_mutex_lock(s->mutex);
    if (!s->ready) {
        s->startup = result; s->native = (long)hr; s->ready = 1;
        maage_condition_signal(s->condition);
    }
    maage_mutex_unlock(s->mutex);
}
static void wasapi_cleanup(maage_wasapi_state *s)
{
    if (!s) return;
    if (s->wake) CloseHandle(s->wake);
    if (s->condition) (void)maage_condition_destroy(s->condition);
    if (s->mutex) (void)maage_mutex_destroy(s->mutex);
    free(s);
}
maage_status maage_audio_backend_start(maage_audio *audio, maage_status_trace *trace)
{
    maage_wasapi_state *s;
    maage_status result;
    (void)trace;
    s = (maage_wasapi_state *)calloc(1, sizeof(*s));
    if (!s) return maage_audio_result(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_ACQUIRE, 0);
    s->audio = audio;
    result = maage_mutex_create(&s->mutex);
    if (maage_status_failed(result)) { wasapi_cleanup(s); return result; }
    result = maage_condition_create(&s->condition);
    if (maage_status_failed(result)) { wasapi_cleanup(s); return result; }
    result = maage_thread_start(wasapi_worker, s, &s->worker);
    if (maage_status_failed(result)) { wasapi_cleanup(s); return result; }
    maage_mutex_lock(s->mutex);
    while (!s->ready) maage_condition_wait(s->condition, s->mutex);
    result = s->startup;
    audio->device_native = s->native;
    maage_mutex_unlock(s->mutex);
    if (maage_status_failed(result)) {
        (void)maage_thread_join(s->worker);
        wasapi_cleanup(s);
        return result;
    }
    audio->backend_state = s;
    return result;
}
void maage_audio_backend_stop(maage_audio *audio)
{
    maage_wasapi_state *s = (maage_wasapi_state *)audio->backend_state;
    if (!s) return;
    maage_mutex_lock(s->mutex);
    s->stop = 1;
    maage_mutex_unlock(s->mutex);
    if (s->wake) SetEvent(s->wake);
    (void)maage_thread_join(s->worker);
    audio->backend_state = NULL;
    wasapi_cleanup(s);
}
