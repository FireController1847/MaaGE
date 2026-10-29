/* AudioUnit render callback uses preallocated device buffers and try-lock mixer. */
#include "audio_internal.h"
#include <AudioToolbox/AudioToolbox.h>
#include <stdlib.h>
#include <string.h>

typedef struct maage_apple_audio {
    AudioComponentInstance unit;
    maage_audio *audio;
} maage_apple_audio;
static OSStatus apple_render(void *context, AudioUnitRenderActionFlags *flags, const AudioTimeStamp *time, UInt32 bus, UInt32 frames, AudioBufferList *io) {
    maage_apple_audio *state = (maage_apple_audio *) context;
    UInt32 cursor = 0, span, remaining;
    float *samples;
    (void) flags;
    (void) time;
    (void) bus;
    if (!io || io->mNumberBuffers != 1 || !io->mBuffers[0].mData || io->mBuffers[0].mNumberChannels != 2) {
        UInt32 i;
        if (io)
            for (i = 0; i < io->mNumberBuffers; ++i)
                if (io->mBuffers[i].mData) memset(io->mBuffers[i].mData, 0, io->mBuffers[i].mDataByteSize);
        return noErr;
    }
    samples = (float *) io->mBuffers[0].mData;
    remaining = io->mBuffers[0].mDataByteSize / (2 * sizeof(float));
    if (remaining < frames) frames = remaining;
    while (cursor < frames) {
        span = frames - cursor;
        if (span > state->audio->quantum) span = state->audio->quantum;
        maage_audio_mix(state->audio, samples + 2 * cursor, (unsigned int) span);
        cursor += span;
    }
    /* Any excess buffer capacity is silent. */
    if (remaining > frames) memset(samples + 2 * frames, 0, (size_t) (remaining - frames) * 2 * sizeof(float));
    return noErr;
}
maage_status maage_audio_backend_start(maage_audio *audio, maage_status_trace *trace) {
    AudioComponentDescription description;
    AudioComponent component;
    AudioStreamBasicDescription format;
    AURenderCallbackStruct callback;
    maage_apple_audio *s;
    OSStatus err;
    (void) trace;
    s = (maage_apple_audio *) calloc(1, sizeof(*s));
    if (!s) return maage_audio_result(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_ACQUIRE, 0);
    s->audio = audio;
    memset(&description, 0, sizeof(description));
    description.componentType = kAudioUnitType_Output;
    description.componentSubType = kAudioUnitSubType_DefaultOutput;
    description.componentManufacturer = kAudioUnitManufacturer_Apple;
    component = AudioComponentFindNext(NULL, &description);
    if (!component) {
        free(s);
        return maage_audio_result(MAAGE_STATUS_SYSTEM_NOT_SUPPORTED, MAAGE_OPERATION_ACQUIRE, 0);
    }
    err = AudioComponentInstanceNew(component, &s->unit);
    if (err != noErr) goto fail;
    memset(&format, 0, sizeof(format));
    format.mSampleRate = (Float64) audio->rate;
    format.mFormatID = kAudioFormatLinearPCM;
    format.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
    format.mBytesPerPacket = 2 * sizeof(float);
    format.mFramesPerPacket = 1;
    format.mBytesPerFrame = 2 * sizeof(float);
    format.mChannelsPerFrame = 2;
    format.mBitsPerChannel = 32;
    err = AudioUnitSetProperty(s->unit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &format, sizeof(format));
    if (err != noErr) goto fail;
    callback.inputProc = apple_render;
    callback.inputProcRefCon = s;
    err = AudioUnitSetProperty(s->unit, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0, &callback, sizeof(callback));
    if (err != noErr) goto fail;
    err = AudioUnitInitialize(s->unit);
    if (err != noErr) goto fail;
    audio->backend_state = s;
    err = AudioOutputUnitStart(s->unit);
    if (err != noErr) {
        audio->backend_state = NULL;
        AudioUnitUninitialize(s->unit);
        goto fail;
    }
    return maage_audio_result(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_ACQUIRE, 0);
fail:
    audio->device_native = (long) err;
    if (s->unit) AudioComponentInstanceDispose(s->unit);
    free(s);
    return maage_audio_result(MAAGE_STATUS_IO_ERROR, MAAGE_OPERATION_ACQUIRE, MAAGE_DETAIL_AUDIO_DEVICE_UNAVAILABLE);
}
void maage_audio_backend_stop(maage_audio *audio) {
    maage_apple_audio *s = (maage_apple_audio *) audio->backend_state;
    if (!s) return;
    AudioOutputUnitStop(s->unit);
    AudioUnitUninitialize(s->unit);
    AudioComponentInstanceDispose(s->unit);
    audio->backend_state = NULL;
    free(s);
}
