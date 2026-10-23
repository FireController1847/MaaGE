#include "audio_internal.h"
#include <string.h>
#include <math.h>

static float clamp(float x)
{
    if (x != x) return 0.0f;
    if (x < -1.0f) return -1.0f;
    if (x > 1.0f) return 1.0f;
    return x;
}
/* Entire callback path consumes preallocated, lock-protected PCM. Decoders
 * work on independent threads and never run while this lock is held. */
void maage_audio_mix(maage_audio *audio, float *output, unsigned int frames)
{
    unsigned int i, j, channels, source_rate;
    maage_audio_voice *v;
    maage_audio_buffer *b;
    maage_audio_stream *s;
    const float *samples;
    double step, pos, frac;
    size_t a, z;
    float left, right, src_l, src_r, gain, pan, dist, dx, dy, dz;
    if (!output || !frames) return;
    memset(output, 0, (size_t)frames * 2U * sizeof(*output));
    if (!audio || frames > audio->quantum || !maage_audio_lock_try(audio->lock)) return;
    for (i = 0; i < audio->voice_count; ++i) {
        v = &audio->voices[i];
        b = v->buffer;
        s = v->stream;
        if (!v->id || (!b && !s) || v->paused) continue;
        channels = b ? b->channels : s->channels;
        source_rate = b ? b->rate : s->rate;
        gain = v->gain * audio->volume[MAAGE_AUDIO_MASTER] * audio->volume[v->category];
        pan = 0.0f;
        if (v->positional) {
            dx = v->position.x - audio->listener.x;
            dy = v->position.y - audio->listener.y;
            dz = v->position.z - audio->listener.z;
            dist = (float)sqrt((double)dx*dx + (double)dy*dy + (double)dz*dz);
            /* Linear distance attenuation uses the voice's resolved range. */
            gain *= dist < v->max_distance ? 1.0f - dist / v->max_distance : 0.0f;
            if (dist > 0.0001f)
                pan = (dx*audio->right.x + dy*audio->right.y + dz*audio->right.z) / dist;
            if (pan < -1.0f) pan = -1.0f;
            if (pan > 1.0f) pan = 1.0f;
        }
        left = gain * (float)sqrt((double)(1.0f - pan) * 0.5);
        right = gain * (float)sqrt((double)(1.0f + pan) * 0.5);
        if (!v->positional) { left = gain; right = gain; }
        step = (double)v->pitch * (double)source_rate / (double)audio->rate;
        for (j = 0; j < frames; ++j) {
            pos = v->cursor;
            if (s) {
                /* Consume only frames actually presented. In an underrun,
                 * silence this voice without advancing playback position. */
                while (pos >= 1.0 && s->buffered) {
                    s->read_index = (s->read_index + 1U) % s->capacity;
                    --s->buffered;
                    pos -= 1.0;
                }
                v->cursor = pos;
                if (!s->buffered) {
                    if (s->ended) maage_audio_voice_release(v);
                    break;
                }
                if (s->buffered == 1U && !s->ended) break;
                a = s->read_index;
                z = s->buffered >= 2U ? (a + 1U) % s->capacity : a;
                samples = s->samples;
            } else {
                if (pos >= (double)b->frames) {
                    if (!v->loop) { maage_audio_voice_release(v); break; }
                    pos = fmod(pos, (double)b->frames);
                    v->cursor = pos;
                }
                a = (size_t)pos;
                z = (a + 1U == b->frames) ? (v->loop ? 0U : a) : a + 1U;
                samples = b->samples;
            }
            frac = s ? pos : pos - (double)a;
            src_l = (float)((1.0 - frac) * samples[a * channels] +
                frac * samples[z * channels]);
            if (channels == 1U) src_r = src_l;
            else src_r = (float)((1.0 - frac) * samples[a * 2U + 1U] +
                frac * samples[z * 2U + 1U]);
            output[2U*j] += src_l * left;
            output[2U*j+1U] += src_r * right;
            v->cursor += step;
        }
    }
    for (j = 0; j < 2U * frames; ++j) output[j] = clamp(output[j]);
    maage_audio_lock_leave(audio->lock);
}
