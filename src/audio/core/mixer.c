#include "audio_internal.h"
#include <string.h>
#include <math.h>

static float clamp(float x)
{
    if (x < -1.0f) return -1.0f;
    if (x > 1.0f) return 1.0f;
    return x;
}
void maage_audio_mix(maage_audio *audio, float *output, unsigned int frames)
{
    unsigned int i, j;
    maage_audio_voice *v;
    maage_audio_buffer *b;
    double step, pos, frac;
    size_t a, z;
    float left, right, src_l, src_r, gain, pan, dist, dx, dy, dz;
    if (!output || !frames) return;
    memset(output, 0, (size_t)frames * 2 * sizeof(*output));
    if (!audio || frames > audio->quantum || !maage_audio_lock_try(audio->lock)) return;
    for (i = 0; i < audio->voice_count; ++i) {
        v = &audio->voices[i];
        b = v->buffer;
        if (!v->id || !b) continue;
        gain = v->gain * audio->volume[MAAGE_AUDIO_MASTER] * audio->volume[v->category];
        pan = 0.0f;
        if (v->positional) {
            dx = v->position.x - audio->listener.x;
            dy = v->position.y - audio->listener.y;
            dz = v->position.z - audio->listener.z;
            dist = (float)sqrt((double)dx*dx + (double)dy*dy + (double)dz*dz);
            gain /= 1.0f + 0.12f * dist * dist;
            if (dist > 0.0001f)
                pan = (dx*audio->right.x + dy*audio->right.y + dz*audio->right.z) / dist;
            if (pan < -1.0f) pan = -1.0f;
            if (pan > 1.0f) pan = 1.0f;
        }
        left = gain * (float)sqrt((double)(1.0f - pan) * 0.5);
        right = gain * (float)sqrt((double)(1.0f + pan) * 0.5);
        if (!v->positional) { left = gain; right = gain; }
        step = (double)v->pitch * (double)b->rate / (double)audio->rate;
        for (j = 0; j < frames; ++j) {
            pos = v->cursor;
            if (pos >= (double)b->frames) {
                if (!v->loop) { maage_audio_voice_release(v); break; }
                /* Loop boundary interpolates the final frame with frame zero. */
                pos = fmod(pos, (double)b->frames);
                v->cursor = pos;
            }
            a = (size_t)pos;
            z = (a + 1 == b->frames) ? (v->loop ? 0 : a) : a + 1;
            frac = pos - (double)a;
            src_l = (float)((1.0 - frac) * b->samples[a*b->channels] +
                frac * b->samples[z*b->channels]) / 32768.0f;
            if (b->channels == 1) src_r = src_l;
            else src_r = (float)((1.0 - frac) * b->samples[a*2+1] +
                frac * b->samples[z*2+1]) / 32768.0f;
            output[2*j] += src_l * left;
            output[2*j+1] += src_r * right;
            v->cursor += step;
        }
    }
    for (j = 0; j < 2 * frames; ++j) output[j] = clamp(output[j]);
    maage_audio_lock_leave(audio->lock);
}
